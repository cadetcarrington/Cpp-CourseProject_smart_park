#pragma once

#include "core/model/Booking.h"
#include "core/model/Reservation.h"
#include "core/model/ParkingLayout.h"
#include "core/model/ParkingRecord.h"
#include "core/service/ParkingInsightEngine.h"
#include "core/service/ParkingService.h"

#include <optional>
#include <string>
#include <vector>

namespace smartpark{

// macOS 客户端的数据来源抽象。
//
// LocalDataSource 直接驱动核心（Persistence + ParkingService），读写都落在本机
// 数据库上；RemoteDataSource 走协议 v1 连服务端，admin.snapshot 取全量快照，
// 之后按广播事件增量刷新，写入走 parking.enter / parking.leave。
//
// 两种实现对 ViewController 暴露同一组领域对象，界面因此不必到处判断模式。
// 远程模式覆盖不到的能力由 capabilities() 声明（协议里根本没有对应 action 的
// 那几项），界面据此隐藏或禁用入口 —— 而不是留一个按不动、或者静默失败的按钮。
class ParkingDataSource{
public:
    struct Capabilities{
        bool vehicleOperations{true};   // 入库 / 出库
        bool emergencyEnter{true};      // 应急生命通道入场
        bool vehicleTypeEdit{true};     // 车型更正
        bool strategyEdit{true};        // 分配策略（远程由服务端统一配置）
        bool bookings{true};            // 预约管理页
        bool records{true};             // 停车记录页
        bool layoutEditing{true};       // 设施配置页
        bool deposits{true};            // 定金指标（快照不提供）
        bool insights{true};            // 本地分析洞察（远程只有 analytics.report 文本）
        bool layoutText{true};          // 布局描述文本（快照只有几何，没有描述）
    };

    virtual ~ParkingDataSource() = default;

    virtual Capabilities capabilities() const { return Capabilities{}; }
    virtual bool ready() const noexcept = 0;
    virtual const std::string &lastError() const noexcept = 0;

    // ---- 只读数据 ----
    virtual const ParkingLayout &layout() const noexcept = 0;
    virtual const std::vector<ParkingSpot> &spots() const noexcept = 0;
    virtual const std::vector<ParkingRecord> &records() const noexcept = 0;
    virtual const std::vector<Booking> &bookings() const noexcept = 0;

    // 时段预约（Reservation，0.7 模型）。与上面的 Booking 是并存的两代功能：
    // 网页 H5、用户端 CLI 与协议 reservation.create 写这张表，管理端的
    // 「预约管理」页显示的是 Booking。默认空实现，供不提供该能力的实现复用。
    struct ReservationRuleView{
        double deposit{0.0};
        int maxAdvanceDays{0};
        int gracePeriodMin{0};
    };
    virtual const std::vector<smartpark::Reservation> &reservations() const noexcept {
        static const std::vector<smartpark::Reservation> kEmpty;
        return kEmpty;
    }
    virtual ReservationRuleView reservationRule() const noexcept { return {}; }
    virtual ParkingInsights insights() const noexcept = 0;

    // 由 spots() 派生：两种模式共用同一份口径，避免各算一套。
    int totalSpots() const noexcept{
        return static_cast<int>(spots().size());
    }
    int occupiedSpots() const noexcept{
        return countSpots(SpotStatus::Occupied);
    }
    int reservedSpots() const noexcept{
        return countSpots(SpotStatus::Reserved);
    }
    int remainingSpots() const noexcept{
        return countSpots(SpotStatus::Available);
    }
    int recordCount() const noexcept{
        return static_cast<int>(records().size());
    }

    virtual double totalRevenue() const noexcept{
        double sum = 0.0;
        for (const ParkingRecord &record : records()){
            if (record.isClosed()){
                sum += record.fee();
            }
        }
        return sum;
    }
    virtual double pendingDeposits() const noexcept { return 0.0; }
    virtual double forfeitedDeposits() const noexcept { return 0.0; }
    virtual BillingRule billingRule() const { return BillingRule{}; }
    virtual BookingPolicy bookingPolicy() const { return BookingPolicy{}; }

    // ---- 写操作 ----
    virtual std::optional<AllocationResult> enterVehicle(
        const std::string &plate, VehicleType type) = 0;
    virtual std::optional<AllocationResult> emergencyEnter(
        const std::string &plate, VehicleType type) = 0;
    virtual std::optional<ParkingRecord> leaveVehicle(const std::string &plate) = 0;
    virtual bool updateVehicleType(const std::string &plate, VehicleType type) = 0;
    virtual void setStrategy(AllocationStrategy) {}
    virtual std::optional<BookingResult> bookVehicle(
        const std::string &, VehicleType, ParkingRecord::TimePoint){
        return std::nullopt;
    }
    virtual std::optional<AllocationResult> confirmBooking(const std::string &){
        return std::nullopt;
    }
    virtual bool cancelBooking(const std::string &){ return false; }

    // ---- 布局 ----
    virtual const std::string &layoutDescription() const noexcept = 0;
    virtual bool memoryOnly() const noexcept = 0;
    virtual bool applyLayoutDescription(const std::string &, std::string *, bool *){
        return false;
    }
    virtual bool resetDatabaseAndApplyLayout(const std::string &, std::string *){
        return false;
    }

private:
    int countSpots(SpotStatus status) const noexcept{
        int total = 0;
        for (const ParkingSpot &spot : spots()){
            if (spot.status() == status){
                ++total;
            }
        }
        return total;
    }
};

} // namespace smartpark
