#include "ParkingBridge.h"

#include "core/model/Booking.h"
#include "core/model/ParkingLayout.h"
#include "core/model/ParkingRecord.h"
#include "core/persistence/Persistence.h"
#include "core/service/ParkingInsightEngine.h"
#include "core/service/ParkingService.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QString>

#include <cstddef>
#include <cstdio>
#include <optional>
#include <stdexcept>

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

ParkingBridge::ParkingBridge(const QString &databasePath){
    databasePath_ = databasePath.toStdString();
    layoutText_ = smartpark::ParkingLayout::garageDescription();

    std::string error;
    if (!rebuildService(smartpark::ParkingLayout::garageLayout(), &error)){
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
    return layoutText_;
}

bool ParkingBridge::memoryOnly() const noexcept{
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

int ParkingBridge::totalSpots() const noexcept{
    return service_ ? static_cast<int>(service_->spots().size()) : 0;
}

int ParkingBridge::occupiedSpots() const noexcept{
    return service_ ? service_->occupiedSpots() : 0;
}

int ParkingBridge::reservedSpots() const noexcept{
    return service_ ? service_->reservedSpots() : 0;
}

int ParkingBridge::remainingSpots() const noexcept{
    return service_ ? service_->remainingSpots() : 0;
}

int ParkingBridge::recordCount() const noexcept{
    return service_ ? static_cast<int>(service_->records().size()) : 0;
}

double ParkingBridge::totalRevenue() const noexcept{
    return service_ ? service_->totalRevenue() : 0.0;
}

const smartpark::ParkingLayout &ParkingBridge::layout() const noexcept{
    static const smartpark::ParkingLayout empty = smartpark::ParkingLayout::garageLayout();
    return service_ ? service_->layout() : empty;
}

const std::vector<smartpark::ParkingSpot> &ParkingBridge::spots() const noexcept{
    static const std::vector<smartpark::ParkingSpot> empty;
    return service_ ? service_->spots() : empty;
}

const std::vector<smartpark::ParkingRecord> &ParkingBridge::records() const noexcept{
    static const std::vector<smartpark::ParkingRecord> empty;
    return service_ ? service_->records() : empty;
}

const std::vector<smartpark::Booking> &ParkingBridge::bookings() const noexcept{
    static const std::vector<smartpark::Booking> empty;
    return service_ ? service_->bookings() : empty;
}

smartpark::ParkingInsights ParkingBridge::insights() const noexcept{
    if (!service_){
        return smartpark::ParkingInsights{};
    }
    return smartpark::ParkingInsightEngine::analyze(
        service_->spots(), service_->records(), service_->bookings());
}

double ParkingBridge::pendingDeposits() const noexcept{
    return service_ ? service_->pendingDeposits() : 0.0;
}

double ParkingBridge::forfeitedDeposits() const noexcept{
    return service_ ? service_->forfeitedDeposits() : 0.0;
}

std::optional<smartpark::AllocationResult> ParkingBridge::enterVehicle(
    const std::string &plate, smartpark::VehicleType type){
    if (!service_){
        return std::nullopt;
    }
    return service_->enter(smartpark::Vehicle(plate, type));
}

std::optional<smartpark::AllocationResult> ParkingBridge::emergencyEnter(
    const std::string &plate, smartpark::VehicleType type){
    if (!service_){
        return std::nullopt;
    }
    return service_->emergencyEnter(smartpark::Vehicle(plate, type));
}

std::optional<smartpark::ParkingRecord> ParkingBridge::leaveVehicle(const std::string &plate){
    if (!service_){
        return std::nullopt;
    }
    return service_->leave(plate);
}

bool ParkingBridge::updateVehicleType(const std::string &plate, smartpark::VehicleType type){
    return service_ && service_->updateVehicleType(plate, type);
}

void ParkingBridge::setStrategy(smartpark::AllocationStrategy strategy){
    // 记录在 bridge 上：布局重建会构造新的 ParkingService，需要恢复该选择。
    strategy_ = strategy;
    if (service_){
        service_->setStrategy(strategy);
    }
}

std::optional<smartpark::BookingResult> ParkingBridge::bookVehicle(
    const std::string &plate, smartpark::VehicleType type,
    smartpark::ParkingRecord::TimePoint arrival){
    if (!service_){
        return std::nullopt;
    }
    return service_->createBooking(smartpark::Vehicle(plate, type), arrival);
}

std::optional<smartpark::AllocationResult> ParkingBridge::confirmBooking(const std::string &plate){
    if (!service_){
        return std::nullopt;
    }
    return service_->confirmBooking(plate);
}

bool ParkingBridge::cancelBooking(const std::string &plate){
    return service_ && service_->cancelBooking(plate);
}

smartpark::BillingRule ParkingBridge::billingRule() const{
    return service_ ? service_->billing().rule() : smartpark::BillingRule{};
}

smartpark::BookingPolicy ParkingBridge::bookingPolicy() const{
    return service_ ? service_->bookingPolicy() : smartpark::BookingPolicy{};
}

bool ParkingBridge::ready() const noexcept{
    return service_ != nullptr;
}

const std::string &ParkingBridge::lastError() const noexcept{
    return lastError_;
}
