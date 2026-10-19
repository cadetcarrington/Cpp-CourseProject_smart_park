#include "network/PlateRecognition.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QPointer>
#include <QProcess>
#include <QQueue>
#include <QStringList>
#include <QTemporaryFile>
#include <QThread>
#include <QTimer>

#include <functional>
#include <memory>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#endif

namespace smartpark::network{

QString mockPlateFromImage(const QByteArray &imageBytes){
    static const char *kProvinces[] = {
        "京", "沪", "粤", "苏", "浙", "皖", "鲁", "豫", "鄂", "湘"};
    static const char *kLetters = "ABCDEFGHJKLMNPQRSTUVWXYZ";
    static const char *kDigits = "0123456789ABCDEFGHJKMNPQRSTUVWXYZ";
    const QByteArray digest =
        QCryptographicHash::hash(imageBytes, QCryptographicHash::Sha256);
    QString plate;
    plate += kProvinces[static_cast<quint8>(digest.at(0)) % 10];
    plate += kLetters[static_cast<quint8>(digest.at(1)) % 24];
    for (int i = 2; i < 7; ++i){
        plate += kDigits[static_cast<quint8>(digest.at(i)) % 33];
    }
    return plate;
}

namespace{

// 把识别脚本的输出收成一句话：优先第一行非空文本，限长，便于放进用户可见的
// message 里（原话也能帮排障）。
QString firstLineOf(const QByteArray &text){
    const QString decoded = QString::fromUtf8(text).trimmed();
    const QStringList lines = decoded.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.isEmpty()){
        return {};
    }
    QString line = lines.first().trimmed();
    if (line.size() > 160){
        line = line.left(160) + QStringLiteral("…");
    }
    return line;
}

bool looksLikeNoPlate(const QString &text){
    const QString lowered = text.toLower();
    return lowered.contains(QStringLiteral("no license plate"))
        || lowered.contains(QStringLiteral("no plate"))
        || lowered.contains(QStringLiteral("未识别到车牌"))
        || lowered.contains(QStringLiteral("未检测到车牌"));
}

// 命令模板：默认按 %1 替换成图片路径；没写 %1 的（测试替身、固定输出脚本）
// 就简单追加路径，别让 QString::arg 在日志里刷 "Argument missing" 警告。
QString buildCommand(const QString &lprCommand, const QString &imagePath){
    if (lprCommand.contains(QLatin1String("%1"))){
        return QString(lprCommand).arg(imagePath);
    }
    return lprCommand + QLatin1Char(' ') + imagePath;
}

// 把图片写成临时文件供脚本读取；失败返回空字符串。
QString writeImageFile(const QByteArray &imageBytes, QObject *owner){
    auto *imageFile = new QTemporaryFile(
        QDir::tempPath() + QStringLiteral("/smartpark-lpr-XXXXXX.jpg"), owner);
    imageFile->setAutoRemove(true);
    if (!imageFile->open()){
        delete imageFile;
        return {};
    }
    imageFile->write(imageBytes);
    imageFile->close();
    return imageFile->fileName();
}

} // namespace

namespace{
QJsonObject mockResult(const QByteArray &imageBytes){
    return QJsonObject{{QStringLiteral("plate"), mockPlateFromImage(imageBytes)},
                       {QStringLiteral("confidence"), 0.87},
                       {QStringLiteral("source"), QStringLiteral("mock")}};
}
} // namespace

