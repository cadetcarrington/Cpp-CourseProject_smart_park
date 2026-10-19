#include "ParkingBridge.h"

#include "bridge/RemoteDataSource.h"

#include "core/model/Booking.h"
#include "core/model/ParkingLayout.h"
#include "core/model/ParkingRecord.h"
#include "core/persistence/Persistence.h"
#include "core/service/ParkingInsightEngine.h"
#include "core/service/ParkingService.h"
#include "core/service/ReservationService.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>
#include <QTimer>

#include <QJsonArray>
#include <QJsonValue>

#include <algorithm>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cmath>
#include <cstddef>
#include <ctime>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <utility>

namespace{
// 解析布局描述；ParkingLayout 没有可访问的默认构造函数，故用 optional 承载。
// 解析失败时返回 nullopt 并把原因写入 error。
std::optional<smartpark::ParkingLayout> parseLayout(const std::string &description,
                                                    std::string *error){
    try{
        return smartpark::ParkingLayout::fromDescription(description);
    } catch (const std::exception &failure){
        if (error != nullptr){
            *error = failure.what();
        }
        return std::nullopt;
    }
}
} // namespace

ParkingBridge::ParkingBridge()
    : ParkingBridge(smartpark::Persistence::defaultDatabasePath()){}

ParkingBridge::ParkingBridge(const QString &databasePath)
    : ParkingBridge(databasePath, QString()){}

ParkingBridge::ParkingBridge(const QString &databasePath,
                             const QString &layoutDescription){
    databasePath_ = databasePath.toStdString();
    layoutText_ = smartpark::ParkingLayout::garageDescription();

    smartpark::ParkingLayout layout = smartpark::ParkingLayout::garageLayout();
    if (!layoutDescription.trimmed().isEmpty()){
        try{
            layout = smartpark::ParkingLayout::fromDescription(
                layoutDescription.toStdString());
            // 编辑框/导出用同一份文本：用户看到的必须是这条库实际生效的布局。
            layoutText_ = layoutDescription.toStdString();
        } catch (const std::exception &failure){
            lastError_ = std::string("布局文件无效：") + failure.what();
            std::fprintf(stderr, "[ParkingBridge] invalid layout: %s\n",
                         failure.what());
            return;
        }
    }

    std::string error;
    if (!rebuildService(layout, &error)){
        // 数据库不可用：沿用「不提供 service_」的语义，lastError_ 供上层展示。
        lastError_ = error;
        std::fprintf(stderr, "[ParkingBridge] persistence error: %s\n",
                     error.c_str());
    }
}

ParkingBridge::~ParkingBridge() = default;

bool ParkingBridge::rebuildService(const smartpark::ParkingLayout &layout,
                                   std::string *error){
    if (databasePath_.empty() || databaseFailed_){
        service_ = std::make_unique<smartpark::ParkingService>(layout, strategy_);
        return true;
    }
    try{
        if (!persistence_){
            persistence_ = std::make_unique<smartpark::Persistence>(
                QString::fromStdString(databasePath_));
        }
        if (!persistence_->lastError().isEmpty()){
            throw std::runtime_error(persistence_->lastError().toStdString());
        }
        // 先构造成功再替换 service_：任一步抛异常时旧 service_ 仍然有效，
        // 用户可以继续使用原布局与原数据。
        auto rebuilt = std::make_unique<smartpark::ParkingService>(
            layout, strategy_, &persistence_->repository());
        service_ = std::move(rebuilt);
        return true;
    } catch (const std::exception &failure){
        if (error != nullptr){
            *error = failure.what();
        }
        return false;
    }
}

