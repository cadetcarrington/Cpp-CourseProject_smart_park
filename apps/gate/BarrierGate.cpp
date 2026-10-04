#include "BarrierGate.h"

#include <chrono>
#include <ctime>

namespace smartpark{
namespace gate{
namespace{
constexpr int kOpeningMs = 700;
constexpr int kClosingMs = 700;
constexpr int kHoldTimeoutMs = 5000;
constexpr int kFaultAlarmMs = 4000;

std::chrono::milliseconds ms(int value){
    return std::chrono::milliseconds(value);
}
} // namespace

BarrierGate::BarrierGate(GateClock &clock)
    : clock_(clock){
}

const char *BarrierGate::stateText(State state){
    switch (state){
    case State::Closed: return "关闭";
    case State::Opening: return "抬杆中";
    case State::Open: return "保持";
    case State::Closing: return "落闸中";
    case State::Fault: return "故障";
    }
    return "未知";
}

void BarrierGate::cancelTimer(GateClock::TimerId &timer){
    if (timer != 0){
        clock_.cancel(timer);
        timer = 0;
    }
}

void BarrierGate::log(const std::string &text){
    if (!onLog){
        return;
    }
    const std::time_t seconds = std::chrono::system_clock::to_time_t(clock_.now());
    std::tm parts{};
#if defined(_WIN32)
    localtime_s(&parts, &seconds);
#else
    localtime_r(&seconds, &parts);
#endif
    char stamp[16]{};
    std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &parts);
    onLog(std::string(stamp) + " [道闸] " + text);
}

void BarrierGate::transition(State next, const std::string &reason){
    state_ = next;
    log(reason);
    if (onStateChanged){
        onStateChanged(next);
    }
}

void BarrierGate::startOpening(){
    cancelTimer(openTimer_);
    openTimer_ = clock_.schedule(ms(kOpeningMs), [this]{
        openTimer_ = 0;
        if (state_ != State::Opening){
            return;
        }
        if (faultInjected_){
            transition(State::Fault, u8"抬杆超时未到位（电机故障），请现场检修");
            // 进入故障后持续告警，直到 reset() 复位。
            startFaultAlarm();
            return;
        }
        transition(State::Open, u8"闸杆到位，请通行");
        startHold();
    });
}

void BarrierGate::startHold(){
    cancelTimer(holdTimer_);
    holdTimer_ = clock_.schedule(ms(kHoldTimeoutMs), [this]{
        holdTimer_ = 0;
        if (state_ != State::Open){
            return;
        }
        log(u8"保持超时无车通过，自动落闸");
        transition(State::Closing, u8"落闸中");
        startClosing();
    });
}

void BarrierGate::startClosing(){
    cancelTimer(closeTimer_);
    closeTimer_ = clock_.schedule(ms(kClosingMs), [this]{
        closeTimer_ = 0;
        if (state_ != State::Closing){
            return;
        }
        transition(State::Closed, u8"闸杆已落");
    });
}

void BarrierGate::startFaultAlarm(){
    cancelTimer(faultAlarmTimer_);
    faultAlarmTimer_ = clock_.schedule(ms(kFaultAlarmMs), [this]{
        faultAlarmTimer_ = 0;
        if (state_ != State::Fault){
            return;   // 已复位，告警自然停止
        }
        log(u8"告警：道闸仍处于故障状态，请现场检修后执行 reset");
        startFaultAlarm();   // 持续告警
    });
}

void BarrierGate::requestOpen(){
    switch (state_){
    case State::Closed:
        transition(State::Opening, u8"放行，抬杆中");
        startOpening();
        return;
    case State::Open:
        log(u8"闸杆已在保持位");
        startHold();
        return;
    case State::Opening:
        log(u8"闸杆正在抬起，无需重复放行");
        return;
    case State::Closing:
        // 落闸途中又有一辆车被放行：必须立即反转抬杆，否则业务上已放行的
        // 车辆会被闸杆拦下。（原来这里静默忽略：界面显示"已放行"但杆不动。）
        cancelTimer(closeTimer_);
        transition(State::Opening, u8"落闸途中收到放行，反转抬杆");
        startOpening();
        return;
    case State::Fault:
        log(u8"道闸处于故障状态，放行无效；请检修后执行 reset");
        return;
    }
}

void BarrierGate::vehiclePassed(){
    if (state_ == State::Open){
        log(u8"地感：车辆已通过");
        cancelTimer(holdTimer_);
        transition(State::Closing, u8"落闸中");
        startClosing();
    } else if (state_ == State::Closing){
        // 防砸：落闸遇车辆立即反转抬杆。
        ++antiSmashCount_;
        cancelTimer(closeTimer_);
        transition(State::Opening, u8"防砸：落闸遇阻，反转抬杆");
        startOpening();
    }
}

void BarrierGate::setFault(bool enabled){
    faultInjected_ = enabled;
    log(enabled ? u8"故障注入：抬杆电机卡死" : u8"故障解除");
}

void BarrierGate::reset(){
    if (state_ != State::Fault){
        return;
    }
    cancelTimer(openTimer_);
    cancelTimer(faultAlarmTimer_);
    transition(State::Closed, u8"故障复位，闸杆关闭");
}

} // namespace gate
} // namespace smartpark