namespace{

// ---------------------------------------------------------------------------
// 识别调度：有限的工人槽 + 有界排队 + 整棵进程树清理
//
// 背景（代码评审）：早先每来一个识别请求就 new 一个 QProcess，16 个并发请求会
// 同时拉起 16 套 python/Paddle OCR，内存和 CPU 直接打满；看门狗里的 kill() 只
// 杀 /bin/sh，recognize_plate.py 自己 Popen 出来的 Paddle 进程会活下来继续吃
// CPU。这里用一个进程内单例调度器：
//   * 最多同时跑 maxConcurrency 个脚本（默认 2），多出来的进 FIFO 队列
//     （默认最多 16 个）；队列也满了就立刻异步失败，不占资源；
//   * 每个脚本进程 setsid() 自成会话/进程组，超时/取消/退出时向 -pid 发
//     SIGTERM，宽限后补 SIGKILL，再 waitForFinished 回收直系子进程；
//   * context（调用方 QObject）销毁时排队任务直接丢弃、运行中的整组杀掉，
//     按约定不再回调。
// 环境变量在进程内第一次用到识别时读取一次（测试据此把超时缩短）：
//   SMARTPARK_LPR_MAX_CONCURRENCY（默认 2）
//   SMARTPARK_LPR_QUEUE_LIMIT（默认 16）
//   SMARTPARK_LPR_TIMEOUT_MS（默认 60000）
// ---------------------------------------------------------------------------

struct LprSettings{
    int maxConcurrency{2};
    int queueLimit{16};
    int timeoutMs{60000};
};

int positiveEnvInt(const char *name, int fallback, int maximum){
    bool ok = false;
    const int value = qEnvironmentVariableIntValue(name, &ok);
    if (!ok || value <= 0){
        return fallback;
    }
    return qMin(value, maximum);
}

LprSettings readLprSettings(){
    LprSettings settings;
    // 单次识别在 s1 上约 13 秒、很吃内存；默认只放 2 个工人，别把机器打满。
    settings.maxConcurrency =
        positiveEnvInt("SMARTPARK_LPR_MAX_CONCURRENCY", 2, 64);
    settings.queueLimit = positiveEnvInt("SMARTPARK_LPR_QUEUE_LIMIT", 16, 4096);
    settings.timeoutMs = positiveEnvInt("SMARTPARK_LPR_TIMEOUT_MS", 60000,
                                        24 * 60 * 60 * 1000);
    return settings;
}

// 进程内第一次用到识别时读取一次，同步/异步路径共用同一份配置。
const LprSettings &lprSettings(){
    static const LprSettings settings = readLprSettings();
    return settings;
}

constexpr int kKillGraceMs = 500;   // SIGTERM 后等这么久再补 SIGKILL
constexpr int kReapTimeoutMs = 2000;

LprOutcome failedOutcome(const QString &note, const QString &detail = QString()){
    LprOutcome outcome;
    outcome.status = LprStatus::Failed;
    outcome.note = note;
    outcome.detail = detail;
    return outcome;
}

QString timeoutNote(int timeoutMs){
    // 保留「识别脚本超时」的说法，调用方拼的 "识别失败：…" 文案不必改。
    if (timeoutMs >= 1000 && timeoutMs % 1000 == 0){
        return QStringLiteral("识别脚本超时（%1 秒）").arg(timeoutMs / 1000);
    }
    return QStringLiteral("识别脚本超时（%1 毫秒）").arg(timeoutMs);
}

#ifdef Q_OS_UNIX
void signalProcessGroup(qint64 pid, int signalNumber){
    if (pid <= 0){
        return;
    }
    // 负 pid = 整个进程组。setsid() 之后 pgid == 子进程 pid。
    ::kill(static_cast<pid_t>(-pid), signalNumber);
}

bool processGroupAlive(qint64 pid){
    return pid > 0 && ::kill(static_cast<pid_t>(-pid), 0) == 0;
}
#endif

// 结束 QProcess 及其整棵进程树，并 waitForFinished 回收直系子进程（同步路径
// 也复用这一份逻辑）。
void terminateProcessTree(QProcess *process, int graceMs){
    if (process == nullptr){
        return;
    }
    const qint64 pid = process->processId();
#ifdef Q_OS_UNIX
    if (pid > 0){
        signalProcessGroup(pid, SIGTERM);
        if (!process->waitForFinished(graceMs)){
            signalProcessGroup(pid, SIGKILL);
            process->waitForFinished(kReapTimeoutMs);
        }
        // 直系子进程回收后，组里可能还有脚本后台化/Popen 出来的孙子进程
        // （Paddle OCR、测试里那个 sleep 300）。它们已被 SIGTERM/SIGKILL 打死，
        // 这里只做有限等待，确认进程组里已经没有被杀剩的成员。
        if (processGroupAlive(pid)){
            signalProcessGroup(pid, SIGKILL);
            for (int i = 0; i < 100 && processGroupAlive(pid); ++i){
                QThread::msleep(10);
            }
        }
        return;
    }
#else
    Q_UNUSED(graceMs);
#endif
    // 还没拿到 pid（没启动或已退出）或非 Unix：退回 QProcess 自己的终止方式。
    process->kill();
    process->waitForFinished(kReapTimeoutMs);
}

class LprScheduler : public QObject{
public:
    explicit LprScheduler(const LprSettings &settings)
        : settings_(settings){}

    ~LprScheduler() override{ shutdown(); }