bool ParkingBridge::clearParkingData(std::string *error){
    if (!persistence_){
        if (error != nullptr){
            *error = "停车数据库连接不可用";
        }
        return false;
    }
    QSqlDatabase &database = persistence_->databaseManager().database();
    if (!database.transaction()){
        if (error != nullptr){
            *error = database.lastError().text().toStdString();
        }
        return false;
    }
    // 账号和停车数据共库，只清理业务数据，保留已注册用户。
    for (const QString &table : {QStringLiteral("deposit_payments"),
                                 QStringLiteral("reservations"),
                                 QStringLiteral("bookings"),
                                 QStringLiteral("parking_records"),
                                 QStringLiteral("parking_spots"),
                                 QStringLiteral("layout_snapshot")}){
        QSqlQuery query(database);
        if (!query.exec(QStringLiteral("DELETE FROM ") + table)){
            if (error != nullptr){
                *error = query.lastError().text().toStdString();
            }
            database.rollback();
            return false;
        }
    }
    if (!database.commit()){
        if (error != nullptr){
            *error = database.lastError().text().toStdString();
        }
        database.rollback();
        return false;
    }
    return true;
}

const std::string &ParkingBridge::layoutDescription() const noexcept{
    if (remote_){
        return remote_->layoutDescription();
    }
    return layoutText_;
}

bool ParkingBridge::memoryOnly() const noexcept{
    if (remote_){
        return remote_->memoryOnly();
    }
    return databaseFailed_ || databasePath_.empty();
}

bool ParkingBridge::applyLayoutDescription(const std::string &description,
                                           std::string *error,
                                           bool *needsDatabaseReset){
    if (needsDatabaseReset != nullptr){
        *needsDatabaseReset = false;
    }
    const auto layout = parseLayout(description, error);
    if (!layout){
        return false;
    }
    std::string buildError;
    if (rebuildService(*layout, &buildError)){
        layoutText_ = description;
        lastError_.clear();
        return true;
    }
    // 语法没问题，但库里的停车数据无法迁移到新布局：交由调用方确认是否重置。
    if (needsDatabaseReset != nullptr){
        *needsDatabaseReset = true;
    }
    if (error != nullptr){
        *error = buildError;
    }
    return false;
}

bool ParkingBridge::resetDatabaseAndApplyLayout(const std::string &description,
                                                std::string *error){
    const auto layout = parseLayout(description, error);
    if (!layout){
        return false;
    }

    if (!clearParkingData(error)){
        return false;
    }
    service_.reset();
    // 必须连 Persistence 一起重建：ParkingRepository 会把上次失败的原因留在
    // lastError_（例如 "persisted layout differs from the current layout"），
    // 而 restore() 只要看到 lastError_ 非空就抛「恢复失败」。复用同一连接会让
    // 已经成功的重置被误判为失败，并连带把 bridge 打成内存模式。
    persistence_.reset();
    databaseFailed_ = false;
    std::string buildError;
    if (!rebuildService(*layout, &buildError)){
        databaseFailed_ = true;
        service_ = std::make_unique<smartpark::ParkingService>(*layout, strategy_);
        layoutText_ = description;
        if (error != nullptr){
            *error = "历史停车记录与预约已清空，但重建停车服务失败：" + buildError +
                     "（已降级为内存模式，重启后不会保留）";
        }
        return false;
    }
    layoutText_ = description;
    lastError_.clear();
    return true;
}

double ParkingBridge::totalRevenue() const noexcept{
    if (remote_){
        return remote_->totalRevenue();
    }
    return service_ ? service_->totalRevenue() : 0.0;
}

void ParkingBridge::clearPlannedRoute(){
    plannedRoute_ = PlannedRoute{};
}

const ParkingBridge::PlannedRoute &ParkingBridge::plannedRoute() const noexcept{
    return plannedRoute_;
}

std::vector<ParkingBridge::PendingReservation>
ParkingBridge::pendingReservations() const{
    std::vector<PendingReservation> pending;
    const std::vector<smartpark::ParkingSpot> &spots = this->spots();
    for (const smartpark::Reservation &reservation : reservations()){
        // 只取「已确认/待支付、还没到场」的订单：已到场（CheckedIn）的车位是
        // Occupied，锁定后的车位是 Reserved，这两种车位图上本来就看得见。
        if (reservation.status() != smartpark::ReservationStatus::Confirmed
            && reservation.status() != smartpark::ReservationStatus::PendingPayment){
            continue;
        }
        const auto spot = std::find_if(
            spots.begin(), spots.end(), [&reservation](const smartpark::ParkingSpot &item){
                return item.identifier() == reservation.spotId();
            });
        if (spot != spots.end()
            && (spot->status() == smartpark::SpotStatus::Reserved
                || spot->status() == smartpark::SpotStatus::Occupied)){
            continue;   // 已经锁位/被占：不再重复标注
        }
        PendingReservation item;
        item.spotId = reservation.spotId();
        item.plate = reservation.plateNumber();
        item.vehicleType = reservation.vehicleType();
        item.start = reservation.startTime();
        item.end = reservation.endTime();
        item.accessible = reservation.isAccessible();
        pending.push_back(std::move(item));
    }
    return pending;
}

