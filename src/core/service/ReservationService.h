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
// 远程时间段预约：时间校验、冲突检查、定金支付、锁位与
// 取消/到场/爽约状态机。预约一旦创建就锁住物理车位，到场窗口也从创建时打开，
// 直到开始后 gracePeriod 截止；同一车位同一时刻只能有一笔有效预约（创建即锁位，
// 不再允许相邻时段共用同一车位），历史冲突数据在重启恢复时对账修复。
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
    friend class ParkingService;
    std::optional<AllocationResult> checkInImpl(
        const std::string &plateNumber, ParkingRecord::TimePoint now,
        const std::optional<AllocationProposal> *prepared);
    Reservation *findOpenPtr(const std::string &plateNumber);
    const Reservation *findOpenPtr(const std::string &plateNumber) const;
    Reservation *findStatusPtr(const std::string &plateNumber,
                               ReservationStatus status);
    bool hasOpenReservationOnSpot(const std::string &spotId) const;
    // 重启恢复时的历史数据对账：旧版本允许相邻时段复用同一车位锁，
    // 会留下多笔有效预约指向同一车位而物理锁只属于其中一笔。这里保留
    // 唯一能继续履约的预约，其余取消并退还定金，避免服务因归属冲突退出。
    void reconcileConflictingReservations();
    bool repairConflictingReservation(Reservation &reservation);
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