    // 必须在调度器所属（事件循环）线程调用；context 与 done 的约定同
    // recognizePlateAsync：done 至多回调一次，context 先销毁则不回调。
    void submit(const QByteArray &imageBytes, const QString &lprCommand,
                QObject *context, std::function<void(LprOutcome)> done){
        if (context == nullptr || !done){
            return;
        }
        if (pending_.size() >= settings_.queueLimit){
            // 队列满：立刻异步失败（不建临时文件、不占工人槽）。用 singleShot
            // 而不是直接回调，保证「submit 不内联回调」的既有约定。
            failAsync(context, std::move(done), failedOutcome(
                QStringLiteral("识别任务排队已满，请稍后重试")));
            return;
        }
        auto *task = new Task(this);
        task->context = context;
        task->imageBytes = imageBytes;
        task->lprCommand = lprCommand;
        task->done = std::move(done);
        // context 销毁 → 排队任务丢弃 / 运行任务整组杀掉，不回调。
        task->contextConnection = QObject::connect(
            context, &QObject::destroyed, this, [this, task]{
                cancelTask(task);
            });
        pending_.enqueue(task);
        startNext();
    }

    // 进程退出时（qAddPostRoutine）把还在跑的识别树一起收掉：脚本 setsid 后
    // 自成会话，父进程退出不会自动带走它们，必须显式整组清理。
    void shutdown(){
        if (shuttingDown_){
            return;
        }
        shuttingDown_ = true;
        while (!pending_.isEmpty()){
            Task *task = pending_.dequeue();
            QObject::disconnect(task->contextConnection);
            task->finished = true;
            delete task;   // 排队任务没有进程/临时文件
        }
        const QList<Task *> running = running_;
        running_.clear();
        for (Task *task : running){
            QObject::disconnect(task->contextConnection);
            task->finished = true;
            if (task->watchdog != nullptr){
                task->watchdog->stop();
                task->watchdog = nullptr;
            }
            if (task->process != nullptr){
                QObject::disconnect(task->process, nullptr, this, nullptr);
                terminateProcessTree(task->process, kKillGraceMs);
                delete task->process;
                task->process = nullptr;
            }
            task->done = nullptr;
            delete task;
        }
    }

private:
    class Task : public QObject{
    public:
        explicit Task(QObject *parent) : QObject(parent){}
        QPointer<QObject> context;
        QMetaObject::Connection contextConnection;
        QByteArray imageBytes;
        QString lprCommand;
        std::function<void(LprOutcome)> done;
        QProcess *process{nullptr};
        QTimer *watchdog{nullptr};
        bool finished{false};   // 结果已定（已回调或按约定不再回调）
    };

    void startNext(){
        if (pumping_ || shuttingDown_){
            return;
        }
        pumping_ = true;
        while (!pending_.isEmpty()
               && running_.size() < settings_.maxConcurrency){
            startTask(pending_.dequeue());
        }
        pumping_ = false;
    }

    void startTask(Task *task){
        if (task->context.isNull()){
            cancelTask(task);   // 排队期间 context 已销毁
            return;
        }
        // 临时图片挂在 QProcess 下：进程销毁时一起删，超时/取消/正常结束都
        // 不会在 /tmp 里攒临时文件。
        auto *process = new QProcess(this);
        const QString imagePath = writeImageFile(task->imageBytes, process);
        if (imagePath.isEmpty()){
            QObject::disconnect(task->contextConnection);
            process->deleteLater();
            QPointer<QObject> context = task->context;
            std::function<void(LprOutcome)> done = std::move(task->done);
            task->done = nullptr;
            task->finished = true;
            task->deleteLater();
            failAsync(context, std::move(done),
                      failedOutcome(QStringLiteral("临时文件创建失败")));
            return;
        }
        task->imageBytes = QByteArray();   // 图片已落盘，尽快释放排队时占的内存
        task->process = process;
        running_.append(task);
#ifdef Q_OS_UNIX
        // 关键：让 /bin/sh 自成一个会话/进程组（pgid == 子进程 pid）。超时或
        // 取消时向 -pid 发信号，才能把 recognize_plate.py 里再 Popen 的 Paddle
        // OCR 一起带走；只 kill 直系子进程会留下孤儿继续吃 CPU。
        process->setChildProcessModifier([]{ ::setsid(); });
#endif
        auto *watchdog = new QTimer(process);
        watchdog->setSingleShot(true);
        watchdog->setInterval(settings_.timeoutMs);
        task->watchdog = watchdog;
        const int timeoutMs = settings_.timeoutMs;
        QObject::connect(watchdog, &QTimer::timeout, this, [this, task, timeoutMs]{
            if (task->finished){
                return;
            }
            // 先摘掉 process 的信号再阻塞杀树：waitForFinished() 内部会同步触发
            // finished 信号，不摘会重入 finishTask 造成双回调。
            QProcess *process = task->process;
            if (process != nullptr){
                QObject::disconnect(process, nullptr, this, nullptr);
            }
            if (task->watchdog != nullptr){
                task->watchdog->stop();
            }
            terminateProcessTree(process, kKillGraceMs);
            finishTask(task, failedOutcome(timeoutNote(timeoutMs)));
        });
        QObject::connect(process, &QProcess::errorOccurred, this,
                         [this, task](QProcess::ProcessError error){
            if (error != QProcess::FailedToStart || task->finished){
                return;   // 其它错误交给 finished 处理
            }
            const QString detail = task->process != nullptr
                ? task->process->errorString() : QString();
            finishTask(task, failedOutcome(
                QStringLiteral("识别脚本无法启动"), detail));
        });
        QObject::connect(
            process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, task](int exitCode, QProcess::ExitStatus){
            if (task->finished || task->process == nullptr){
                return;
            }
            finishTask(task, classifyLprOutput(
                exitCode, task->process->readAllStandardOutput(),
                task->process->readAllStandardError()));
        });
        process->start(QStringLiteral("/bin/sh"),
                       QStringList{QStringLiteral("-c"),
                                   buildCommand(task->lprCommand, imagePath)});
        if (task->finished){
            // 极端情况：start() 同步报 FailedToStart，finishTask 已经接管
            // （task/process 都走 deleteLater，这里安全返回）。
            return;
        }
        watchdog->start();
    }

