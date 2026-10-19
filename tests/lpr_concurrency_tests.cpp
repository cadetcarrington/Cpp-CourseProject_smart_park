// 车牌识别并发/进程泄漏回归测试。
//
// 背景（代码评审 P2）：早先 recognizePlateAsync 每个请求起一个 QProcess，没有
// 并发上限；16 个并发请求会同时拉起 16 套 python/Paddle OCR。60 秒看门狗只
// kill /bin/sh，recognize_plate.py 自己 Popen 出来的 Paddle OCR 子进程会活下来。
// 这里用假 shell 脚本当 lprCommand，真实地测：
//   1. 并发上限 2 + 排队上限 4：排满了立刻失败，且同时运行数不超过 2；
//   2. 超时后整棵进程树（脚本后台化的 marker）都被杀掉；
//   3. context 销毁：排队任务丢弃、运行中任务整组杀掉、不回调、工人槽能恢复；
//   4. 正常路径照旧解析 JSON 且只回调一次；
//   5. 同步入口的超时同样清理整棵进程树。
//
// 注意（一）：调度器参数在进程内第一次用到识别时读取一次，所以整个测试二进制
// 共用一套配置（在 main() 一开始设置）：并发 2 / 排队 4 / 超时 500ms。
//
// 注意（二）：测试里每个脚本都是运行期新写到临时目录的。macOS 对新写入的可执行
// 脚本第一次 exec 会做一次约 0.3~0.4 秒的校验，第二次只要十几毫秒；这段一次性
// 开销会吞掉 500ms 超时的大半，导致用例偶发失败（真实脚本在仓库里、跑过一次后
// 也是热的，不受影响）。因此每个脚本都带 SMARTPARK_LPR_TEST_WARM 短路分支，
// 正式提交识别前先 warmUpScript() 空跑一次把它预热掉。
//
// 注意（三）：用 pgrep -f 找带唯一 tag 的 marker 进程（macOS 的 /bin/ps 是 setuid
// 程序，在受限沙箱里不能 exec）。另外用 marker 自己写出的 pid + kill(pid, 0)
// 做更严格的存活判断：marker 是脚本后台化出来的孙子进程，只有整组杀干净它才会死。
//
// 全部用事件循环 + 截止时间轮询，不靠长 sleep；每个用例结束都兜底清进程。

#include "network/PlateRecognition.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QJsonObject>
#include <QList>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <cmath>
#include <functional>
#include <iostream>

using smartpark::network::LprOutcome;
using smartpark::network::LprStatus;
using smartpark::network::recognizePlate;
using smartpark::network::recognizePlateAsync;

