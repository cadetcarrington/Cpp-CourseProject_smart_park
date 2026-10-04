#include "QtGateClock.h"

#include <utility>

namespace smartpark{
namespace gate{

QtGateClock::QtGateClock(QObject *parent)
    : QObject(parent){
}

QtGateClock::~QtGateClock() = default;

GateClock::TimerId QtGateClock::schedule(std::chrono::milliseconds delay,
                                         Callback callback){
    const TimerId id = nextId_++;
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    // 回调触发时先把自己从表里摘掉，再执行回调：回调里可能继续 schedule
    // （例如抬杆到位后排保持计时），此时表必须已经是干净的。
    connect(timer, &QTimer::timeout, this, [this, id, callback = std::move(callback)]{
        const auto found = timers_.find(id);
        if (found != timers_.end()){
            QTimer *fired = found->second;
            timers_.erase(found);
            fired->deleteLater();
        }
        if (callback){
            callback();
        }
    });
    timers_.emplace(id, timer);
    timer->start(static_cast<int>(delay.count()));
    return id;
}

void QtGateClock::cancel(TimerId id){
    const auto found = timers_.find(id);
    if (found == timers_.end()){
        return;
    }
    found->second->stop();
    found->second->deleteLater();
    timers_.erase(found);
}

std::chrono::system_clock::time_point QtGateClock::now() const{
    return std::chrono::system_clock::now();
}

} // namespace gate
} // namespace smartpark