    void finishTask(Task *task, const LprOutcome &outcome){
        if (task->finished){
            return;
        }
        task->finished = true;
        QObject::disconnect(task->contextConnection);
        pending_.removeOne(task);
        running_.removeOne(task);
        if (task->watchdog != nullptr){
            task->watchdog->stop();
            task->watchdog->deleteLater();
            task->watchdog = nullptr;
        }
        if (task->process != nullptr){
            QObject::disconnect(task->process, nullptr, this, nullptr);
            task->process->deleteLater();   // 临时图片是它的子对象，一起删
            task->process = nullptr;
        }
        QPointer<QObject> context = task->context;
        std::function<void(LprOutcome)> done = std::move(task->done);
        task->done = nullptr;
        task->deleteLater();
        // 清理完再回调：回调里重新提交识别、或销毁 context，都不会踩到这块状态。
        if (!context.isNull() && done){
            done(outcome);
        }
        startNext();
    }

    void cancelTask(Task *task){
        if (task == nullptr || task->finished){
            return;
        }
        task->finished = true;
        QObject::disconnect(task->contextConnection);
        const bool wasRunning = running_.removeOne(task);
        pending_.removeOne(task);
        if (task->watchdog != nullptr){
            task->watchdog->stop();
            task->watchdog->deleteLater();
            task->watchdog = nullptr;
        }
        QProcess *process = task->process;
        task->process = nullptr;
        if (process != nullptr){
            QObject::disconnect(process, nullptr, this, nullptr);
            terminateProcessTree(process, kKillGraceMs);
            process->deleteLater();
        }
        task->done = nullptr;   // context 已销毁：按约定不再回调
        task->deleteLater();
        if (wasRunning){
            startNext();   // 工人槽已释放，队列里的下一个顶上
        }
    }

    // 队列满/临时文件失败等即时失败：异步回调一次，绝不内联回调。
    static void failAsync(QObject *context, std::function<void(LprOutcome)> done,
                          const LprOutcome &outcome){
        if (context == nullptr || !done){
            return;
        }
        QTimer::singleShot(
            0, context, [done = std::move(done), outcome]() mutable {
                done(outcome);
            });
    }

    LprSettings settings_;
    QQueue<Task *> pending_;
    QList<Task *> running_;
    bool pumping_{false};       // startNext 防重入
    bool shuttingDown_{false};
};

// 进程级单例：首次使用时创建（此时事件循环已存在），不随静态析构顺序被提前删。
LprScheduler *g_lprScheduler = nullptr;

LprScheduler &lprScheduler(){
    if (g_lprScheduler == nullptr){
        g_lprScheduler = new LprScheduler(lprSettings());
        qAddPostRoutine([]{
            delete g_lprScheduler;
            g_lprScheduler = nullptr;
        });
    }
    return *g_lprScheduler;
}

} // namespace