namespace{

int g_failures = 0;
QTemporaryDir *g_tempDir = nullptr;
QStringList g_tags;          // 本进程创建过的 marker tag，退出前兜底清理
QStringList g_dirs;          // 本进程创建过的临时目录（脚本/命令行里带着它）
QList<qint64> g_markerPids;  // 见过的 marker pid，退出前兜底清理

void expect(bool condition, const QString &message){
    if (!condition){
        ++g_failures;
        std::cerr << "FAILED: " << message.toStdString() << '\n';
    }
}

// 泵事件循环直到 condition 成立或超时；返回最终结果。
bool pumpUntil(const std::function<bool()> &condition, int timeoutMs = 15000){
    if (condition()){
        return true;
    }
    {
        QEventLoop loop;
        QTimer poll;
        poll.setInterval(5);
        QObject::connect(&poll, &QTimer::timeout, &loop, [&]{
            if (condition()){
                loop.quit();
            }
        });
        poll.start();
        QTimer deadline;
        deadline.setSingleShot(true);
        deadline.start(timeoutMs);
        QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        loop.exec();
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    return condition();
}

void pumpFor(int milliseconds){
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

QString freshDir(const QString &name){
    const QString path = g_tempDir->path() + QLatin1Char('/') + name;
    QDir().mkpath(path);
    g_dirs << path;
    return path;
}

QString writeScript(const QString &dir, const QString &name,
                    const QString &body){
    const QString path = dir + QLatin1Char('/') + name;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)){
        expect(false, QStringLiteral("cannot create script %1").arg(path));
        return {};
    }
    file.write(body.toUtf8());
    file.close();
    QFile::setPermissions(
        path, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                  | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                  | QFileDevice::ExeGroup | QFileDevice::ReadOther
                  | QFileDevice::ExeOther);
    if (!QFileInfo(path).isExecutable()){
        expect(false, QStringLiteral("script %1 is not executable").arg(path));
    }
    return path;
}

// 新写入的脚本第一次 exec 很慢（见文件头注意二）：先以 WARM 模式空跑一次。
// 用和调度器完全一致的调用形式（/bin/sh -c "<脚本> <参数>"）确保预热的是同一条
// exec 路径；脚本里的 SMARTPARK_LPR_TEST_WARM 短路让这次空跑立刻退出。
void warmUpScript(const QString &path){
    if (path.isEmpty()){
        return;
    }
    QProcess warm;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("SMARTPARK_LPR_TEST_WARM"), QStringLiteral("1"));
    warm.setProcessEnvironment(env);
    warm.start(QStringLiteral("/bin/sh"),
               QStringList{QStringLiteral("-c"),
                           path + QStringLiteral(" /dev/null")});
    bool finished = warm.waitForFinished(8000);
    if (!finished){
        warm.kill();
        warm.waitForFinished(2000);
    }
    expect(finished && warm.exitStatus() == QProcess::NormalExit
               && warm.exitCode() == 0,
           QStringLiteral("warm-up exec of %1 succeeds (finished=%2, code=%3)")
               .arg(path).arg(finished).arg(warm.exitCode()));
}

QString uniqueTag(const QString &kind){
    static int counter = 0;
    const QString tag = QStringLiteral("LPRTAG_%1_%2_%3")
                            .arg(QCoreApplication::applicationPid())
                            .arg(++counter)
                            .arg(kind.toUpper());
    g_tags << tag;
    return tag;
}

int readIntFile(const QString &path){
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)){
        return -1;
    }
    bool ok = false;
    const int value = QString::fromUtf8(file.readAll()).trimmed().toInt(&ok);
    return ok ? value : -1;
}

#ifdef Q_OS_UNIX
bool processAlive(qint64 pid){
    if (pid <= 0){
        return false;
    }
    return ::kill(static_cast<pid_t>(pid), 0) == 0;
}

// 杀一个进程及其进程组（脚本 setsid 后 pgid == 脚本 pid；marker 的 pgid 就是
// 外层脚本的 pid）。测试兜底清理用，正常路径由调度器自己负责。
void killProcessTree(qint64 pid){
    if (!processAlive(pid)){
        return;
    }
    const pid_t pgid = ::getpgid(static_cast<pid_t>(pid));
    if (pgid > 0 && pgid != ::getpgrp()){
        ::kill(-pgid, SIGKILL);
    }
    ::kill(static_cast<pid_t>(pid), SIGKILL);
}

// pgrep -f 用完整命令行匹配（macOS 的 /bin/ps 是 setuid 程序，沙箱里不能 exec）。
// 第一次调用时探测 pgrep 是否可用；不可用则跳过基于命令行的断言（pid 存活检查
// 仍然有效，而且更严格）。
bool pgrepAvailable(){
    static int cached = -1;
    if (cached < 0){
        QProcess probe;
        probe.start(QStringLiteral("/usr/bin/pgrep"),
                    QStringList{QStringLiteral("-f"),
                                QStringLiteral("__smartpark_lpr_no_such_process__")});
        cached = (probe.waitForStarted(2000) && probe.waitForFinished(3000)) ? 1 : 0;
        if (cached == 0){
            std::cerr << "WARNING: pgrep unavailable, tag-based process checks "
                         "are skipped (pid liveness checks still run)\n";
        }
    }
    return cached == 1;
}