std::vector<std::string> ParkingBridge::parkedPlateNumbers() const{
    std::vector<std::string> plates;
    for (const smartpark::ParkingRecord &record : records()){
        const std::string &plate = record.plateNumber();
        if (record.isClosed() || plate.empty()){
            continue;
        }
        plates.push_back(plate);
    }
    std::sort(plates.begin(), plates.end());
    plates.erase(std::unique(plates.begin(), plates.end()), plates.end());
    return plates;
}

namespace{
// 车道规划网络：四向移动（折线只可能横平竖直）+ 靠车道中间（离障碍越近代价越高），
// 除目标车位外的车位都当障碍物。入场/出场路线与反向寻车的步行路线共用这一套，
// 车位图上画出来的线因此都是沿车道走的，不会斜穿车位。
struct LaneNetwork{
    smartpark::GridPlanner planner;
    smartpark::OccupancyField field;
    smartpark::Rectangle targetBounds{};
};

LaneNetwork buildLaneNetwork(const smartpark::ParkingLayout &layout,
                             const std::vector<smartpark::ParkingSpot> &spots,
                             const std::string &targetSpotId){
    std::vector<smartpark::Rectangle> obstacles;
    obstacles.reserve(layout.obstacles().size() + spots.size());
    for (const smartpark::LayoutObstacle &item : layout.obstacles()){
        obstacles.push_back(item.bounds);
    }
    smartpark::Rectangle targetBounds{};
    bool hasTarget = false;
    for (const smartpark::ParkingSpot &item : spots){
        if (item.identifier() == targetSpotId){
            targetBounds = item.bounds();
            hasTarget = true;
            continue;
        }
        obstacles.push_back(item.bounds());
    }
    if (!hasTarget){
        targetBounds = smartpark::Rectangle{};
    }

    const double cellSize = 0.5;
    const int columnCount = std::max(1, static_cast<int>(std::ceil(layout.siteWidth() / cellSize)));
    const int rowCount = std::max(1, static_cast<int>(std::ceil(layout.siteHeight() / cellSize)));
    const double margin = 3.0;   // 只统计 3 米以内的障碍物距离
    const int maxCost = 8;       // 单格加价上限（weight 0.6 → 最贵处代价 ×5.8）
    const std::size_t cellCount = static_cast<std::size_t>(columnCount) * static_cast<std::size_t>(rowCount);
    std::vector<double> nearest(cellCount, 1e9);
    auto stampNearest = [&](const smartpark::Rectangle &rect){
        const int minColumn = std::max(0, static_cast<int>(std::floor((rect.origin.x - margin) / cellSize)));
        const int maxColumn = std::min(columnCount - 1,
            static_cast<int>(std::ceil((rect.origin.x + rect.width + margin) / cellSize)));
        const int minRow = std::max(0, static_cast<int>(std::floor((rect.origin.y - margin) / cellSize)));
        const int maxRow = std::min(rowCount - 1,
            static_cast<int>(std::ceil((rect.origin.y + rect.height + margin) / cellSize)));
        for (int row = minRow; row <= maxRow; ++row){
            for (int column = minColumn; column <= maxColumn; ++column){
                const double centerX = (static_cast<double>(column) + 0.5) * cellSize;
                const double centerY = (static_cast<double>(row) + 0.5) * cellSize;
                const double dx = std::max({rect.origin.x - centerX, 0.0,
                                            centerX - (rect.origin.x + rect.width)});
                const double dy = std::max({rect.origin.y - centerY, 0.0,
                                            centerY - (rect.origin.y + rect.height)});
                const double distance = std::hypot(dx, dy);
                if (distance > margin){
                    continue;
                }
                double &slot = nearest[static_cast<std::size_t>(row) * columnCount + column];
                slot = std::min(slot, distance);
            }
        }
    };
    for (const smartpark::Rectangle &rect : obstacles){
        stampNearest(rect);
    }
    // 外墙也参与：路线不会贴着墙根跑
    stampNearest(smartpark::Rectangle{smartpark::Point{0.0, 0.0}, layout.siteWidth(), 0.5});
    stampNearest(smartpark::Rectangle{smartpark::Point{0.0, layout.siteHeight() - 0.5},
                                      layout.siteWidth(), 0.5});
    stampNearest(smartpark::Rectangle{smartpark::Point{0.0, 0.0}, 0.5, layout.siteHeight()});
    stampNearest(smartpark::Rectangle{smartpark::Point{layout.siteWidth() - 0.5, 0.0},
                                      0.5, layout.siteHeight()});
    std::vector<int> clearance(cellCount, 0);
    for (std::size_t index = 0; index < clearance.size(); ++index){
        if (nearest[index] > margin){
            continue;
        }
        const long cost = std::lround((margin - nearest[index]) / cellSize);
        clearance[index] = static_cast<int>(std::min<long>(maxCost, std::max(0L, cost)));
    }

    LaneNetwork network{
        smartpark::GridPlanner(layout.siteWidth(), layout.siteHeight(), {}, obstacles, 0.5, false),
        smartpark::OccupancyField{}, targetBounds};
    network.field.weight = 0.6;
    network.field.occupancy = std::move(clearance);
    return network;
}

// 把点吸附到车位矩形边缘：路线端点落在车位边上，线条不压住车位格子。
smartpark::Point snapToBay(const smartpark::Rectangle &bay, const smartpark::Point &point){
    const double minX = bay.origin.x, maxX = bay.origin.x + bay.width;
    const double minY = bay.origin.y, maxY = bay.origin.y + bay.height;
    smartpark::Point snapped = point;
    snapped.x = std::min(std::max(snapped.x, minX), maxX);
    snapped.y = std::min(std::max(snapped.y, minY), maxY);
    return snapped;
}

} // namespace

