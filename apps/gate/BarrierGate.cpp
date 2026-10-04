#include "BarrierGate.h"

#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <iostream>

namespace smartpark{
namespace gate{
namespace{
constexpr int kOpeningMs = 700;
constexpr int kClosingMs = 700;
constexpr int kHoldTimeoutMs = 5000;
constexpr int kFaultAlarmMs = 4000;
} // namespace

BarrierGate::BarrierGate(QObject *parent)
    : QObject(parent){
    openTimer_.setSingleShot(true);
    closeTimer_.setSingleShot(true);
    holdTimer_.setSingleShot(true);
    faultAlarmTimer_.setSingleShot(true);

    connect(&openTimer_, &QTimer::timeout, this, [this]{
        if (state_ == State::Opening){
            if (faultInjected_){
                transition(State::Fault, QStringLiteral("抬杆超时未到位（电机故障），请现场检修"));
                // 进入故障后持续告警，直到 reset() 复位。
                faultAlarmTimer_.start();
                return;
            }
            transition(State::Open, QStringLiteral("闸杆到位，请通行"));
            holdTimer_.start(kHoldTimeoutMs);
        }
    });
    connect(&holdTimer_, &QTimer::timeout, this, [this]{
        if (state_ == State::Open){
            log(QStringLiteral("保持超时无车通过，自动落闸"));
            transition(State::Closing, QStringLiteral("落闸中"));
            closeTimer_.start(kClosingMs);
        }
    });
    connect(&closeTimer_, &QTimer::timeout, this, [this]{
        if (state_ == State::Closing){
            transition(State::Closed, QStringLiteral("闸杆已落"));
        }
    });
    // 故障持续告警：每 4s 重复一次，复位后停止。
    faultAlarmTimer_.setInterval(kFaultAlarmMs);
    connect(&faultAlarmTimer_, &QTimer::timeout, this, [this]{
        if (state_ == State::Fault){
            log(QStringLiteral("告警：道闸仍处于故障状态，请现场检修后执行 reset"));
            return;
        }
        faultAlarmTimer_.stop();
    });
}

QString BarrierGate::stateText(State state){
    switch (state){
    case State::Closed: return QStringLiteral("关闭");
    case State::Opening: return QStringLiteral("抬杆中");
    case State::Open: return QStringLiteral("保持");
    case State::Closing: return QStringLiteral("落闸中");
    case State::Fault: return QStringLiteral("故障");
    }
    return QStringLiteral("未知");
}

void BarrierGate::transition(State next, const QString &reason){
    state_ = next;
    log(reason);
    if (onStateChanged){
        onStateChanged(next);
    }
}

void BarrierGate::log(const QString &text){
    const QString stamped = QDateTime::currentDateTime().toString("HH:mm:ss")
        + " [道闸] " + text;
    if (onLog){
        onLog(stamped);
    }
}

void BarrierGate::startOpening(){
    openTimer_.start(kOpeningMs);
}

void BarrierGate::requestOpen(){
    switch (state_){
    case State::Closed:
        transition(State::Opening, QStringLiteral("放行，抬杆中"));
        startOpening();
        return;
    case State::Open:
        log(QStringLiteral("闸杆已在保持位"));
        holdTimer_.start(kHoldTimeoutMs);
        return;
    case State::Opening:
        log(QStringLiteral("闸杆正在抬起，无需重复放行"));
        return;
    case State::Closing:
        // 落闸途中又有一辆车被放行：必须立即反转抬杆，否则业务上已放行的
        // 车辆会被闸杆拦下。（原来这里静默忽略：界面显示"已放行"但杆不动。）
        closeTimer_.stop();
        transition(State::Opening, QStringLiteral("落闸途中收到放行，反转抬杆"));
        startOpening();
        return;
    case State::Fault:
        log(QStringLiteral("道闸处于故障状态，放行无效；请检修后执行 reset"));
        return;
    }
}

void BarrierGate::vehiclePassed(){
    if (state_ == State::Open){
        log(QStringLiteral("地感：车辆已通过"));
        holdTimer_.stop();
        transition(State::Closing, QStringLiteral("落闸中"));
        closeTimer_.start(kClosingMs);
    } else if (state_ == State::Closing){
        // 防砸：落闸遇车辆立即反转抬杆。
        ++antiSmashCount_;
        closeTimer_.stop();
        transition(State::Opening, QStringLiteral("防砸：落闸遇阻，反转抬杆"));
        startOpening();
    }
}

void BarrierGate::setFault(bool enabled){
    faultInjected_ = enabled;
    log(enabled ? QStringLiteral("故障注入：抬杆电机卡死")
                : QStringLiteral("故障解除"));
}

void BarrierGate::reset(){
    if (state_ == State::Fault){
        openTimer_.stop();
        faultAlarmTimer_.stop();
        transition(State::Closed, QStringLiteral("故障复位，闸杆关闭"));
    }
}

OfflineQueue::OfflineQueue(QString filePath)
    : filePath_(std::move(filePath)){
    QFile file(filePath_);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)){
        for (const QByteArray &line : file.readAll().split('\n')){
            if (!line.trimmed().isEmpty()) ++cachedCount_;
        }
    }
}