QList<qint64> processesMatching(const QString &needle){
    QList<qint64> pids;
    if (needle.isEmpty() || !pgrepAvailable()){
        return pids;
    }
    QProcess pgrep;
    pgrep.start(QStringLiteral("/usr/bin/pgrep"),
                QStringList{QStringLiteral("-f"), needle});
    if (!pgrep.waitForFinished(5000)){
        pgrep.kill();
        pgrep.waitForFinished(1000);
        return pids;
    }
    // pgrep：1 = 没有匹配，0 = 有匹配；其它退出码按“没查到”处理。
    const QList<QByteArray> lines = pgrep.readAllStandardOutput().split('\n');
    for (const QByteArray &line : lines){
        bool ok = false;
        const qint64 pid = line.trimmed().toLongLong(&ok);
        if (ok && pid > 0 && pid != QCoreApplication::applicationPid()){
            pids << pid;
        }
    }
    return pids;
}

bool waitForNoProcess(const QString &needle, int timeoutMs){
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs){
        if (processesMatching(needle).isEmpty()){
            return true;
        }
        pumpFor(50);
    }
    return processesMatching(needle).isEmpty();
}

void killMatching(const QString &needle){
    if (needle.isEmpty()){
        return;
    }
    for (qint64 pid : processesMatching(needle)){
        killProcessTree(pid);
    }
}

qint64 markerPid(const QString &markerFile){
    QFile file(markerFile);
    if (!file.open(QIODevice::ReadOnly)){
        return -1;
    }
    bool ok = false;
    const qint64 pid = QString::fromUtf8(file.readAll()).trimmed().toLongLong(&ok);
    return ok ? pid : -1;
}

// 等 marker 进程真的起来（marker 文件里写了 pid 且进程还活着），返回 pid。
qint64 waitForMarker(const QString &markerFile, int timeoutMs){
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs){
        const qint64 pid = markerPid(markerFile);
        if (pid > 0 && processAlive(pid)){
            g_markerPids << pid;
            return pid;
        }
        pumpFor(25);
    }
    return -1;
}

bool waitForMarkerDead(qint64 pid, int timeoutMs){
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs){
        if (!processAlive(pid)){
            return true;
        }
        pumpFor(25);
    }
    return !processAlive(pid);
}
#else
bool processAlive(qint64){ return false; }
void killProcessTree(qint64){}
bool pgrepAvailable(){ return false; }
QList<qint64> processesMatching(const QString &){ return {}; }
bool waitForNoProcess(const QString &, int){ return true; }
void killMatching(const QString &){}
qint64 markerPid(const QString &){ return -1; }
qint64 waitForMarker(const QString &, int){ return -1; }
bool waitForMarkerDead(qint64, int){ return true; }
#endif

// 测试结束兜底：按 tag / 临时目录路径 / 已知 marker pid 杀残留（重复跑不越积越多）。
void cleanupLeftovers(){
    for (const QString &tag : g_tags){
        killMatching(tag);
    }
    for (const QString &dir : g_dirs){
        killMatching(dir);
    }
    for (qint64 pid : g_markerPids){
        killProcessTree(pid);
    }
    g_markerPids.clear();
}

// ---------------------------------------------------------------------------
// 假脚本：全部带 WARM 短路分支（预热用，见文件头注意二）。
// ---------------------------------------------------------------------------

// 并发计数器：用 mkdir 锁串行化「当前并发数/历史最大值」的读改写；跑约 0.3
// 秒（小于测试配置的 500ms 超时）；输出可解析的 JSON（同时验证结果解析）。
const char *kConcurrencyScript = R"SCRIPT(#!/bin/sh
if [ "${SMARTPARK_LPR_TEST_WARM:-}" = "1" ]; then exit 0; fi
set -u
d="${SMARTPARK_LPR_TEST_DIR:?}"
lock="$d/counter.lock"
i=0
while ! mkdir "$lock" 2>/dev/null; do
    i=$((i + 1))
    if [ "$i" -gt 2000 ]; then break; fi
    sleep 0.01
done
cur=0
if [ -f "$d/current" ]; then cur=$(cat "$d/current"); fi
cur=$((cur + 1))
echo "$cur" > "$d/current"
max=0
if [ -f "$d/max" ]; then max=$(cat "$d/max"); fi
if [ "$cur" -gt "$max" ]; then echo "$cur" > "$d/max"; fi
rmdir "$lock" 2>/dev/null
sleep 0.3
i=0
while ! mkdir "$lock" 2>/dev/null; do
    i=$((i + 1))
    if [ "$i" -gt 2000 ]; then break; fi
    sleep 0.01
