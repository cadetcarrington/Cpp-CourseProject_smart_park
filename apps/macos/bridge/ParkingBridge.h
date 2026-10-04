#pragma once

#include "core/model/ParkingLayout.h"
#include "core/service/ParkingInsightEngine.h"
#include "core/service/ParkingService.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QString>

namespace smartpark{
class Persistence;
}

// C++ 业务核心与 AppKit UI 之间的数据桥。
// 持有 Persistence + ParkingService，向 ViewController 暴露只读仪表盘指标。
// 后续页面（车位地图/车辆作业/停车记录等）共享同一个 bridge 实例。
class ParkingBridge{
public:
    ParkingBridge();
    explicit ParkingBridge(const QString &databasePath);
    ~ParkingBridge();

    ParkingBridge(const ParkingBridge &) = delete;
    ParkingBridge &operator=(const ParkingBridge &) = delete;

    int totalSpots() const noexcept;
    int occupiedSpots() const noexcept;
    int reservedSpots() const noexcept;
    int remainingSpots() const noexcept;
    int recordCount() const noexcept;
    double totalRevenue() const noexcept;
    const smartpark::ParkingLayout &layout() const noexcept;
    const std::vector<smartpark::ParkingSpot> &spots() const noexcept;
    const std::vector<smartpark::ParkingRecord> &records() const noexcept;
    const std::vector<smartpark::Booking> &bookings() const noexcept;
    smartpark::ParkingInsights insights() const noexcept;
    double pendingDeposits() const noexcept;
    double forfeitedDeposits() const noexcept;
    bool ready() const noexcept;
    const std::string &lastError() const noexcept;

    // 车辆作业写操作
    std::optional<smartpark::AllocationResult> enterVehicle(
        const std::string &plate, smartpark::VehicleType type);
    std::optional<smartpark::AllocationResult> emergencyEnter(
        const std::string &plate, smartpark::VehicleType type);
    std::optional<smartpark::ParkingRecord> leaveVehicle(const std::string &plate);
    bool updateVehicleType(const std::string &plate, smartpark::VehicleType type);
    void setStrategy(smartpark::AllocationStrategy strategy);

    // 预约写操作
    std::optional<smartpark::BookingResult> bookVehicle(
        const std::string &plate, smartpark::VehicleType type,
        smartpark::ParkingRecord::TimePoint arrival);
    std::optional<smartpark::AllocationResult> confirmBooking(const std::string &plate);
    bool cancelBooking(const std::string &plate);

    // 计费规则
    smartpark::BillingRule billingRule() const;
    smartpark::BookingPolicy bookingPolicy() const;

    // ---- 布局编辑 ----
    // 当前生效的布局描述文本：初始为内置车库布局，成功应用自定义布局后同步更新。
    const std::string &layoutDescription() const noexcept;

    // 解析并应用自定义布局描述（语法见 ParkingLayout::fromDescription）。
    // 失败返回 false 并填充 error：
    //   - *needsDatabaseReset == false：布局语法/语义错误，原布局与数据保持不变；
    //   - *needsDatabaseReset == true ：历史停车数据与新布局不兼容，需要调用方
    //     确认后改调 resetDatabaseAndApplyLayout()。
    bool applyLayoutDescription(const std::string &description, std::string *error,
                                bool *needsDatabaseReset);

    // 重置停车业务数据后应用布局，保留同库的用户账号。仅在用户明确
    // 确认后调用；失败时 error 说明原因。
    bool resetDatabaseAndApplyLayout(const std::string &description, std::string *error);

    // 数据库不可用而降级为纯内存模式时为 true：此时布局写入会「成功」但不落库，
    // 重启即丢失。调用方据此如实提示，不要谎报已保存。
    bool memoryOnly() const noexcept;

private:
    // 用给定布局重建 ParkingService；失败时保留原有 service_（旧布局继续可用）。
    bool rebuildService(const smartpark::ParkingLayout &layout, std::string *error);
    bool clearParkingData(std::string *error);

    std::unique_ptr<smartpark::Persistence> persistence_;
    std::unique_ptr<smartpark::ParkingService> service_;
    std::string lastError_;
    std::string databasePath_;
    std::string layoutText_;
    // 布局重建后需要恢复用户选择的分配策略。
    smartpark::AllocationStrategy strategy_{smartpark::AllocationStrategy::WeightedCost};
    bool databaseFailed_{false};
};
