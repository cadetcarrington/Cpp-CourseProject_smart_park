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
    connect(&faultAlarmTimer_, &QTimer::timeout, this, [this]{
        if (state_ == State::Opening && faultInjected_){
            log(QStringLiteral("告警：道闸抬杆受阻，已进入故障状态"));
        }
    });
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

void BarrierGate::requestOpen(){
    if (state_ == State::Closed){
        transition(State::Opening, QStringLiteral("放行，抬杆中"));
        faultAlarmTimer_.start(kFaultAlarmMs);
        openTimer_.start(kOpeningMs);
    } else if (state_ == State::Open){
        log(QStringLiteral("闸杆已在保持位"));
        holdTimer_.start(kHoldTimeoutMs);
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
        openTimer_.start(kOpeningMs);
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
    gate.reset();
    check(gate.state() == BarrierGate::State::Closed, "fault reset returns to closed");
    gate.setFault(false);

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

    std::cout << (failures == 0 ? "GATE SELFTEST: PASS" : "GATE SELFTEST: FAIL")
              << " (" << failures << " failure(s))\n";
    return failures == 0 ? 0 : 1;
}

} // namespace gate
} // namespace smartpark