void ParkingBridge::setPlannedRoute(const smartpark::Route &entryRoute,
                                    const smartpark::Route &exitRoute){
    plannedRoute_.entryRoute = entryRoute;
    plannedRoute_.exitRoute = exitRoute;
    // 分配器给的是八向路线，直接画会在图上斜穿车道。这里按车道口径重算一遍用于显示：
    // 四向移动 + 靠车道中间 + 除目标车位外都是障碍，端点再吸附到车位边缘上。
    const smartpark::ParkingLayout &current = layout();
    const std::vector<smartpark::ParkingSpot> &spotList = spots();
    smartpark::Point endpoint{};
    bool hasEndpoint = false;
    if (entryRoute.points.size() >= 2){
        endpoint = entryRoute.points.back();
        hasEndpoint = true;
    } else if (exitRoute.points.size() >= 2){
        endpoint = exitRoute.points.front();
        hasEndpoint = true;
    }
    const smartpark::ParkingSpot *targetSpot = nullptr;
    if (hasEndpoint){
        double best = 2.5;
        for (const smartpark::ParkingSpot &item : spotList){
            const smartpark::Point access = item.accessPoint();
            const double distance = std::hypot(access.x - endpoint.x, access.y - endpoint.y);
            if (distance <= best){
                best = distance;
                targetSpot = &item;
            }
        }
    }
    if (targetSpot != nullptr){
        const LaneNetwork lane = buildLaneNetwork(current, spotList, targetSpot->identifier());
        const smartpark::Rectangle &bay = targetSpot->bounds();
        if (entryRoute.points.size() >= 2){
            smartpark::Route shown = lane.planner.plan(
                entryRoute.points.front(), targetSpot->accessPoint(), &lane.field);
            if (shown.points.size() >= 2){
                shown.points.back() = snapToBay(bay, shown.points.back());
                plannedRoute_.entryRoute = shown;
            }
        }
        if (exitRoute.points.size() >= 2){
            smartpark::Route shown = lane.planner.plan(
                targetSpot->accessPoint(), exitRoute.points.back(), &lane.field);
            if (shown.points.size() >= 2){
                shown.points.front() = snapToBay(bay, shown.points.front());
                plannedRoute_.exitRoute = shown;
            }
        }
    }
    // 只有真的有两个点以上才值得画：单点或空路线当作「没有路线」，
    // 免得车位图上留一段看不见的东西，界面还以为画过了。
    plannedRoute_.valid = plannedRoute_.entryRoute.points.size() >= 2
        || plannedRoute_.exitRoute.points.size() >= 2;
}