done
cur=1
if [ -f "$d/current" ]; then cur=$(cat "$d/current"); fi
cur=$((cur - 1))
echo "$cur" > "$d/current"
rmdir "$lock" 2>/dev/null
printf '{"plate":"京B00001","confidence":0.9}\n'
)SCRIPT";

const char *kSuccessScript = R"SCRIPT(#!/bin/sh
if [ "${SMARTPARK_LPR_TEST_WARM:-}" = "1" ]; then exit 0; fi
printf '{"plate":"京A12345","confidence":0.97}\n'
)SCRIPT";

// 挂起脚本：后台起一个带唯一 tag 的 marker（先把 pid 写进 markerFile，证明真的
// 起来了），然后前台 sleep 300 卡住，等调度器超时/取消来杀整个进程组。
QString hangScriptBody(const QString &tag, const QString &markerFile){
    return QStringLiteral(
               "#!/bin/sh\n"
               "if [ \"${SMARTPARK_LPR_TEST_WARM:-}\" = \"1\" ]; then exit 0; fi\n"
               "sh -c 'echo $$ > \"$1\"; sleep 300; :' lpr-marker-%1 \"%2\" &\n"
               "sleep 300\n")
        .arg(tag, markerFile);
}

// ---------------------------------------------------------------------------
// 用例
// ---------------------------------------------------------------------------

void testConcurrencyCapAndQueueLimit(){
    const QString dir = freshDir(QStringLiteral("concurrency"));
    qputenv("SMARTPARK_LPR_TEST_DIR", dir.toUtf8());
    const QString script = writeScript(
        dir, QStringLiteral("work.sh"), QString::fromUtf8(kConcurrencyScript));
    warmUpScript(script);
    QFile::remove(dir + QStringLiteral("/current"));
    QFile::remove(dir + QStringLiteral("/max"));

    QObject context;
    int okCount = 0;
    int rejectedCount = 0;
    int otherFailures = 0;
    int total = 0;
    QStringList failureNotes;
    QStringList statusNotes;
    // 配置：并发 2、排队 4。一次性连发 8 个请求 → 2 个在跑 + 4 个排队，
    // 最后 2 个正好撞上「排队已满」。
    for (int i = 0; i < 8; ++i){
        recognizePlateAsync(
            QByteArrayLiteral("image-") + QByteArray::number(i), script, &context,
            [&](LprOutcome outcome){
                ++total;
                statusNotes << QStringLiteral("%1:%2")
                                   .arg(static_cast<int>(outcome.status))
                                   .arg(outcome.note);
                if (outcome.status == LprStatus::Ok){
                    ++okCount;
                } else if (outcome.note.contains(QStringLiteral("排队已满"))){
                    ++rejectedCount;
                } else{
                    ++otherFailures;
                    failureNotes << outcome.note;
                }
            });
    }
    expect(pumpUntil([&]{ return rejectedCount >= 2; }, 8000),
           QStringLiteral("overflow requests get the queue-full answer"));
    expect(rejectedCount == 2 && okCount == 0,
           QStringLiteral("queue-full failures fail fast, before the first ~300ms "
                          "recognition finishes (rejected=%1 ok=%2)")
               .arg(rejectedCount).arg(okCount));
    expect(pumpUntil([&]{ return total == 8; }, 30000),
           QStringLiteral("all accepted recognition tasks eventually finish "
                          "(callbacks=%1)")
               .arg(total));
    QStringList details;
    for (const QString &note : statusNotes){
        details << note;
    }
    expect(okCount == 6 && rejectedCount == 2 && otherFailures == 0,
           QStringLiteral("expected 6 ok + 2 rejected, got ok=%1 rejected=%2 "
                          "other=%3 failures=[%4] all=[%5]")
               .arg(okCount).arg(rejectedCount).arg(otherFailures)
               .arg(failureNotes.join(QStringLiteral("; ")),
                    details.join(QStringLiteral(" "))));

    const int maxObserved = readIntFile(dir + QStringLiteral("/max"));
    expect(maxObserved >= 1 && maxObserved <= 2,
           QStringLiteral("at most 2 recognitions run concurrently (observed %1)")
               .arg(maxObserved));
    expect(readIntFile(dir + QStringLiteral("/current")) == 0,
           QStringLiteral("all concurrent slots were released"));
}