LprOutcome classifyLprOutput(int exitCode, const QByteArray &stdoutBytes,
                             const QByteArray &stderrBytes){
    LprOutcome outcome;
    QJsonParseError parseError{};
    const QJsonDocument document =
        QJsonDocument::fromJson(stdoutBytes, &parseError);
    const QString scriptPlate =
        document.object().value(QStringLiteral("plate")).toString();
    if (parseError.error == QJsonParseError::NoError && !scriptPlate.isEmpty()){
        outcome.status = LprStatus::Ok;
        outcome.result = document.object();
        // recognize_plate.py 的键是 recognition_confidence，统一补 confidence 供前端展示。
        if (!outcome.result.contains(QStringLiteral("confidence"))
            && outcome.result.contains(QStringLiteral("recognition_confidence"))){
            outcome.result.insert(
                QStringLiteral("confidence"),
                outcome.result.value(QStringLiteral("recognition_confidence")).toDouble());
        }
        outcome.note = QStringLiteral("script");
        return outcome;
    }
    // 没解析出车牌：脚本可能只是「画面里没有车牌」，也可能是真出错。
    // 两种情况给客户端的说法完全不同，所以分开归类（网页端据此提示换照片）。
    const QString stdoutText = QString::fromUtf8(stdoutBytes).trimmed();
    const QString stderrText = QString::fromUtf8(stderrBytes).trimmed();
    outcome.detail = firstLineOf(stdoutBytes);
    if (outcome.detail.isEmpty()){
        outcome.detail = firstLineOf(stderrBytes);
    }
    if (looksLikeNoPlate(stdoutText) || looksLikeNoPlate(stderrText)){
        outcome.status = LprStatus::NoPlate;
        outcome.note = QStringLiteral("未识别到车牌");
        return outcome;
    }
    outcome.status = LprStatus::Failed;
    outcome.note = QStringLiteral("识别脚本未返回可用结果");
    if (outcome.detail.isEmpty() && exitCode != 0){
        outcome.note += QStringLiteral("（退出码 %1）").arg(exitCode);
    }
    return outcome;
}

QJsonObject recognizePlate(const QByteArray &imageBytes,
                           const QString &lprCommand, QString *note){
    if (lprCommand.isEmpty()){
        if (note != nullptr){
            *note = QStringLiteral("mock");
        }
        return mockResult(imageBytes);
    }
    // 脚本桥接：写临时图片 -> 执行命令模板（%1 = 图片路径）-> 解析 stdout JSON。
    QTemporaryFile imageFile(QDir::tempPath()
                             + QStringLiteral("/smartpark-lpr-XXXXXX.jpg"));
    imageFile.setAutoRemove(true);
    if (!imageFile.open()){
        if (note != nullptr){
            *note = QStringLiteral("临时文件创建失败");
        }
        return {};
    }
    imageFile.write(imageBytes);
    imageFile.close();
    QProcess process;
#ifdef Q_OS_UNIX
    // 与异步路径一致：脚本自成进程组，超时能把整棵树一起带走。
    process.setChildProcessModifier([]{ ::setsid(); });
#endif
    process.start(QStringLiteral("/bin/sh"),
                  QStringList{QStringLiteral("-c"),
                              buildCommand(lprCommand, imageFile.fileName())});
    const int timeoutMs = lprSettings().timeoutMs;
    if (!process.waitForStarted(3000)){
        terminateProcessTree(&process, kKillGraceMs);
        if (note != nullptr){
            *note = QStringLiteral("识别脚本启动失败或超时");
        }
        return {};
    }
    if (!process.waitForFinished(timeoutMs)){
        // 超时：整组杀干净并回收直系子进程，别留下孤儿 Paddle 进程。
        terminateProcessTree(&process, kKillGraceMs);
        if (note != nullptr){
            *note = QStringLiteral("识别脚本启动失败或超时");
        }
        return {};
    }
    const LprOutcome outcome = classifyLprOutput(
        process.exitCode(), process.readAllStandardOutput(),
        process.readAllStandardError());
    if (note != nullptr){
        *note = outcome.note;
    }
    return outcome.result;
}

void recognizePlateAsync(const QByteArray &imageBytes, const QString &lprCommand,
                         QObject *context, std::function<void(LprOutcome)> done){
    if (context == nullptr || !done){
        return;
    }
    // mock：没有脚本可跑，但保持「异步回调」的约定，调用方只写一条路径。
    if (lprCommand.isEmpty()){
        QTimer::singleShot(0, context, [imageBytes, done]{
            LprOutcome outcome;
            outcome.status = LprStatus::Ok;
            outcome.result = mockResult(imageBytes);
            outcome.note = QStringLiteral("mock");
            done(outcome);
        });
        return;
    }
    // 服务端一律走调度器：工人数/排队上限/超时都可配，进程树清理有保证。
    lprScheduler().submit(imageBytes, lprCommand, context, std::move(done));
}

} // namespace smartpark::network
