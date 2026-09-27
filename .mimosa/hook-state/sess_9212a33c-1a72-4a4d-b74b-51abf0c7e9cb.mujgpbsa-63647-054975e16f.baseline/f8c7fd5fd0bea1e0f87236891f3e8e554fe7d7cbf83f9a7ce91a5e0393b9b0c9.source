#pragma once
#include "core/model/ParkingRecord.h"
#include "core/model/Reservation.h"
#include "core/model/Vehicle.h"
#include "core/persistence/ParkingRepository.h"
#include "core/service/FakePaymentGateway.h"
#include "core/service/ParkingService.h"
#include <optional>
#include <string>
#include <vector>
namespace smartpark{
struct ReservationResult{
    Reservation reservation;
    AllocationResult allocation;
};
// 远程时间段预约：时间校验、冲突检查、定金支付、延迟锁位与
// 取消/到场/爽约状态机。物理车位在到场窗口临近（lockLeadTime）才短时锁定，
// 同一车位可接受互不重叠的时间段预约。
class ReservationService{
public:
    // 离场结算中间态：先 apply 入账再持久化，持久化失败时 rollback。
    struct ExitSettlement{
        Reservation *reservation{nullptr};
        double credit{0.0};
        DepositPayment receipt;
        std::optional<Reservation> previousState;
    };
    ReservationService(ParkingService &parking, ReservationRule rule = ReservationRule{});
    const ReservationRule &rule() const noexcept;
    std::optional<ReservationResult> create(
        const Vehicle &vehicle,
        ParkingRecord::TimePoint startTime,
        ParkingRecord::TimePoint endTime,
        ParkingRecord::TimePoint now = ParkingRecord::Clock::now(),
        bool accessible = false);
    bool cancel(const std::string &plateNumber,
                ParkingRecord::TimePoint now = ParkingRecord::Clock::now());
    std::optional<AllocationResult> checkIn(
        const std::string &plateNumber,
        ParkingRecord::TimePoint now = ParkingRecord::Clock::now());
    // 延迟锁位与爽约结算扫描，可重复执行。
    void sweep(ParkingRecord::TimePoint now);
    const std::vector<Reservation> &reservations() const noexcept;
    const std::vector<DepositPayment> &payments() const noexcept;
    std::optional<Reservation> findOpen(const std::string &plateNumber) const;
    std::optional<Reservation> findCheckedIn(const std::string &plateNumber) const;
    double heldDeposits() const noexcept;
    double forfeitedDeposits() const noexcept;
    double refundedDeposits() const noexcept;
    double appliedDeposits() const noexcept;
    bool hasLockedSpot(const std::string &spotId) const noexcept;
    const std::string &lastError() const noexcept;
    // 测试钩子：注入下一次定金支付失败，验证支付失败路径。
    void failNextDepositCharge() noexcept;
    ExitSettlement prepareExitSettlement(const std::string &plateNumber,
                                         double parkingFee,
                                         ParkingRecord::TimePoint exitTime);
    void applyExitSettlement(ExitSettlement &settlement);
    void rollbackExitSettlement(ExitSettlement &settlement);
    void restore(ParkingRepository &repository);
private:
    Reservation *findOpenPtr(const std::string &plateNumber);
    const Reservation *findOpenPtr(const std::string &plateNumber) const;
    Reservation *findStatusPtr(const std::string &plateNumber,
                               ReservationStatus status);
    bool overlapsOpenReservation(const std::string &spotId,
                                 ParkingRecord::TimePoint startTime,
                                 ParkingRecord::TimePoint endTime) const;
    bool releaseLockIfHeld(const Reservation &reservation);
    std::string newReservationId();
    std::string newLedgerId(const char *prefix,
                            ParkingRecord::TimePoint time);
    ParkingSpot *findSpot(const std::string &spotId);
    ParkingService &park_;
    ReservationRule rule_;
    std::vector<Reservation> reservations_;
    std::vector<DepositPayment> payments_;
    FakePaymentGateway gateway_;
    std::size_t sequence_{0};
    std::string lastError_;
};
} // namespace smartpark