// 闸机在布局里只有坐标没有名字，按贴墙的一侧取名，状态行里更好读。
std::string gateSideName(const smartpark::ParkingLayout &layout, const smartpark::Point &point){
    const double tolerance = 1.0;
    if (point.x <= tolerance){
        return "西侧";
    }
    if (point.x >= layout.siteWidth() - tolerance){
        return "东侧";
    }
    if (point.y <= tolerance){
        return "北侧";
    }
    if (point.y >= layout.siteHeight() - tolerance){
        return "南侧";
    }
    return "";
}

std::optional<ParkingBridge::CarFinderData> ParkingBridge::findCarForPlate(
        const std::string &plateNumber){
    const std::vector<smartpark::ParkingRecord> &recordList = records();
    const auto record = std::find_if(
        recordList.begin(), recordList.end(),
        [&plateNumber](const smartpark::ParkingRecord &item){
            return item.plateNumber() == plateNumber && !item.isClosed();
        });
    if (record == recordList.end()){
        return std::nullopt;
    }
    const std::vector<smartpark::ParkingSpot> &spotList = spots();
    const auto spot = std::find_if(
        spotList.begin(), spotList.end(),
        [&record](const smartpark::ParkingSpot &item){
            return item.identifier() == record->spotId();
        });
    if (spot == spotList.end()){
        return std::nullopt;
    }

    const smartpark::ParkingLayout &current = layout();
    const smartpark::Point target = spot->accessPoint();

    const LaneNetwork lane = buildLaneNetwork(current, spotList, spot->identifier());

    CarFinderData data;
    data.plateNumber = record->plateNumber();
    data.spotId = spot->identifier();
    data.zone = spot->zone();
    data.target = target;

    // 第一段：只从入口步行进来（出口是车开出去的），取最近的入口。
    double bestEntrance = std::numeric_limits<double>::infinity();
    for (const smartpark::Point &entrance : current.entrances()){
        const smartpark::Route route = lane.planner.plan(entrance, target, &lane.field);
        if (route.points.empty() || route.distance >= bestEntrance){
            continue;
        }
        bestEntrance = route.distance;
        data.entryToCar.points = route.points;
        data.entryToCar.distance = route.distance;
        data.entryToCar.turnCount = route.turnCount;
        data.entranceName = gateSideName(current, entrance) + "入口";
    }
    // 第二段：从车位走到最近的出口。
    double bestExit = std::numeric_limits<double>::infinity();
    for (const smartpark::Point &exit : current.exits()){
        const smartpark::Route route = lane.planner.plan(target, exit, &lane.field);
        if (route.points.empty() || route.distance >= bestExit){
            continue;
        }
        bestExit = route.distance;
        data.carToExit.points = route.points;
        data.carToExit.distance = route.distance;
        data.carToExit.turnCount = route.turnCount;
        data.exitName = gateSideName(current, exit) + "出口";
    }
    if (data.entryToCar.points.empty() && data.carToExit.points.empty()){
        return std::nullopt;
    }
    // 灰线/蓝线都停在车位边缘上（取车点本身可能就在车位外一点点，或者因为
    // 网格取整落进车位里），这样线条不会压住车位格子。
    const smartpark::Rectangle &bay = spot->bounds();
    if (!data.entryToCar.points.empty()){
        const smartpark::Point edge = snapToBay(bay, data.entryToCar.points.back());
        data.entryToCar.points.back() = edge;
        if (!data.carToExit.points.empty()){
            data.carToExit.points.front() = edge;
        }
        data.target = edge;
    } else if (!data.carToExit.points.empty()){
        data.target = snapToBay(bay, data.carToExit.points.front());
        data.carToExit.points.front() = data.target;
    }
    return data;
}

