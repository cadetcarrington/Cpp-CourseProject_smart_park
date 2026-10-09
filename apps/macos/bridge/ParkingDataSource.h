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
// 之后按广播事件增量刷新，写入走 parking.enter / parking.leave 与
// reservation.create / reservation.checkin / reservation.cancel。
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
    // 网页 H5、用户端 CLI、协议 reservation.* 与管理端「预约管理」页的表单
    // 写的都是这一张表（见文件末尾的写操作），Booking 只剩历史数据。
    struct ReservationRuleView{
        double deposit{0.0};
        int maxAdvanceDays{0};
        int minLeadTimeMin{0};    // 至少提前多久
        int minDurationMin{0};    // 最短预约时长
        int gracePeriodMin{0};    // 到场宽限期（自开始时间起算）
        int lockLeadTimeMin{0};   // 到场窗口可以从开始前多久打开
    };
    virtual const std::vector<smartpark::Reservation> &reservations() const noexcept {
        static const std::vector<smartpark::Reservation> kEmpty;
        return kEmpty;
    }
    virtual ReservationRuleView reservationRule() const noexcept { return {}; }

    // 服务端车牌识别结果。
    struct PlateRecognition{
        bool ok{false};
        std::string plate;
        double confidence{0.0};
        std::string backend;   // script / mock，用来区分真识别与演示结果
        std::string error;
    };
    // 把照片交给服务端识别（异步）。识别要跑检测+OCR，可能几秒，用同步的
    // 嵌套事件循环会把主线程卡成菊花。默认不支持：本地模式自己起脚本。
    virtual void recognizePlateRemotely(
        const QByteArray &imageBytes,
        std::function<void(PlateRecognition)> done){
        (void)imageBytes;
        done(PlateRecognition{false, {}, 0.0, {},
                              "该数据源不支持服务端识别"});
    }
    // 是否支持服务端识别（界面据此决定走远程还是本机脚本）。
    virtual bool supportsRemoteRecognition() const noexcept { return false; }
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
    // 这里刻意不暴露第一版 Booking 的策略（deposit/advanceDays/gracePeriod）：
    // 远程模式下它只能返回本地默认值，界面会照着一份不存在的规则做提示。
    // 当前规则一律走 reservationRule()（本地取核心、远程取快照）。

    // ---- 写操作 ----
    virtual std::optional<AllocationResult> enterVehicle(
        const std::string &plate, VehicleType type) = 0;
    virtual std::optional<AllocationResult> emergencyEnter(
        const std::string &plate, VehicleType type) = 0;
    virtual std::optional<ParkingRecord> leaveVehicle(const std::string &plate) = 0;
    virtual bool updateVehicleType(const std::string &plate, VehicleType type) = 0;
    virtual void setStrategy(AllocationStrategy) {}

    // ---- 时段预约写操作（Reservation，0.7 模型）----
    // 表单提交的是「时间段预约」：网页 H5 / 用户端 CLI / 协议 reservation.*
    // 与管理端共用同一套规则、同一张表。
    //
    // 这里刻意不再提供 Booking（第一版预约）的写入口：它的创建/到场/取消在
    // 远程模式下没有对应的协议 action，基类默认实现只会静默返回 nullopt ——
    // 按钮按不动，还不报错。Booking 仅作为历史数据经 bookings() 只读展示。
    struct ReservationCreation{
        Reservation reservation;   // 下单成功后的订单快照（服务端/核心的事实）
        Route entryRoute;          // 下单时规划好的预期入场路线
        Route exitRoute;
        std::size_t entranceIndex{0};
        std::size_t exitIndex{0};
        // Reservation 没有默认构造：订单快照必须由创建方给出。
        explicit ReservationCreation(Reservation order)
            : reservation(std::move(order)){
        }
    };
    // 创建预约：start 需满足规则的提前量与时长；成功即收定金并返回预期路线。
    // 失败返回 nullopt，原因见 lastError()。
    virtual std::optional<ReservationCreation> createReservation(
        const std::string &, VehicleType, ParkingRecord::TimePoint,
        std::chrono::minutes, bool){
        return std::nullopt;
    }
    // 到场确认：预约转到场、占用预约车位并新建停车记录。
    virtual std::optional<Reservation> checkInReservation(const std::string &){
        return std::nullopt;
    }
    // 取消预约：开始时间之前取消并退回定金。
    virtual bool cancelReservation(const std::string &){ return false; }

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
