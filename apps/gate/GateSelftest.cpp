#include "GateSelftest.h"

#include "BarrierGate.h"
#include "GateClock.h"
#include "OfflineQueue.h"

#include <QString>
#include <QTemporaryDir>

#include <string>

#include <iostream>

namespace smartpark{
namespace gate{

int runSelftest(){
    int failures = 0;
    auto check = [&failures](bool condition, const std::string &what){
        std::cout << (condition ? "  ok  " : "  FAIL ") << what << '\n';
        if (!condition){
            ++failures;
        }
    };

    // 1. 正常放行周期：抬杆 → 保持 → 车辆通过 → 落闸 → 关闭。
    //    用虚拟时钟推进，不再真实等待。
    VirtualGateClock clock;
    BarrierGate gate(clock);
    auto observed = BarrierGate::State::Closed;
    gate.onStateChanged = [&observed](BarrierGate::State state){ observed = state; };

    gate.requestOpen();
    check(gate.state() == BarrierGate::State::Opening, "放行后立即进入抬杆中");
    clock.advance(std::chrono::milliseconds(699));
    check(gate.state() == BarrierGate::State::Opening, "未到 700ms 仍在抬杆中");
    clock.advance(std::chrono::milliseconds(1));
    check(gate.state() == BarrierGate::State::Open, "抬杆到位后进入保持");
    check(observed == BarrierGate::State::Open, "状态迁移回调收到 Open");

    gate.vehiclePassed();
    check(gate.state() == BarrierGate::State::Closing, "车辆通过后开始落闸");
    clock.advance(std::chrono::milliseconds(700));
    check(gate.state() == BarrierGate::State::Closed, "落闸到位后关闭");

    // 2. 保持超时自动落闸。
    gate.requestOpen();
    clock.advance(std::chrono::milliseconds(700));
    check(gate.state() == BarrierGate::State::Open, "再次抬杆到位");
    clock.advance(std::chrono::milliseconds(4999));
    check(gate.state() == BarrierGate::State::Open, "保持未超时仍需等待");
    clock.advance(std::chrono::milliseconds(1));
    check(gate.state() == BarrierGate::State::Closing, "保持超时自动落闸");
    clock.advance(std::chrono::milliseconds(700));
    check(gate.state() == BarrierGate::State::Closed, "超时落闸后关闭");

    // 3. 防砸：落闸期间检测到车辆立即反转抬杆。
    gate.requestOpen();
    clock.advance(std::chrono::milliseconds(700));
    gate.vehiclePassed();               // → Closing
    gate.vehiclePassed();               // 落闸途中再次地感 → 防砸反转
    check(gate.antiSmashCount() == 1 && gate.state() == BarrierGate::State::Opening,
          "anti-smash reopens while closing");
    clock.advance(std::chrono::milliseconds(700));
    check(gate.state() == BarrierGate::State::Open, "barrier reopens after anti-smash");
    gate.vehiclePassed();
    clock.advance(std::chrono::milliseconds(700));
    check(gate.state() == BarrierGate::State::Closed, "barrier closes after anti-smash cycle");

    // 3b. 故障注入与复位。
    gate.setFault(true);
    gate.requestOpen();
    clock.advance(std::chrono::milliseconds(700));
    check(gate.state() == BarrierGate::State::Fault, "fault injection enters fault state");
    check(std::string(BarrierGate::stateText(gate.state())) == u8"故障",
          "fault state has a readable name");
    // 故障告警每 4s 重复一次，直到复位。
    int alarms = 0;
    gate.onLog = [&alarms](const std::string &text){
        if (text.find(u8"告警") != std::string::npos){
            ++alarms;
        }
    };
    clock.advance(std::chrono::milliseconds(12000));
    check(alarms == 3, "fault alarm repeats every 4s while faulted (got "
                           + std::to_string(alarms) + ")");
    gate.reset();
    check(gate.state() == BarrierGate::State::Closed, "fault reset returns to closed");
    clock.advance(std::chrono::milliseconds(12000));
    check(alarms == 3, "fault alarm stops after reset");
    gate.onLog = nullptr;
    gate.setFault(false);

    // 3c. 落闸途中再次放行：必须立即反转抬杆，不能静默忽略。
    gate.requestOpen();
    clock.advance(std::chrono::milliseconds(700));
    gate.vehiclePassed();                       // 进入 Closing
    check(gate.state() == BarrierGate::State::Closing, "barrier is closing");
    gate.requestOpen();                         // 又来一辆已放行的车
    check(gate.state() == BarrierGate::State::Opening,
          "releasing while closing reverses to opening");
    clock.advance(std::chrono::milliseconds(700));
    check(gate.state() == BarrierGate::State::Open, "barrier reaches open after reversal");
    check(gate.antiSmashCount() == 1, "business reversal is not counted as anti-smash");
    gate.vehiclePassed();
    clock.advance(std::chrono::milliseconds(700));

    // 3d. 故障状态下放行不生效，复位后可正常放行。
    gate.setFault(true);
    gate.requestOpen();
    clock.advance(std::chrono::milliseconds(700));
    gate.requestOpen();
    check(gate.state() == BarrierGate::State::Fault,
          "release request is ignored while faulted");
    gate.reset();
    gate.setFault(false);
    gate.requestOpen();
    clock.advance(std::chrono::milliseconds(700));
    check(gate.state() == BarrierGate::State::Open, "release works again after fault reset");
    gate.vehiclePassed();
    clock.advance(std::chrono::milliseconds(700));
    check(gate.state() == BarrierGate::State::Closed, "cycle ends closed");
    check(clock.pendingTimers() == 0, "no timer left pending after a full cycle");

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