void testTimeoutKillsProcessTree(){
    const QString dir = freshDir(QStringLiteral("timeout"));
    qputenv("SMARTPARK_LPR_TEST_DIR", dir.toUtf8());
    const QString tag = uniqueTag(QStringLiteral("timeout"));
    const QString markerFile = dir + QStringLiteral("/marker.started");
    const QString script = writeScript(
        dir, QStringLiteral("hang.sh"), hangScriptBody(tag, markerFile));
    warmUpScript(script);

    QObject context;
    bool called = false;
    LprOutcome outcome;
    QElapsedTimer elapsed;
    elapsed.start();
    recognizePlateAsync(QByteArrayLiteral("timeout-image"), script, &context,
                        [&](LprOutcome result){
                            called = true;
                            outcome = result;
                        });
    // marker 是脚本后台化出来的孙子进程：它活着就证明脚本真的跑起来了。
    const qint64 pid = waitForMarker(markerFile, 5000);
    expect(pid > 0,
           QStringLiteral("timeout script spawned its background marker (pid=%1)")
               .arg(pid));
    if (pgrepAvailable()){
        expect(!processesMatching(tag).isEmpty(),
               QStringLiteral("marker process is visible under the unique tag"));
    }
    expect(pumpUntil([&]{ return called; }, 10000),
           QStringLiteral("timeout outcome delivered"));
    const qint64 took = elapsed.elapsed();
    expect(outcome.status == LprStatus::Failed
               && outcome.note.contains(QStringLiteral("超时")),
           QStringLiteral("timeout produces Failed + 超时 note (note=%1, status=%2)")
               .arg(outcome.note).arg(static_cast<int>(outcome.status)));
    expect(took >= 350 && took < 5000,
           QStringLiteral("honors SMARTPARK_LPR_TIMEOUT_MS≈500ms (took %1ms)")
               .arg(took));
    // 关键回归：脚本本身、后台 marker 都不能剩（看门狗要杀整组而不是只 kill sh）。
    expect(waitForMarkerDead(pid, 5000),
           QStringLiteral("background marker process is gone after timeout "
                          "(pid=%1)")
               .arg(pid));
    if (pgrepAvailable()){
        expect(waitForNoProcess(tag, 5000),
               QStringLiteral("no process whose command line contains the unique "
                              "tag remains after timeout"));
    }
    // 超时必须释放工人槽：下一个请求还能正常成功。
    const QString successScript = writeScript(
        dir, QStringLiteral("success.sh"), QString::fromUtf8(kSuccessScript));
    warmUpScript(successScript);
    bool secondCalled = false;
    LprOutcome secondOutcome;
    recognizePlateAsync(QByteArrayLiteral("after-timeout"), successScript, &context,
                        [&](LprOutcome result){
                            secondCalled = true;
                            secondOutcome = result;
                        });
    expect(pumpUntil([&]{ return secondCalled; }, 8000),
           QStringLiteral("a later request completes after a timeout"));
    expect(secondOutcome.status == LprStatus::Ok,
           QStringLiteral("worker slot recovered after timeout (status=%1, note=%2)")
               .arg(static_cast<int>(secondOutcome.status)).arg(secondOutcome.note));
    killMatching(tag);
}

