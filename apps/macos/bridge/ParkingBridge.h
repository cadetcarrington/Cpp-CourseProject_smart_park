#pragma once

#include "bridge/ParkingDataSource.h"
#include "core/model/ParkingLayout.h"
#include "core/service/ParkingInsightEngine.h"
#include "core/service/ParkingService.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QString>

namespace smartpark{
class Persistence;
class RemoteDataSource;
}

// C++ 业务核心与 AppKit UI 之间的数据桥。
//
// 它本身就是 ParkingDataSource 的**本地实现**（持有 Persistence +
// ParkingService，直接读写本机数据库）；调用 connectRemote() 之后切换成
// 委派给 RemoteDataSource（协议 v1 的 admin.snapshot + 广播事件）。
// 两种实现暴露同一组方法，界面只需用 capabilities() 决定哪些入口该隐藏。
//
// 后续页面（车位地图/车辆作业/停车记录等）共享同一个 bridge 实例。
class ParkingBridge : public smartpark::ParkingDataSource{
public:
    ParkingBridge();
    explicit ParkingBridge(const QString &databasePath);
    ~ParkingBridge();

    ParkingBridge(const ParkingBridge &) = delete;
    ParkingBridge &operator=(const ParkingBridge &) = delete;

    // totalSpots / occupiedSpots / reservedSpots / remainingSpots / recordCount
    // 由 ParkingDataSource 依据 spots() / records() 统一派生，两种数据源口径一致。
    double totalRevenue() const noexcept override;
    const smartpark::ParkingLayout &layout() const noexcept override;
    const std::vector<smartpark::ParkingSpot> &spots() const noexcept override;
    const std::vector<smartpark::ParkingRecord> &records() const noexcept override;
    const std::vector<smartpark::Booking> &bookings() const noexcept override;
    smartpark::ParkingInsights insights() const noexcept override;
    double pendingDeposits() const noexcept override;
    double forfeitedDeposits() const noexcept override;
    bool ready() const noexcept override;
    const std::string &lastError() const noexcept override;

    // 车辆作业写操作
    std::optional<smartpark::AllocationResult> enterVehicle(
        const std::string &plate, smartpark::VehicleType type) override;
    std::optional<smartpark::AllocationResult> emergencyEnter(
        const std::string &plate, smartpark::VehicleType type) override;
    std::optional<smartpark::ParkingRecord> leaveVehicle(const std::string &plate) override;
    bool updateVehicleType(const std::string &plate, smartpark::VehicleType type) override;
    void setStrategy(smartpark::AllocationStrategy strategy) override;

    // 预约写操作
    std::optional<smartpark::BookingResult> bookVehicle(
        const std::string &plate, smartpark::VehicleType type,
        smartpark::ParkingRecord::TimePoint arrival) override;
    std::optional<smartpark::AllocationResult> confirmBooking(
        const std::string &plate) override;
    bool cancelBooking(const std::string &plate) override;

    // 计费规则
    smartpark::BillingRule billingRule() const override;
    smartpark::BookingPolicy bookingPolicy() const override;

    // ---- 布局编辑 ----
    // 当前生效的布局描述文本：初始为内置车库布局，成功应用自定义布局后同步更新。
    const std::string &layoutDescription() const noexcept override;

    // 解析并应用自定义布局描述（语法见 ParkingLayout::fromDescription）。
    // 失败返回 false 并填充 error：
    //   - *needsDatabaseReset == false：布局语法/语义错误，原布局与数据保持不变；
    //   - *needsDatabaseReset == true ：历史停车数据与新布局不兼容，需要调用方
    //     确认后改调 resetDatabaseAndApplyLayout()。
    bool applyLayoutDescription(const std::string &description, std::string *error,
                                bool *needsDatabaseReset) override;

    // 重置停车业务数据后应用布局，保留同库的用户账号。仅在用户明确
    // 确认后调用；失败时 error 说明原因。
    bool resetDatabaseAndApplyLayout(const std::string &description,
                                     std::string *error) override;

    // 数据库不可用而降级为纯内存模式时为 true：此时布局写入会「成功」但不落库，
    // 重启即丢失。调用方据此如实提示，不要谎报已保存。
    bool memoryOnly() const noexcept override;

    // 近 7 日（含今天）已结算收入，从早到晚固定 7 项。
    // 本地模式按停车记录聚合；远程模式直接取服务端快照的 dailyRevenue
    // （服务端已按同一口径算好，客户端不必也不该重算）。
    struct DailyRevenueEntry{
        std::string date;   // yyyy-MM-dd
        double fee{0.0};
    };
    std::vector<DailyRevenueEntry> sevenDayRevenue() const;

    // ---- 远程模式 ----
    // 建立到服务端的 TCP 会话并登录。返回是否已发起；登录与连接结果通过
    // onRemoteStateChanged 回调（远程模式下本 bridge 不再读写本地库）。
    bool connectRemote(const QString &host, quint16 port, const QString &user,
                       const QString &password);
    // 断开并回到本地模式。
    void disconnectRemote();
    bool remoteMode() const noexcept;
    // 当前数据源支持哪些能力（远程模式下预约/记录/布局编辑等为 false）。
    smartpark::ParkingDataSource::Capabilities capabilities() const override;
    // 连接状态变化（online=false 时 detail 说明原因）。
    std::function<void(bool online, const QString &detail)> onRemoteStateChanged;
    // 远程快照刷新：界面应重绘。
    std::function<void()> onRemoteDataChanged;
    // 远程会话（本地模式为 nullptr），供界面读取快照时间与分析报告。
    smartpark::RemoteDataSource *remote() const noexcept;

private:
    // 用给定布局重建 ParkingService；失败时保留原有 service_（旧布局继续可用）。
    bool rebuildService(const smartpark::ParkingLayout &layout, std::string *error);
    bool clearParkingData(std::string *error);

    std::unique_ptr<smartpark::RemoteDataSource> remote_;
    std::unique_ptr<smartpark::Persistence> persistence_;
    std::unique_ptr<smartpark::ParkingService> service_;
    std::string lastError_;
    std::string databasePath_;
    std::string layoutText_;
    // 布局重建后需要恢复用户选择的分配策略。
    smartpark::AllocationStrategy strategy_{smartpark::AllocationStrategy::WeightedCost};
    bool databaseFailed_{false};
};