const smartpark::ParkingLayout &ParkingBridge::layout() const noexcept{
    if (remote_){
        return remote_->layout();
    }
    static const smartpark::ParkingLayout empty = smartpark::ParkingLayout::garageLayout();
    return service_ ? service_->layout() : empty;
}

const std::vector<smartpark::ParkingSpot> &ParkingBridge::spots() const noexcept{
    if (remote_){
        return remote_->spots();
    }
    static const std::vector<smartpark::ParkingSpot> empty;
    return service_ ? service_->spots() : empty;
}

const std::vector<smartpark::ParkingRecord> &ParkingBridge::records() const noexcept{
    if (remote_){
        return remote_->records();
    }
    static const std::vector<smartpark::ParkingRecord> empty;
    return service_ ? service_->records() : empty;
}

const std::vector<smartpark::Booking> &ParkingBridge::bookings() const noexcept{
    if (remote_){
        return remote_->bookings();
    }
    static const std::vector<smartpark::Booking> empty;
    return service_ ? service_->bookings() : empty;
}

const std::vector<smartpark::Reservation> &ParkingBridge::reservations() const noexcept{
    if (remote_){
        return remote_->reservations();
    }
    static const std::vector<smartpark::Reservation> empty;
    return service_ ? service_->reservations().reservations() : empty;
}

ParkingBridge::ReservationRuleView ParkingBridge::reservationRule() const noexcept{
    if (remote_){
        return remote_->reservationRule();
    }
    ReservationRuleView view;
    if (service_){
        const smartpark::ReservationRule &rule = service_->reservations().rule();
        view.deposit = rule.deposit;
        view.maxAdvanceDays = rule.maxAdvanceDays;
        view.minLeadTimeMin = static_cast<int>(rule.minLeadTime.count());
        view.minDurationMin = static_cast<int>(rule.minDuration.count());
        view.gracePeriodMin = static_cast<int>(rule.gracePeriod.count());
        view.lockLeadTimeMin = static_cast<int>(rule.lockLeadTime.count());
    }
    return view;
}

void ParkingBridge::recognizePlateRemotely(
    const QByteArray &imageBytes, std::function<void(PlateRecognition)> done){
    if (remote_){
        remote_->recognizePlateRemotely(imageBytes, std::move(done));
        return;
    }
    // 本地模式由界面自己起识别脚本（沿用原有路径），这里不接管。
    done(PlateRecognition{false, {}, 0.0, {}, "本地模式请使用本机识别脚本"});
}

bool ParkingBridge::supportsRemoteRecognition() const noexcept{
    return remote_ != nullptr;
}

smartpark::ParkingInsights ParkingBridge::insights() const noexcept{
    if (remote_){
        return remote_->insights();
    }
    if (!service_){
        return smartpark::ParkingInsights{};
    }
    return smartpark::ParkingInsightEngine::analyze(
        service_->spots(), service_->records(), service_->bookings());
}

double ParkingBridge::pendingDeposits() const noexcept{
    if (remote_){
        return remote_->pendingDeposits();
    }
    return service_ ? service_->pendingDeposits() : 0.0;
}

double ParkingBridge::forfeitedDeposits() const noexcept{
    if (remote_){
        return remote_->forfeitedDeposits();
    }
    return service_ ? service_->forfeitedDeposits() : 0.0;
}

std::optional<smartpark::AllocationResult> ParkingBridge::enterVehicle(
    const std::string &plate, smartpark::VehicleType type){
    if (remote_){
        return remote_->enterVehicle(plate, type);
    }
    if (!service_){
        return std::nullopt;
    }
    return service_->enter(smartpark::Vehicle(plate, type));
}

