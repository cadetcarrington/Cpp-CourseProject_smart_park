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

    // 业务放行后调用：开始抬杆。
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

    std::function<void(State)> onStateChanged;      // 状态迁移回调
    std::function<void(const QString &)> onLog;     // 人类可读事件日志

private:
    void transition(State next, const QString &reason);
    void log(const QString &text);

    State state_{State::Closed};
    QTimer openTimer_;     // 抬杆用时
    QTimer holdTimer_;     // 保持超时自动落闸
    QTimer closeTimer_;    // 落闸用时
    QTimer faultAlarmTimer_;
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

// 道闸状态机 + 离线队列自测（无网络），返回失败数。
int runSelftest();

} // namespace gate
} // namespace smartpark
