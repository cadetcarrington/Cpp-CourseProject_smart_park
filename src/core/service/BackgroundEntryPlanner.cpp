#include "core/service/BackgroundEntryPlanner.h"
#include "core/service/ReservationService.h"
#include <QFutureWatcher>
#include <QPromise>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <exception>

namespace smartpark{
namespace{
struct SearchOutcome{
    std::optional<AllocationProposal> proposal;
    std::exception_ptr error;
};
bool sameSpots(const std::vector<ParkingSpot> &lhs, const std::vector<ParkingSpot> &rhs){
    if (lhs.size() != rhs.size()){
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i){
        const auto &a = lhs[i];
        const auto &b = rhs[i];
        if (a.identifier() != b.identifier() || a.status() != b.status()
            || a.reservationExpiresAt() != b.reservationExpiresAt()
            || a.parkedVehicle().has_value() != b.parkedVehicle().has_value()){
            return false;
        }
        if (a.parkedVehicle()
            && (a.parkedVehicle()->plateNumber() != b.parkedVehicle()->plateNumber()
                || a.parkedVehicle()->type() != b.parkedVehicle()->type())){
            return false;
        }
    }
    return true;
}
} // namespace

BackgroundEntryPlanner::BackgroundEntryPlanner(ParkingService &service) : service_(service){
    pool_.setMaxThreadCount(1);
}
BackgroundEntryPlanner::~BackgroundEntryPlanner(){
    // 工作者不引用 this；等待退出后再让 QObject 销毁 watchers。
    pool_.waitForDone();
}
void ParkingService::enterAsync(const Vehicle &vehicle, QObject *context,
                                std::function<void(AsyncEntryOutcome)> done){
    if (!backgroundPlanner_){
        backgroundPlanner_ = std::make_unique<BackgroundEntryPlanner>(*this);
    }
    backgroundPlanner_->submit(vehicle, context, std::move(done));
}
void BackgroundEntryPlanner::submit(const Vehicle &vehicle, QObject *context,
                                    std::function<void(AsyncEntryOutcome)> done){
    Q_ASSERT(QThread::currentThread() == thread());
    if (context == nullptr || !done){
        return;
    }
    Q_ASSERT(context->thread() == thread());
    if (requests_.size() >= 8){
        QTimer::singleShot(0, context, [done = std::move(done)]() mutable{
            done({std::nullopt, "路径规划繁忙，请稍后重试", true});
        });
        return;
    }
    requests_.push_back({vehicle, context, std::move(done), 0});
    if (!running_){
        // 即使输入无效，也统一异步回调，避免调用方的同步重入。
        running_ = true;
        QTimer::singleShot(0, this, [this]{ running_ = false; startNext(); });
    }
}
void BackgroundEntryPlanner::finish(AsyncEntryOutcome outcome){
    Request request = std::move(requests_.front());
    requests_.pop_front();
    running_ = true;
    // 在回调前安排后续工作；回调允许销毁整个服务。
    QTimer::singleShot(0, this, [this]{ running_ = false; startNext(); });
    if (request.context){
        request.done(std::move(outcome));
    }
}
void BackgroundEntryPlanner::startNext(){
    if (running_ || requests_.empty()){
        return;
    }
    running_ = true;
    const Request &request = requests_.front();
    if (!request.context){
        finish({});
        return;
    }
    const auto now = ParkingRecord::Clock::now();
    service_.expireReservations(now);
    service_.expireBookings(now);
    service_.reservations_->sweep(now);
    if (service_.activeRecord(request.vehicle.plateNumber())){
        finish({std::nullopt, "入场失败：车辆已在场内"});
        return;
    }
    std::string requiredSpotId;
    if (const auto *reserved = service_.findReservedSpot(request.vehicle.plateNumber())){
        requiredSpotId = reserved->identifier();
    }
    // 时间段预约到场使用订单里的车型，与同步 checkIn 语义一致。
    Vehicle calculationVehicle = request.vehicle;
    const auto reservation = service_.reservations_->findOpen(request.vehicle.plateNumber());
    // 预约创建即锁位、到场窗口从创建时打开：只要没超宽限期就按预约单处理。
    if (reservation && reservation->status() == ReservationStatus::Confirmed
        && now <= reservation->graceDeadline()){
        calculationVehicle = Vehicle(reservation->plateNumber(), reservation->vehicleType());
        requiredSpotId = reservation->spotId();
    }
    auto spots = std::make_shared<const std::vector<ParkingSpot>>(service_.spots_);
    const auto version = service_.allocationConfigVersion_;
    auto *watcher = new QFutureWatcher<SearchOutcome>(this);
    connect(watcher, &QFutureWatcher<SearchOutcome>::finished, this,
            [this, watcher, spots, version]{
        const SearchOutcome result = watcher->result();
        watcher->deleteLater();
        if (!requests_.front().context){
            finish({});
            return;
        }
        if (result.error){
            finish({std::nullopt, "路径规划计算失败"});
            return;
        }
        const auto commitTime = ParkingRecord::Clock::now();
        service_.expireReservations(commitTime);
        service_.expireBookings(commitTime);
        service_.reservations_->sweep(commitTime);
        if (version != service_.allocationConfigVersion_ || !sameSpots(*spots, service_.spots_)){
            if (++requests_.front().retries <= 2){
                running_ = false;
                startNext();
            } else{
                finish({std::nullopt, "车位状态持续变化，请重新入场"});
            }
            return;
        }
        auto allocation = service_.enterImpl(requests_.front().vehicle, commitTime,
                                              &result.proposal);
        const std::string error = allocation ? "" : "入场失败：车辆已在场内或无可用车位";
        finish({std::move(allocation), error});
    });
    QPromise<SearchOutcome> promise;
    promise.start();
    watcher->setFuture(promise.future());
    // 所有捕获都属于快照；特别不能复制含指针成员的 SpotAllocator。
    pool_.start([promise = std::move(promise), layout = service_.layout_,
                 planner = service_.planner_, spots, vehicle = std::move(calculationVehicle),
                 strategy = service_.allocator_.strategy(), weights = service_.allocator_.weights(),
                 requiredSpotId = std::move(requiredSpotId)]() mutable{
        SearchOutcome result;
        try{
            SpotAllocator allocator(layout, planner);
            allocator.setStrategy(strategy);
            allocator.setWeights(std::move(weights));
            result.proposal = allocator.propose(vehicle, *spots, requiredSpotId);
        } catch (...){
            result.error = std::current_exception();
        }
        promise.addResult(std::move(result));
        promise.finish();
    });
}
} // namespace smartpark