void ParkingBridge::enterVehicleAsync(
    const std::string &plate, smartpark::VehicleType type, QObject *context,
    std::function<void(smartpark::AsyncEntryOutcome)> done){
    if (!remote_ && service_){
        service_->enterAsync(smartpark::Vehicle(plate, type), context, std::move(done));
        return;
    }
    // 远程模式沿用现有请求/事件泵，路径计算在服务端后台执行。
    QTimer::singleShot(0, context, [this, plate, type, done = std::move(done)]() mutable{
        auto result = enterVehicle(plate, type);
        const std::string error = result ? "" : (lastError().empty()
            ? "入场失败：车辆已在场内或无可用车位" : lastError());
        done({std::move(result), error});
    });
}

std::optional<smartpark::AllocationResult> ParkingBridge::emergencyEnter(
    const std::string &plate, smartpark::VehicleType type){
    if (remote_){
        // 协议里没有应急生命通道 action：远程模式下该能力不可用。
        return remote_->emergencyEnter(plate, type);
    }
    if (!service_){
        return std::nullopt;
    }
    return service_->emergencyEnter(smartpark::Vehicle(plate, type));
}

std::optional<smartpark::ParkingRecord> ParkingBridge::leaveVehicle(const std::string &plate){
    if (remote_){
        return remote_->leaveVehicle(plate);
    }
    if (!service_){
        return std::nullopt;
    }
    return service_->leave(plate);
}

bool ParkingBridge::updateVehicleType(const std::string &plate, smartpark::VehicleType type){
    if (remote_){
        return remote_->updateVehicleType(plate, type);
    }
    return service_ && service_->updateVehicleType(plate, type);
}

void ParkingBridge::setStrategy(smartpark::AllocationStrategy strategy){
    // 记录在 bridge 上：布局重建会构造新的 ParkingService，需要恢复该选择。
    strategy_ = strategy;
    if (remote_){
        return;   // 远程模式下分配策略由服务端统一配置
    }
    if (service_){
        service_->setStrategy(strategy);
    }
}

std::optional<smartpark::ParkingDataSource::ReservationCreation>
ParkingBridge::createReservation(const std::string &plate,
                                 smartpark::VehicleType type,
                                 smartpark::ParkingRecord::TimePoint start,
                                 std::chrono::minutes duration, bool accessible){
    if (remote_){
        return remote_->createReservation(plate, type, start, duration, accessible);
    }
    if (!service_){
        lastError_ = "停车数据服务不可用";
        return std::nullopt;
    }
    auto created = service_->reservations().create(
        smartpark::Vehicle(plate, type), start, start + duration,
        smartpark::ParkingRecord::Clock::now(), accessible);
    if (!created){
        // 规则拒绝的原因直接来自核心（提前量/时长/冲突/已在场内…），
        // 原样上抛，界面不自己编一套说辞。
        lastError_ = service_->reservations().lastError();
        return std::nullopt;
    }
    lastError_.clear();
    ReservationCreation result(created->reservation);
    result.entryRoute = created->allocation.entryRoute;
    result.exitRoute = created->allocation.exitRoute;
    result.entranceIndex = created->allocation.entranceIndex;
    result.exitIndex = created->allocation.exitIndex;
    return result;
}

std::optional<smartpark::Reservation> ParkingBridge::checkInReservation(
    const std::string &plate){
    if (remote_){
        return remote_->checkInReservation(plate);
    }
    if (!service_){
        lastError_ = "停车数据服务不可用";
        return std::nullopt;
    }
    const auto arrived = service_->reservations().checkIn(
        plate, smartpark::ParkingRecord::Clock::now());
    if (!arrived){
        lastError_ = service_->reservations().lastError();
        return std::nullopt;
    }
    lastError_.clear();
    // 到场后订单转 CheckedIn：回读核心里的最新状态，而不是回传请求时的快照。
    return service_->reservations().findCheckedIn(plate);
}

bool ParkingBridge::cancelReservation(const std::string &plate){
    if (remote_){
        return remote_->cancelReservation(plate);
    }
    if (!service_){
        lastError_ = "停车数据服务不可用";
        return false;
    }
    const bool ok = service_->reservations().cancel(
        plate, smartpark::ParkingRecord::Clock::now());
    if (ok){
        lastError_.clear();
    } else{
        lastError_ = service_->reservations().lastError();
    }
    return ok;
}