bool OfflineQueue::append(const QJsonObject &event){
    QFile file(filePath_);
    if (!file.open(QIODevice::Append | QIODevice::Text)) return false;
    QByteArray line = QJsonDocument(event).toJson(QJsonDocument::Compact);
    line.append('\n');
    if (file.write(line) != line.size() || !file.flush()) return false;
    file.close();
    if (file.error() != QFileDevice::NoError) return false;
    ++cachedCount_;
    return true;
}

QJsonArray OfflineQueue::pending(int limit) const{
    QJsonArray events;
    QFile file(filePath_);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return events;
    for (const QByteArray &line : file.readAll().split('\n')){
        if (line.trimmed().isEmpty()) continue;
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) return {};
        events.append(document.object());
        if (events.size() == limit) break;
    }
    return events;
}

bool OfflineQueue::acknowledge(int count){
    if (count < 1 || count > cachedCount_) return false;
    QFile source(filePath_);
    if (!source.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    const QList<QByteArray> lines = source.readAll().split('\n');
    source.close();
    QSaveFile target(filePath_);
    if (!target.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    int consumed = 0;
    for (const QByteArray &line : lines){
        if (line.trimmed().isEmpty()) continue;
        if (consumed < count){
            ++consumed;
            continue;
        }
        QByteArray output = line;
        output.append('\n');
        if (target.write(output) != output.size()) return false;
    }
    if (consumed != count || !target.commit()) return false;
    cachedCount_ -= count;
    return true;
}

int OfflineQueue::size() const{
    return cachedCount_;
}

int replayHandledPrefix(const QJsonObject &result, int submitted){
    if (submitted <= 0){
        return 0;
    }
    // 首选按逐条结论判定：results 与服务端处理顺序一致，只认「有结论」的前缀。
    int handled = 0;
    for (const QJsonValue &item : result.value(QStringLiteral("results")).toArray()){
        if (handled >= submitted){
            break;
        }
        if (!item.toObject().contains(QStringLiteral("ok"))){
            break;   // 从这里开始没有结论，它及其后全部保留待重报
        }
        ++handled;
    }
    if (handled > 0){
        return handled;
    }
    // 兼容没有 results 的旧响应：三个计数之和等于提交条数即视为整批已处理。
    // skipped 与 applied/duplicate 同等对待——被拒事件同样已有结论，不能堵住队列。
    const int applied = result.value(QStringLiteral("applied")).toInt();
    const int duplicate = result.value(QStringLiteral("duplicate")).toInt();
    const int skipped = result.value(QStringLiteral("skipped")).toInt();
    return applied + duplicate + skipped == submitted ? submitted : 0;
}

bool replayBatchHandled(const QJsonObject &result, int submitted){
    return submitted > 0 && replayHandledPrefix(result, submitted) == submitted;
}

QStringList replaySkippedDetails(const QJsonObject &result, int limit){
    QStringList details;
    int seen = 0;
    for (const QJsonValue &item : result.value(QStringLiteral("results")).toArray()){
        if (limit >= 0 && seen >= limit){
            break;
        }
        ++seen;
        const QJsonObject entry = item.toObject();
        if (entry.value(QStringLiteral("ok")).toBool()){
            continue;
        }
        details << QStringLiteral("%1 %2：%3")
                       .arg(entry.value(QStringLiteral("plate")).toString(),
                            entry.value(QStringLiteral("kind")).toString(),
                            entry.value(QStringLiteral("error")).toString());
    }
    return details;
}

namespace{
bool waitFor(const std::function<bool()> &condition, int timeoutMs){
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QTimer poller;
    poller.setInterval(20);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&poller, &QTimer::timeout, &loop, &QEventLoop::quit);
    deadline.start(timeoutMs);
    poller.start();
    while (!condition() && deadline.isActive()){
        loop.exec();
    }
    poller.stop();
    return condition();
}
} // namespace

int runSelftest(){
    int failures = 0;
    auto check = [&failures](bool condition, const QString &what){
        std::cout << (condition ? "  ok  " : "  FAIL ")
                  << what.toStdString() << '\n';
        if (!condition){
            ++failures;
        }
    };

    // 1. 正常放行周期：抬杆 → 保持 → 车辆通过 → 落闸 → 关闭。
    BarrierGate gate;
    auto observed = BarrierGate::State::Closed;
    gate.onStateChanged = [&observed](BarrierGate::State state){ observed = state; };
    gate.requestOpen();
    check(waitFor([&]{ return gate.state() == BarrierGate::State::Open; }, 1500),
          "barrier opens on demand");
    gate.vehiclePassed();
    check(waitFor([&]{ return gate.state() == BarrierGate::State::Closed; }, 1500),
          "barrier closes after vehicle passed");

    // 2. 防砸：落闸期间检测到车辆立即反转抬杆。
    gate.requestOpen();
    waitFor([&]{ return gate.state() == BarrierGate::State::Open; }, 1500);
    gate.vehiclePassed();
    gate.vehiclePassed();   // 落闸途中再次地感 → 防砸反转
    check(gate.antiSmashCount() == 1 && gate.state() == BarrierGate::State::Opening,
          "anti-smash reopens while closing");
    check(waitFor([&]{ return gate.state() == BarrierGate::State::Open; }, 1500),
          "barrier reopens after anti-smash");
    gate.vehiclePassed();
    check(waitFor([&]{ return gate.state() == BarrierGate::State::Closed; }, 1500),
          "barrier closes after anti-smash cycle");

    // 3. 故障注入与复位。
    gate.setFault(true);
    gate.requestOpen();
    check(waitFor([&]{ return gate.state() == BarrierGate::State::Fault; }, 1500),
          "fault injection enters fault state");
    check(gate.stateText(gate.state()) == QStringLiteral("故障"),
          "fault state has a readable name");
    gate.reset();
    check(gate.state() == BarrierGate::State::Closed, "fault reset returns to closed");
    gate.setFault(false);

    // 3b. 落闸途中再次放行：必须立即反转抬杆，不能静默忽略。
    gate.requestOpen();
    waitFor([&]{ return gate.state() == BarrierGate::State::Open; }, 1500);
    gate.vehiclePassed();                       // 进入 Closing
    check(gate.state() == BarrierGate::State::Closing, "barrier is closing");
    gate.requestOpen();                         // 又来一辆已放行的车
    check(gate.state() == BarrierGate::State::Opening,
          "releasing while closing reverses to opening");
    check(waitFor([&]{ return gate.state() == BarrierGate::State::Open; }, 1500),
          "barrier reaches open after reversal");
    check(gate.antiSmashCount() == 1,
          "business reversal is not counted as anti-smash");
    gate.vehiclePassed();
    waitFor([&]{ return gate.state() == BarrierGate::State::Closed; }, 1500);

    // 3c. 故障状态下放行不生效（闸杆不会动），复位后可正常放行。
    gate.setFault(true);
    gate.requestOpen();
    waitFor([&]{ return gate.state() == BarrierGate::State::Fault; }, 1500);
    gate.requestOpen();
    check(gate.state() == BarrierGate::State::Fault,
          "release request is ignored while faulted");
    gate.reset();
    gate.setFault(false);
    gate.requestOpen();
    check(waitFor([&]{ return gate.state() == BarrierGate::State::Open; }, 1500),
          "release works again after fault reset");
    gate.vehiclePassed();
    waitFor([&]{ return gate.state() == BarrierGate::State::Closed; }, 1500);

    // 4. 离线队列：追加 → 计数 → 取出清空。
    QTemporaryDir directory;
    OfflineQueue queue(directory.filePath("queue.jsonl"));
    QJsonObject first{{QStringLiteral("kind"), QStringLiteral("enter")},
                      {QStringLiteral("plate"), QStringLiteral("晋G00001")}};
    QJsonObject second{{QStringLiteral("kind"), QStringLiteral("exit")},
                       {QStringLiteral("plate"), QStringLiteral("晋G00001")}};
    check(queue.append(first) && queue.append(second), "offline queue persists events");
    check(queue.size() == 2, "offline queue counts cached events");
    const QJsonArray taken = queue.pending();
    check(taken.size() == 2 && taken.at(0).toObject().value(
               QStringLiteral("plate")).toString() == QStringLiteral("晋G00001"),
          "offline queue replays events in order");
    check(queue.size() == 2 && OfflineQueue(directory.filePath("queue.jsonl")).size() == 2,
          "unconfirmed replay survives restart");
    check(queue.acknowledge(1), "acknowledged prefix clears");
    check(queue.size() == 1 && queue.pending().size() == 1,
          "unconfirmed suffix survives acknowledgement");
    check(queue.acknowledge(1), "remaining event clears");
    check(queue.size() == 0, "offline queue clears after replay");

    // 5. 补报确认判定：skipped 必须计入，否则队头一条被拒事件会永久堵死队列。
    const QJsonObject allApplied{{QStringLiteral("applied"), 3},
                                 {QStringLiteral("duplicate"), 0},
                                 {QStringLiteral("skipped"), 0}};
    check(replayBatchHandled(allApplied, 3),
          "a fully applied batch is acknowledged");

    const QJsonObject withDuplicate{{QStringLiteral("applied"), 1},
                                    {QStringLiteral("duplicate"), 2},
                                    {QStringLiteral("skipped"), 0}};
    check(replayBatchHandled(withDuplicate, 3),
          "duplicates count towards completion");

    // 关键回归：一条永远无法追溯的事件（例如「已在场」）也必须让整批出队，
    // 否则队列永远排不空，后面所有离线事件都堵在它后面。
    const QJsonObject withSkipped{
        {QStringLiteral("applied"), 1},
        {QStringLiteral("duplicate"), 1},
        {QStringLiteral("skipped"), 1},
        {QStringLiteral("results"),
         QJsonArray{
             QJsonObject{{QStringLiteral("plate"), QStringLiteral("晋G00001")},
                         {QStringLiteral("kind"), QStringLiteral("enter")},
                         {QStringLiteral("ok"), true}},
             QJsonObject{{QStringLiteral("plate"), QStringLiteral("晋G00002")},
                         {QStringLiteral("kind"), QStringLiteral("enter")},
                         {QStringLiteral("ok"), true},
                         {QStringLiteral("duplicate"), true}},
             QJsonObject{{QStringLiteral("plate"), QStringLiteral("晋G00003")},
                         {QStringLiteral("kind"), QStringLiteral("exit")},
                         {QStringLiteral("ok"), false},
                         {QStringLiteral("error"),
                          QStringLiteral("追溯离场失败（可能不在场）")}},
         }}};
    check(replayBatchHandled(withSkipped, 3),
          "a batch with a permanently rejected event is still acknowledged");
    const QStringList dropped = replaySkippedDetails(withSkipped, -1);
    check(dropped.size() == 1
              && dropped.first().contains(QStringLiteral("晋G00003"))
              && dropped.first().contains(QStringLiteral("追溯离场失败")),
          "rejected events are reported with plate and reason");

    check(!replayBatchHandled(withSkipped, 5),
          "an incomplete response keeps the batch queued");
    check(!replayBatchHandled(QJsonObject{}, 1),
          "an empty response keeps the batch queued");

    // 6. 部分确认：只出队服务端已给出结论的前缀，其余留在本地重报。
    const QJsonObject truncated{
        {QStringLiteral("applied"), 2},
        {QStringLiteral("duplicate"), 0},
        {QStringLiteral("skipped"), 0},
        {QStringLiteral("results"),
         QJsonArray{
             QJsonObject{{QStringLiteral("plate"), QStringLiteral("晋G00001")},
                         {QStringLiteral("kind"), QStringLiteral("enter")},
                         {QStringLiteral("ok"), true}},
             QJsonObject{{QStringLiteral("plate"), QStringLiteral("晋G00002")},
                         {QStringLiteral("kind"), QStringLiteral("enter")},
                         {QStringLiteral("ok"), true}},
         }}};
    check(replayHandledPrefix(truncated, 4) == 2,
          "a truncated response only acknowledges the confirmed prefix");
    check(!replayBatchHandled(truncated, 4),
          "a truncated response does not acknowledge the whole batch");

    // 前缀在第一条就断掉：一条都不能出队。
    const QJsonObject cutAtHead{
        {QStringLiteral("results"),
         QJsonArray{QJsonObject{{QStringLiteral("plate"), QStringLiteral("晋G00003")},
                                {QStringLiteral("kind"), QStringLiteral("enter")}}}}};
    check(replayHandledPrefix(cutAtHead, 3) == 0,
          "a response without per-event verdicts acknowledges nothing");

    // 旧服务端不返回 results 时，退回按计数之和判定。
    const QJsonObject legacy{{QStringLiteral("applied"), 1},
                             {QStringLiteral("duplicate"), 1},
                             {QStringLiteral("skipped"), 1}};
    check(replayHandledPrefix(legacy, 3) == 3,
          "a legacy response without results falls back to the counters");
    check(replayHandledPrefix(legacy, 4) == 0,
          "legacy counters that do not add up acknowledge nothing");

    // 被丢弃的明细只覆盖已确认前缀，不把未处理的条目也算进来。
    const QJsonObject mixed{
        {QStringLiteral("results"),
         QJsonArray{
             QJsonObject{{QStringLiteral("plate"), QStringLiteral("晋G00004")},
                         {QStringLiteral("kind"), QStringLiteral("exit")},
                         {QStringLiteral("ok"), false},
                         {QStringLiteral("error"), QStringLiteral("追溯离场失败")}},
             QJsonObject{{QStringLiteral("plate"), QStringLiteral("晋G00005")},
                         {QStringLiteral("kind"), QStringLiteral("exit")}},
         }}};
    check(replaySkippedDetails(mixed, 1).size() == 1
              && replaySkippedDetails(mixed, 1).first().contains(
                     QStringLiteral("晋G00004")),
          "skipped details only cover the acknowledged prefix");

    std::cout << (failures == 0 ? "GATE SELFTEST: PASS" : "GATE SELFTEST: FAIL")
              << " (" << failures << " failure(s))\n";
    return failures == 0 ? 0 : 1;
}

} // namespace gate
} // namespace smartpark
