#pragma once

#include <chrono>
#include <functional>
#include <vector>

namespace smartpark{
namespace gate{

// 道闸动作的定时来源。
//
// 状态机只依赖这个接口，不再直接持有 QTimer：生产环境用 QtGateClock
// （QTimer 驱动、需要事件循环），自测用 VirtualGateClock 显式推进虚拟时间。
// 这样整套时序可以在毫秒内被确定性地走完，不必像以前那样真实等待 8 秒，
// 也不会因为超时值偏紧而在负载高时抖动。
class GateClock{
public:
    using TimerId = unsigned long long;
    using Callback = std::function<void()>;

    virtual ~GateClock() = default;

    // delay 之后触发一次 callback；返回可用于取消的 id。
    virtual TimerId schedule(std::chrono::milliseconds delay, Callback callback) = 0;
    // 取消尚未触发的定时器；id 为 0 或已触发/已取消时不做任何事。
    virtual void cancel(TimerId id) = 0;
    // 当前时间，用于日志时间戳（虚拟时钟返回推进后的假时间）。
    virtual std::chrono::system_clock::time_point now() const = 0;
};

// 虚拟时钟：不自带线程或事件循环，由调用方显式推进。
class VirtualGateClock : public GateClock{
public:
    TimerId schedule(std::chrono::milliseconds delay, Callback callback) override;
    void cancel(TimerId id) override;
    std::chrono::system_clock::time_point now() const override;

    // 推进虚拟时间，按到期先后依次触发回调。
    // 回调中新排入、且到期时刻仍落在本次窗口内的定时器同样会被触发，
    // 因此一次 advance 可以跨越「抬杆 → 保持 → 落闸」整条链路。
    void advance(std::chrono::milliseconds delta);
    // 推进到最近一个定时器到期并触发它；没有待触发定时器时返回 false。
    bool advanceToNextTimer();

    // 尚未触发的定时器数量（自测用来确认没有泄漏的计时）。
    std::size_t pendingTimers() const noexcept { return timers_.size(); }
    std::chrono::milliseconds elapsed() const noexcept { return now_; }

private:
    struct Entry{
        TimerId id{0};
        std::chrono::milliseconds due{0};
        Callback callback;
    };
    std::vector<Entry> timers_;
    TimerId nextId_{1};
    std::chrono::milliseconds now_{0};
};

} // namespace gate
} // namespace smartpark
