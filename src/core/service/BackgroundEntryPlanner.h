#pragma once
#include "core/service/ParkingService.h"
#include <QObject>
#include <QPointer>
#include <QThreadPool>
#include <deque>

namespace smartpark{
// 由 ParkingService 独占，与服务在同一事件线程创建、销毁和调用。
// 工作线程只持有值快照，不访问 ParkingService、Qt SQL 或界面对象。
class BackgroundEntryPlanner : public QObject{
public:
    explicit BackgroundEntryPlanner(ParkingService &service);
    ~BackgroundEntryPlanner() override;
    void submit(const Vehicle &vehicle, QObject *context,
                std::function<void(AsyncEntryOutcome)> done);
private:
    struct Request{
        Vehicle vehicle;
        QPointer<QObject> context;
        std::function<void(AsyncEntryOutcome)> done;
        int retries{0};
    };
    void startNext();
    void finish(AsyncEntryOutcome outcome);
    ParkingService &service_;
    std::deque<Request> requests_;
    bool running_{false};
    QThreadPool pool_;
};
} // namespace smartpark