smartpark::BillingRule ParkingBridge::billingRule() const{
    if (remote_){
        return remote_->billingRule();
    }
    return service_ ? service_->billing().rule() : smartpark::BillingRule{};
}

bool ParkingBridge::ready() const noexcept{
    if (remote_){
        return remote_->ready();
    }
    return service_ != nullptr;
}

const std::string &ParkingBridge::lastError() const noexcept{
    if (remote_){
        return remote_->lastError();
    }
    return lastError_;
}

namespace{
// 本地按记录聚合 7 日收入：与服务端 admin.snapshot 的口径一致
// （仅已离场记录，费用已扣定金）。
std::vector<ParkingBridge::DailyRevenueEntry> localSevenDayRevenue(
    const std::vector<smartpark::ParkingRecord> &records){
    std::vector<ParkingBridge::DailyRevenueEntry> series;
    const auto now = std::chrono::system_clock::now();
    const std::time_t today = std::chrono::system_clock::to_time_t(now);
    std::tm parts{};
    localtime_r(&today, &parts);
    parts.tm_hour = 0;
    parts.tm_min = 0;
    parts.tm_sec = 0;
    const std::time_t midnight = std::mktime(&parts);
    for (int offset = 6; offset >= 0; --offset){
        const std::time_t day = midnight - offset * 86400;
        std::tm dayParts{};
        localtime_r(&day, &dayParts);
        char buffer[16];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &dayParts);
        series.push_back({buffer, 0.0});
    }
    for (const smartpark::ParkingRecord &record : records){
        if (!record.isClosed() || !record.exitTime()){
            continue;
        }
        const std::time_t exit = std::chrono::system_clock::to_time_t(*record.exitTime());
        std::tm exitParts{};
        localtime_r(&exit, &exitParts);
        exitParts.tm_hour = 0;
        exitParts.tm_min = 0;
        exitParts.tm_sec = 0;
        const double daysAgo = std::difftime(midnight, std::mktime(&exitParts)) / 86400.0;
        const int index = 6 - static_cast<int>(std::llround(daysAgo));
        if (index >= 0 && index < 7){
            series[static_cast<std::size_t>(index)].fee += record.fee();
        }
    }
    return series;
}
} // namespace

std::vector<ParkingBridge::DailyRevenueEntry> ParkingBridge::sevenDayRevenue() const{
    if (remote_){
        std::vector<DailyRevenueEntry> series;
        for (const QJsonValue &item : remote_->dailyRevenueSeries()){
            const QJsonObject entry = item.toObject();
            series.push_back({entry.value(QStringLiteral("date")).toString().toStdString(),
                              entry.value(QStringLiteral("fee")).toDouble()});
        }
        if (!series.empty()){
            return series;
        }
        return {};
    }
    return localSevenDayRevenue(records());
}

bool ParkingBridge::connectRemote(const QString &host, quint16 port,
                                  const QString &user, const QString &password){
    if (!remote_){
        remote_ = std::make_unique<smartpark::RemoteDataSource>();
        remote_->onConnectionChanged = [this](bool online, const QString &detail){
            if (onRemoteStateChanged){
                onRemoteStateChanged(online, detail);
            }
        };
        remote_->onSnapshotRefreshed = [this]{
            if (onRemoteDataChanged){
                onRemoteDataChanged();
            }
        };
    }
    remote_->start(host, port, user, password);
    return true;
}

void ParkingBridge::disconnectRemote(){
    if (!remote_){
        return;
    }
    remote_->stop();
    // 释放远程数据源：remoteMode() 变回 false，读写重新落回本地库。
    remote_.reset();
}

bool ParkingBridge::remoteMode() const noexcept{
    return remote_ != nullptr;
}

smartpark::ParkingDataSource::Capabilities ParkingBridge::capabilities() const{
    if (remote_){
        return remote_->capabilities();
    }
    return smartpark::ParkingDataSource::Capabilities{};
}

smartpark::RemoteDataSource *ParkingBridge::remote() const noexcept{
    return remote_.get();
}
