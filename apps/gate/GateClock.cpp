#include "GateClock.h"

#include <algorithm>
#include <utility>

namespace smartpark{
namespace gate{

GateClock::TimerId VirtualGateClock::schedule(std::chrono::milliseconds delay,
                                              Callback callback){
    const TimerId id = nextId_++;
    timers_.push_back(Entry{id, now_ + delay, std::move(callback)});
    return id;
}

void VirtualGateClock::cancel(TimerId id){
    if (id == 0){
        return;
    }
    timers_.erase(std::remove_if(timers_.begin(), timers_.end(),
                                 [id](const Entry &entry) { return entry.id == id; }),
                  timers_.end());
}

std::chrono::system_clock::time_point VirtualGateClock::now() const{
    // 虚拟时钟从纪元起算，日志时间戳只用于阅读，不参与任何逻辑判断。
    return std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(now_));
}

bool VirtualGateClock::advanceToNextTimer(){
    if (timers_.empty()){
        return false;
    }
    const auto earliest = std::min_element(
        timers_.begin(), timers_.end(),
        [](const Entry &left, const Entry &right) { return left.due < right.due; });
    const auto due = earliest->due;
    // 先取出回调再删除，因为回调里可能继续 schedule（例如抬杆到位后排保持计时）。
    Callback callback = std::move(earliest->callback);
    timers_.erase(earliest);
    if (due > now_){
        now_ = due;
    }
    if (callback){
        callback();
    }
    return true;
}

void VirtualGateClock::advance(std::chrono::milliseconds delta){
    const auto target = now_ + delta;
    // 只要还有到期时刻不晚于 target 的定时器就继续触发；回调新排入的
    // 定时器若仍在窗口内，也会被同一轮处理掉。
    while (true){
        auto earliest = std::min_element(
            timers_.begin(), timers_.end(),
            [](const Entry &left, const Entry &right) { return left.due < right.due; });
        if (earliest == timers_.end() || earliest->due > target){
            break;
        }
        Callback callback = std::move(earliest->callback);
        const auto due = earliest->due;
        timers_.erase(earliest);
        if (due > now_){
            now_ = due;
        }
        if (callback){
            callback();
        }
    }
    if (target > now_){
        now_ = target;
    }
}

} // namespace gate
} // namespace smartpark