void testContextDestroyCancellation(){
    const QString dir = freshDir(QStringLiteral("cancel"));
    qputenv("SMARTPARK_LPR_TEST_DIR", dir.toUtf8());
    const QString successScript = writeScript(
        dir, QStringLiteral("success.sh"), QString::fromUtf8(kSuccessScript));
    warmUpScript(successScript);
    const QString tagA = uniqueTag(QStringLiteral("cancelA"));
    const QString tagB = uniqueTag(QStringLiteral("cancelB"));
    const QString markerA = dir + QStringLiteral("/a.started");
    const QString markerB = dir + QStringLiteral("/b.started");
    const QString hangA = writeScript(dir, QStringLiteral("hang-a.sh"),
                                      hangScriptBody(tagA, markerA));
    const QString hangB = writeScript(dir, QStringLiteral("hang-b.sh"),
                                      hangScriptBody(tagB, markerB));
    warmUpScript(hangA);
    warmUpScript(hangB);

    int cancelledCallbacks = 0;
    auto *first = new QObject;
    auto *second = new QObject;
    auto *queued = new QObject;
    recognizePlateAsync(QByteArrayLiteral("cancel-a"), hangA, first,
                        [&](LprOutcome){ ++cancelledCallbacks; });
    recognizePlateAsync(QByteArrayLiteral("cancel-b"), hangB, second,
                        [&](LprOutcome){ ++cancelledCallbacks; });
    // 两个工人槽都占住之后，第三个请求只能排队。
    const qint64 pidA = waitForMarker(markerA, 5000);
    const qint64 pidB = waitForMarker(markerB, 5000);
    expect(pidA > 0 && pidB > 0,
           QStringLiteral("both worker slots are busy before cancellation "
                          "(pidA=%1 pidB=%2)")
               .arg(pidA).arg(pidB));
    if (pgrepAvailable()){
        expect(!processesMatching(tagA).isEmpty()
                   && !processesMatching(tagB).isEmpty(),
               QStringLiteral("both running scripts are visible under their tags"));
    }
    recognizePlateAsync(QByteArrayLiteral("cancel-queued"), successScript, queued,
                        [&](LprOutcome){ ++cancelledCallbacks; });
    // context 销毁：排队任务丢弃、运行任务整组杀掉，且都不回调（在 500ms 超时
    // 之前销毁掉，免得超时回调干扰判断）。
    delete queued;
    delete first;
    delete second;
    expect(waitForMarkerDead(pidA, 5000) && waitForMarkerDead(pidB, 5000),
           QStringLiteral("running process trees are killed when the context is "
                          "destroyed"));
    if (pgrepAvailable()){
        expect(waitForNoProcess(tagA, 5000) && waitForNoProcess(tagB, 5000),
               QStringLiteral("no tagged process remains after context destruction"));
    }
    pumpFor(300);
    expect(cancelledCallbacks == 0,
           QStringLiteral("destroyed contexts never call back (callbacks=%1)")
               .arg(cancelledCallbacks));

    // 工人槽已恢复：两个新请求应该能并发跑完（配置并发上限 2）。
    QObject recoverContext;
    int recoveredOk = 0;
    int recoveredFailed = 0;
    QStringList recoverNotes;
    for (int i = 0; i < 2; ++i){
        recognizePlateAsync(
            QByteArrayLiteral("recover-") + QByteArray::number(i), successScript,
            &recoverContext, [&](LprOutcome outcome){
                recoverNotes << outcome.note;
                if (outcome.status == LprStatus::Ok){
                    ++recoveredOk;
                } else{
                    ++recoveredFailed;
                }
            });
    }
    expect(pumpUntil([&]{ return recoveredOk + recoveredFailed == 2; }, 8000),
           QStringLiteral("requests keep working after context destruction"));
    expect(recoveredOk == 2 && recoveredFailed == 0,
           QStringLiteral("worker slots recovered (ok=%1 failed=%2 notes=[%3])")
               .arg(recoveredOk).arg(recoveredFailed)
               .arg(recoverNotes.join(QStringLiteral("; "))));
    killMatching(tagA);
    killMatching(tagB);
}

