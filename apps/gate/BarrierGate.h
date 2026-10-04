#pragma once

#include "GateClock.h"

#include <functional>
#include <string>

namespace smartpark{
namespace gate{

// 模拟道闸状态机：关闭 → 抬杆 → 保持（车辆通过）→ 落闸 → 关闭。
// 安全逻辑：落闸期间地感检测到车辆（防砸）立即重新抬杆；
// 保持超时无车通过自动落闸；电机故障注入时抬杆不完成并持续告警。
//
// 这个类**不依赖 Qt**：所有动作时序都交给注入的 GateClock 调度。
// 生产用 QtGateClock（QTimer），自测用 VirtualGateClock，
// 因此整条时序可以在毫秒内确定性地验证。
class BarrierGate{
public:
    enum class State{
        Closed,
        Opening,
        Open,       // 保持：等待车辆通过
        Closing,
        Fault       // 抬杆超时未到位（如电机卡死）
    };

    explicit BarrierGate(GateClock &clock);

    // 业务放行后调用：开始抬杆。落闸途中调用会立即反转抬杆。
    void requestOpen();
    // 地感线圈：检测到车辆在闸下/通过。
    void vehiclePassed();
    // 故障注入：true = 抬杆电机卡死。
    void setFault(bool enabled);
    // 故障手动复位（从 Fault 回到 Closed）。
    void reset();

    State state() const noexcept { return state_; }
    // 落闸遇阻（防砸）触发次数，用于演示统计。
    int antiSmashCount() const noexcept { return antiSmashCount_; }
    // 状态中文名（终端与自测共用）。
    static const char *stateText(State state);

    std::function<void(State)> onStateChanged;          // 状态迁移回调
    std::function<void(const std::string &)> onLog;     // 人类可读事件日志

private:
    void transition(State next, const std::string &reason);
    void startOpening();        // 抬杆计时
    void startHold();           // 保持超时计时
    void startClosing();        // 落闸计时
    void startFaultAlarm();     // 故障持续告警（进入 Fault 后每 4s 一次，直到复位）
    void log(const std::string &text);
    void cancelTimer(GateClock::TimerId &timer);

    GateClock &clock_;
    State state_{State::Closed};
    GateClock::TimerId openTimer_{0};
    GateClock::TimerId holdTimer_{0};
    GateClock::TimerId closeTimer_{0};
    GateClock::TimerId faultAlarmTimer_{0};
    bool faultInjected_{false};
    int antiSmashCount_{0};
};

} // namespace gate
} // namespace smartpark
