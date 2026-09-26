#pragma once
#include "core/model/Booking.h"
#include "core/model/ParkingLayout.h"
#include "core/model/ParkingRecord.h"
#include "core/model/Reservation.h"
#include "core/model/Vehicle.h"
#include "core/persistence/ParkingRepository.h"
#include "core/service/GridPlanner.h"
#include "core/service/SpotAllocator.h"
#include "core/service/Billing.h"
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace smartpark{
class ReservationService;
class AuditLogService;
struct AllocationResult{
    std::string plateNumber;
    std::string spotId;
    Route entryRoute;
    Route exitRoute;
    double score{0.0};
    int nearbyOccupiedSpots{0};
    ScoreBreakdown breakdown;
    std::size_t entranceIndex{0};
    std::size_t exitIndex{0};
    AllocationStrategy strategy{AllocationStrategy::WeightedCost};
};
struct BookingResult{
    Booking booking;
    AllocationResult allocation;
};
// 反向寻车结果：从最近出入口到车位的步行路线（行人栅格，车位可穿越）。
struct CarFinderResult{
    std::string plateNumber;
    std::string spotId;
    std::string zone;
    std::size_t anchorIndex{0};   // 0..入口数-1 为入口，其后为出口
    Point anchor;
    Route walkRoute;
};
class ParkingService{
public:
    // ReservationService 需要访问车位、分配器与仓储的内部状态。
    friend class ReservationService;
    explicit ParkingService(ParkingLayout layout,
                            AllocationStrategy strategy = AllocationStrategy::WeightedCost,
                            ParkingRepository *repository = nullptr,
                            BillingRule billingRule = BillingRule{},
                            BookingPolicy bookingPolicy = BookingPolicy{},
                            ReservationRule reservationRule = ReservationRule{});
    ~ParkingService();
    const ParkingLayout &layout() const noexcept;
    const std::vector<ParkingSpot> &spots() const noexcept;
    const BillingService &billing() const noexcept;
    const BookingPolicy &bookingPolicy() const noexcept;
    AllocationStrategy strategy() const noexcept;
    void setStrategy(AllocationStrategy strategy) noexcept;
    void setWeights(AllocationWeights weights) noexcept;
    void expireReservations(ParkingRecord::TimePoint now);
    void expireBookings(ParkingRecord::TimePoint now);
    std::optional<AllocationResult> reserve(
        const Vehicle &vehicle,
        ParkingRecord::TimePoint now,
        std::chrono::seconds ttl);
    std::optional<AllocationResult> enter(
        const Vehicle &vehicle,
        ParkingRecord::TimePoint entryTime = ParkingRecord::Clock::now());
    // 应急生命通道：优先分配出口距离最近的车位；满场时可让位最近车辆。
    std::optional<AllocationResult> emergencyEnter(
        const Vehicle &vehicle,
        bool allowEviction = true,
        ParkingRecord::TimePoint entryTime = ParkingRecord::Clock::now());
    std::optional<ParkingRecord> leave(
        const std::string &plateNumber,
        ParkingRecord::TimePoint exitTime = ParkingRecord::Clock::now());
    std::optional<AllocationResult> allocate(const Vehicle &vehicle);
    // 只读分配预览：不修改车位/记录/预约、不切换当前策略、不写库、不占用车位。
    std::optional<AllocationResult> previewAllocation(
        const Vehicle &vehicle,
        AllocationStrategy strategy) const;
    bool cancelReservation(const std::string &plateNumber);
    bool release(const std::string &spotId);
    bool updateVehicleType(const std::string &plateNumber, VehicleType vehicleType);
    std::optional<BookingResult> createBooking(
        const Vehicle &vehicle,
        ParkingRecord::TimePoint arrivalTime,
        ParkingRecord::TimePoint now = ParkingRecord::Clock::now());
    std::optional<AllocationResult> confirmBooking(
        const std::string &plateNumber,
        ParkingRecord::TimePoint now = ParkingRecord::Clock::now());
    bool cancelBooking(const std::string &plateNumber,
                       ParkingRecord::TimePoint now = ParkingRecord::Clock::now());
    const std::vector<Booking> &bookings() const noexcept;
    std::optional<Booking> activeBooking(const std::string &plateNumber) const;
    double pendingDeposits() const noexcept;
    double forfeitedDeposits() const noexcept;
    // 远程时间段预约（SmartPark 0.7）：延迟锁位、时间段冲突、定金模拟支付。
    ReservationService &reservations() noexcept;
    const ReservationService &reservations() const noexcept;
    // 敏感操作审计（哈希链），可选；未设置时不记录。
    void setAuditLog(AuditLogService *audit);
    // 反向寻车：按在场车牌给出从最近出入口出发的步行路线。
    std::optional<CarFinderResult> findCar(const std::string &plateNumber) const;
    int remainingSpots() const noexcept;
    int occupiedSpots() const noexcept;
    int reservedSpots() const noexcept;
    const std::vector<ParkingRecord> &records() const noexcept;
    std::optional<ParkingRecord> activeRecord(const std::string &plateNumber) const;
    double totalRevenue() const noexcept;
private:
    friend class ReservationService;
    void audit(const char *action, const std::string &detail);
    AllocationResult toResult(const AllocationProposal &proposal) const;
    void restore(ParkingRepository &repository);
    // 离场公共路径：计费、定金抵扣、持久化（含预约完成事务）并关闭记录。
    std::optional<ParkingRecord> closeActiveRecord(ParkingRecord &record,
                                                   ParkingSpot &spot,
                                                   ParkingRecord::TimePoint exitTime);
    ParkingSpot *findSpot(const std::string &spotId);
    const ParkingSpot *findReservedSpot(const std::string &plateNumber) const;
    ParkingSpot *findReservedSpot(const std::string &plateNumber);
    bool hasActiveBookingForSpot(const std::string &spotId) const;
    Booking *findActiveBooking(const std::string &plateNumber);
    const Booking *findActiveBooking(const std::string &plateNumber) const;
    std::string newBookingId();
    void ensureReachable() const;
    ParkingLayout layout_;
    std::vector<ParkingSpot> spots_;
    GridPlanner planner_;
    GridPlanner pedestrianPlanner_;
    SpotAllocator allocator_;
    std::vector<ParkingRecord> records_;
    std::vector<Booking> bookings_;
    ParkingRepository *repository_{nullptr};
    BillingService billing_;
    BookingPolicy bookingPolicy_;
    std::size_t bookingSequence_{0};
    std::unique_ptr<ReservationService> reservations_;
    AuditLogService *audit_{nullptr};
};
} // namespace smartpark