void testSuccessPathSingleCallback(){
    const QString dir = freshDir(QStringLiteral("success"));
    const QString script = writeScript(
        dir, QStringLiteral("success.sh"), QString::fromUtf8(kSuccessScript));
    warmUpScript(script);
    QObject context;
    int calls = 0;
    LprOutcome outcome;
    recognizePlateAsync(QByteArrayLiteral("success-image"), script, &context,
                        [&](LprOutcome result){
                            ++calls;
                            outcome = result;
                        });
    expect(pumpUntil([&]{ return calls > 0; }, 8000),
           QStringLiteral("successful recognition calls back"));
    pumpFor(150);
    expect(calls == 1,
           QStringLiteral("successful recognition calls back exactly once (calls=%1)")
               .arg(calls));
    expect(outcome.status == LprStatus::Ok,
           QStringLiteral("successful recognition yields Ok"));
    expect(outcome.note == QStringLiteral("script"),
           QStringLiteral("Ok note keeps the script backend (note=%1)")
               .arg(outcome.note));
    expect(outcome.result.value(QStringLiteral("plate")).toString()
               == QStringLiteral("京A12345"),
           QStringLiteral("plate parsed from script JSON (plate=%1)")
               .arg(outcome.result.value(QStringLiteral("plate")).toString()));
    expect(std::fabs(outcome.result.value(QStringLiteral("confidence")).toDouble()
                     - 0.97) < 0.001,
           QStringLiteral("confidence parsed from script JSON (confidence=%1)")
               .arg(outcome.result.value(QStringLiteral("confidence")).toDouble()));
}

void testSyncTimeoutTreeCleanup(){
    const QString dir = freshDir(QStringLiteral("sync-timeout"));
    qputenv("SMARTPARK_LPR_TEST_DIR", dir.toUtf8());
    const QString tag = uniqueTag(QStringLiteral("synctimeout"));
    const QString markerFile = dir + QStringLiteral("/sync-marker.started");
    const QString script = writeScript(
        dir, QStringLiteral("hang-sync.sh"), hangScriptBody(tag, markerFile));
    warmUpScript(script);

    QElapsedTimer elapsed;
    elapsed.start();
    QString note;
    const QJsonObject result = recognizePlate(
        QByteArrayLiteral("sync-timeout-image"), script, &note);
    const qint64 took = elapsed.elapsed();
    expect(result.isEmpty(), QStringLiteral("sync timeout returns an empty result"));
    expect(note.contains(QStringLiteral("超时")),
           QStringLiteral("sync timeout note mentions 超时 (note=%1)").arg(note));
    expect(took >= 300 && took < 5000,
           QStringLiteral("sync timeout honors the configured ~500ms (took %1ms)")
               .arg(took));
    expect(QFileInfo::exists(markerFile),
           QStringLiteral("sync script did spawn its background marker before "
                          "timeout"));
    const qint64 pid = markerPid(markerFile);
    expect(pid > 0 && waitForMarkerDead(pid, 5000),
           QStringLiteral("sync timeout path kills the whole process tree "
                          "(marker pid=%1)").arg(pid));
    if (pgrepAvailable()){
        expect(waitForNoProcess(tag, 5000),
               QStringLiteral("no tagged process remains after sync timeout"));
    }
    killMatching(tag);
}

} // namespace

int main(int argc, char **argv){
    // 调度器参数在进程内第一次用到识别时读取一次：一次设定，全测试共用。
    qputenv("SMARTPARK_LPR_MAX_CONCURRENCY", "2");
    qputenv("SMARTPARK_LPR_QUEUE_LIMIT", "4");
    qputenv("SMARTPARK_LPR_TIMEOUT_MS", "500");

    QCoreApplication app(argc, argv);
    QTemporaryDir tempDir(QDir::tempPath()
                          + QStringLiteral("/smartpark-lpr-concurrency-XXXXXX"));
    if (!tempDir.isValid()){
        std::cerr << "FAILED: cannot create temporary directory\n";
        return 1;
    }
    g_tempDir = &tempDir;

    testConcurrencyCapAndQueueLimit();
    testTimeoutKillsProcessTree();
    testContextDestroyCancellation();
    testSuccessPathSingleCallback();
    testSyncTimeoutTreeCleanup();

    cleanupLeftovers();
    // 再给被杀的残留进程一点时间退出，避免影响下一次运行。
    pumpFor(100);
    cleanupLeftovers();

    if (g_failures == 0){
        std::cout << "ALL LPR CONCURRENCY TESTS PASSED\n";
        return 0;
    }
    std::cerr << g_failures << " LPR CONCURRENCY TEST(S) FAILED\n";
    return 1;
}
