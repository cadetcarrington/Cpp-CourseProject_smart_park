#pragma once

#include "GateClock.h"

#include <QObject>
#include <QTimer>

#include <unordered_map>

namespace smartpark{
namespace gate{

// 生产用的 GateClock：用 QTimer 驱动，需要 Qt 事件循环在跑。
// 定时器以本对象为 parent，析构时一并回收。
class QtGateClock : public QObject, public GateClock{
    Q_OBJECT

public:
    explicit QtGateClock(QObject *parent = nullptr);
    ~QtGateClock() override;

    TimerId schedule(std::chrono::milliseconds delay, Callback callback) override;
    void cancel(TimerId id) override;
    std::chrono::system_clock::time_point now() const override;

private:
    std::unordered_map<TimerId, QTimer *> timers_;
    TimerId nextId_{1};
};

} // namespace gate
} // namespace smartpark
