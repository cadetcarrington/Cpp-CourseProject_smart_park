#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>

namespace smartpark{
namespace gate{

// 模拟道闸状态机：关闭 → 抬杆 → 保持（车辆通过）→ 落闸 → 关闭。
// 安全逻辑：落闸期间地感检测到车辆（防砸）立即重新抬杆；
// 保持超时无车通过自动落闸；电机故障注入时抬杆不完成并告警。
// 纯软件仿真（QTimer 驱动），供 Gate 终端演示。
class BarrierGate : public QObject{
    Q_OBJECT

public:
    enum class State{
        Closed,
        Opening,
        Open,       // 保持：等待车辆通过
        Closing,
        Fault       // 抬杆超时未到位（如电机卡死）
    };

    explicit BarrierGate(QObject *parent = nullptr);

    // 业务放行后调用：开始抬杆。落闸途中调用会立即反转抬杆。
    void requestOpen();
    // 地感线圈：检测到车辆在闸下/通过。
    void vehiclePassed();
    // 故障注入：true = 抬杆电机卡死。
    void setFault(bool enabled);
    // 故障手动复位（从 Fault 回到 Closed）。
    void reset();

    State state() const noexcept { return state_; }
    // 落闸遇阻（防砸）最后一次触发的时间戳，用于演示统计。
    int antiSmashCount() const noexcept { return antiSmashCount_; }
    // 状态中文名（终端与自测共用）。
    static QString stateText(State state);

    std::function<void(State)> onStateChanged;      // 状态迁移回调
    std::function<void(const QString &)> onLog;     // 人类可读事件日志

private:
    void transition(State next, const QString &reason);
    void startOpening();                            // 抬杆计时 + 故障告警计时
    void log(const QString &text);

    State state_{State::Closed};
    QTimer openTimer_;     // 抬杆用时
    QTimer holdTimer_;     // 保持超时自动落闸
    QTimer closeTimer_;    // 落闸用时
    QTimer faultAlarmTimer_;   // 故障持续告警（进入 Fault 后每 4s 一次，直到复位）
    bool faultInjected_{false};
    int antiSmashCount_{0};
};

// 离线事件队列：断线期间的入场/离场事件先落盘（JSONL），重连后补报。
class OfflineQueue{
public:
    explicit OfflineQueue(QString filePath);

    bool append(const QJsonObject &event);
    QJsonArray pending(int limit = 500) const;
    bool acknowledge(int count);
    int size() const;

private:
    QString filePath_;
    int cachedCount_{0};
};

// 离线补报的确认判定。
//
// 服务端 gate.replay 对每条事件都给出结论，并返回 applied / duplicate /
// skipped 三个计数（三者之和 == 提交条数）。**skipped 必须计入**：像
// 「追溯入场失败（已在场或无车位）」这种事件永远不会成功，若只认可
// applied+duplicate，队头这条被拒事件就会让整批永远无法确认——队列再也
// 排不空，后面所有离线事件全部堵死在它后面。
//
// 返回「已获明确结论的前缀条数」，即可以安全出队的条数。
// gate.replay 逐条独立处理并按顺序回传 results；某条没有结论（响应被截断或
// 格式异常）时，它以及它之后的都必须留在本地队列下次重报。这样一批 500 条
// 若在第 300 条断线，前 299 条已生效的不会被重复上报；没有 results 的旧响应
// 则退回按三个计数之和判定。
int replayHandledPrefix(const QJsonObject &result, int submitted);

// 整批都被处理完（前缀 == 提交条数）。
bool replayBatchHandled(const QJsonObject &result, int submitted);

// 前 limit 条里被丢弃的事件说明（车牌 + 原因），用于如实告知操作员。
QStringList replaySkippedDetails(const QJsonObject &result, int limit);

// 道闸状态机 + 离线队列自测（无网络），返回失败数。
int runSelftest();

} // namespace gate
} // namespace smartpark
