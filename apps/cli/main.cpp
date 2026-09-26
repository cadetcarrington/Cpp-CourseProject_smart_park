#include "core/model/ParkingLayout.h"
#include "core/model/Vehicle.h"
#include "core/persistence/Persistence.h"
#include "core/service/AuditLogService.h"
#include "core/service/DemoDirector.h"
#include "core/service/AnalyticsEngine.h"
#include "core/service/ParkingService.h"
#include "core/service/RemoteAnalystClient.h"
#include "core/service/ReservationService.h"
#include <QCoreApplication>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace{
const char *usageText =
    "usage: smartpark_cli [layout.txt] [--db <path>] [--reset] [预约命令]\n"
    "  layout.txt   自定义停车场布局文本文件，缺省使用内置 60 车位布局\n"
    "  --db <path>  SQLite 数据库文件路径，缺省使用用户数据目录 smartpark/smartpark.db\n"
    "  --reset      启动前删除数据库文件，保证从空库开始\n"
    "预约命令（指定任一命令时只执行预约流程，不运行自动演示）：\n"
    "  --book <plate>           预约车位，到场时间默认 60 分钟后\n"
    "  --checkin <plate>        预约到场确认，转入停车并退回定金\n"
    "  --cancel <plate>         取消预约（须在到场时间之前），退定金并释放车位\n"
    "  --bookings               列出全部预约记录与定金统计\n"
    "  --expire-bookings        结算爽约预约（超过宽限期未到场的没收定金）\n"
    "时段预约命令（SmartPark 0.7 远程时间段预约，含定金支付与冲突检查）：\n"
    "  --reserve <plate>        创建时段预约，开始时间默认 120 分钟后\n"
    "  --arrive <plate>         时段预约到场确认，转入停车，定金转预付\n"
    "  --rsv-cancel <plate>     取消时段预约（须在开始之前），定金退回\n"
    "  --reservations           列出全部时段预约与定金流水统计\n"
    "数据分析命令：\n"
    "  --analyze                对当前数据运行本地分析模型并输出结论\n"
    "应急与寻车：\n"
    "  --emergency <plate>      应急车辆入场（优先出口最近车位，满场自动让位）\n"
    "  --find <plate>           反向寻车（步行路线）\n"
    "审计命令：\n"
    "  --audit                  最近审计日志\n"
    "  --audit-verify           校验审计哈希链完整性\n"
    "演示命令：\n"
    "  --demo-script            剧本式一键演示（虚拟时钟）\n"
    "  --settle-reservations    执行延迟锁位与爽约结算扫描\n"
    "  --duration <minutes>     时段预约时长（分钟，默认 120）\n"
    "时间参数（对预约命令生效）：\n"
    "  --at \"YYYY-MM-DD HH:MM\"  绝对基准时间（默认当前时间）\n"
    "  --in <minutes>           相对基准时间的分钟偏移（默认 0）\n"
    "  --type <name>            车辆类型 car|motorcycle|truck|electric（默认 car）\n"
    "示例：\n"
    "  smartpark_cli --db p.db --book 晋A12345 --in 90\n"
    "  smartpark_cli --db p.db --checkin 晋A12345 --in 90\n"
    "  smartpark_cli --db p.db --cancel 晋A12345\n"
    "  smartpark_cli --db p.db --bookings\n"
    "  smartpark_cli --db p.db --reserve 晋B12345 --in 120 --duration 180\n"
    "  smartpark_cli --db p.db --arrive 晋B12345 --in 120\n"
    "  smartpark_cli --db p.db --reservations\n"
    "  --help       显示本帮助\n";
struct Options{
    std::string layoutPath;
    std::string databasePath;
    bool resetDatabase{false};
    std::string bookPlate;
    std::string checkinPlate;
    std::string cancelPlate;
    std::string reservePlate;
    std::string arrivePlate;
    std::string rsvCancelPlate;
    std::string atText;
    int inMinutes{-1};
    int durationMinutes{-1};
    std::string vehicleTypeText;
    bool listBookings{false};
    bool expireNow{false};
    bool listReservations{false};
    bool settleReservationsNow{false};
    bool analyzeNow{false};
    bool accessibleReserve{false};
    bool auditList{false};
    bool auditVerify{false};
    bool demoScript{false};
    std::string emergencyPlate;
    std::string findPlate;
    bool hasBookingCommand() const{
        return !bookPlate.empty() || !checkinPlate.empty() || !cancelPlate.empty()
            || listBookings || expireNow;
    }
    bool hasReservationCommand() const{
        return !reservePlate.empty() || !arrivePlate.empty() || !rsvCancelPlate.empty()
            || listReservations || settleReservationsNow;
    }
    bool hasAnalyticsCommand() const{ return analyzeNow; }
    bool hasOpsCommand() const{
        return !emergencyPlate.empty() || !findPlate.empty() || auditList
            || auditVerify || demoScript;
    }
};
Options parseOptions(int argc, char **argv){
    Options options;
    for (int index = 1; index < argc; ++index){
        const std::string argument = argv[index];
        if (argument == "--db"){
            if (index + 1 >= argc){
                throw std::runtime_error("--db requires a path");
            }
            options.databasePath = argv[++index];
        } else if (argument == "--reset"){
            options.resetDatabase = true;
        } else if (argument == "--book" || argument == "--checkin"
                   || argument == "--cancel" || argument == "--reserve"
                   || argument == "--arrive" || argument == "--rsv-cancel"){
            if (index + 1 >= argc){
                throw std::runtime_error(argument + " requires a plate number");
            }
            const std::string plate = argv[++index];
            if (argument == "--book"){
                options.bookPlate = plate;
            } else if (argument == "--checkin"){
                options.checkinPlate = plate;
            } else if (argument == "--cancel"){
                options.cancelPlate = plate;
            } else if (argument == "--reserve"){
                options.reservePlate = plate;
            } else if (argument == "--arrive"){
                options.arrivePlate = plate;
            } else{
                options.rsvCancelPlate = plate;
            }
        } else if (argument == "--at"){
            if (index + 1 >= argc){
                throw std::runtime_error("--at requires a time like \"YYYY-MM-DD HH:MM\"");
            }
            options.atText = argv[++index];
        } else if (argument == "--in"){
            if (index + 1 >= argc){
                throw std::runtime_error("--in requires minutes");
            }
            const std::string minutes = argv[++index];
            try{
                options.inMinutes = std::stoi(minutes);
            } catch (const std::exception &){
                throw std::runtime_error("--in requires an integer: " + minutes);
            }
            if (options.inMinutes < 0){
                throw std::runtime_error("--in must be >= 0: " + minutes);
            }
        } else if (argument == "--type"){
            if (index + 1 >= argc){
                throw std::runtime_error("--type requires a name");
            }
            options.vehicleTypeText = argv[++index];
        } else if (argument == "--duration"){
            if (index + 1 >= argc){
                throw std::runtime_error("--duration requires minutes");
            }
            const std::string minutes = argv[++index];
            try{
                options.durationMinutes = std::stoi(minutes);
            } catch (const std::exception &){
                throw std::runtime_error("--duration requires an integer: " + minutes);
            }
            if (options.durationMinutes <= 0){
                throw std::runtime_error("--duration must be > 0: " + minutes);
            }
        } else if (argument == "--bookings"){
            options.listBookings = true;
        } else if (argument == "--expire-bookings"){
            options.expireNow = true;
        } else if (argument == "--reservations"){
            options.listReservations = true;
        } else if (argument == "--settle-reservations"){
            options.settleReservationsNow = true;
        } else if (argument == "--analyze"){
            options.analyzeNow = true;
        } else if (argument == "--emergency"){
            if (index + 1 >= argc){
                throw std::runtime_error("--emergency requires a plate number");
            }
            options.emergencyPlate = argv[++index];
        } else if (argument == "--find"){
            if (index + 1 >= argc){
                throw std::runtime_error("--find requires a plate number");
            }
            options.findPlate = argv[++index];
        } else if (argument == "--accessible"){
            options.accessibleReserve = true;
        } else if (argument == "--audit"){
            options.auditList = true;
        } else if (argument == "--audit-verify"){
            options.auditVerify = true;
        } else if (argument == "--demo-script"){
            options.demoScript = true;
        } else if (!argument.empty() && argument[0] == '-'){
            throw std::runtime_error("unknown option: " + argument);
        } else if (options.layoutPath.empty()){
            options.layoutPath = argument;
        } else{
            throw std::runtime_error("unexpected argument: " + argument);
        }
    }
    return options;
}
void removeDatabaseFile(const std::string &path){
    for (const std::string &suffix : std::initializer_list<std::string>{std::string(), "-wal", "-shm"}){
        if (std::remove((path + suffix).c_str()) != 0 && errno != ENOENT){
            throw std::runtime_error("cannot remove database file: " + path + suffix);
        }
    }
}
smartpark::ParkingLayout loadLayout(const std::string &path){
    if (path.empty()){
        return smartpark::ParkingLayout::defaultLayout();
    }
    std::ifstream input(path);
    if (!input){
        throw std::runtime_error("cannot open layout file: " + path);
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return smartpark::ParkingLayout::fromDescription(buffer.str());
}
const smartpark::ParkingSpot *findSpot(const smartpark::ParkingService &service,
                                       const std::string &spotId){
    const auto spot = std::find_if(
        service.spots().begin(), service.spots().end(),
        [&spotId](const smartpark::ParkingSpot &item) { return item.identifier() == spotId; });
    if (spot == service.spots().end()){
        return nullptr;
    }
    return &(*spot);
}
void printAllocation(const smartpark::ParkingService &service,
                     const smartpark::AllocationResult &result){
    const smartpark::ParkingSpot *spot = findSpot(service, result.spotId);
    std::cout << "  停车位置: " << result.spotId
              << " | 类型: " << (spot != nullptr ? toString(spot->type()) : "unknown")
              << " | 入口距离: " << result.entryRoute.distance << "m"
              << " | 出口距离: " << result.exitRoute.distance << "m"
              << " | 转弯次数: " << (result.entryRoute.turnCount + result.exitRoute.turnCount)
              << " | 附近已占用车位: " << result.nearbyOccupiedSpots
              << " | 得分: " << result.score << '\n';
    std::cout << "  详细信息: 入口路径成本=" << result.breakdown.entryPathCost
              << " 出口路径成本=" << result.breakdown.exitPathCost
              << " 车道拥堵成本=" << result.breakdown.laneCongestionCost
              << " 分区压力成本=" << result.breakdown.zonePressureCost
              << " 转弯次数成本=" << result.breakdown.turnCountCost
              << " 类型惩罚=" << result.breakdown.typePenalty
              << " | 入口门: in#" << result.entranceIndex
              << " 出口门: out#" << result.exitIndex << '\n';
    if (!result.entryRoute.points.empty()){
        const smartpark::Point &first = result.entryRoute.points.front();
        const smartpark::Point &last = result.entryRoute.points.back();
        std::cout << "  Entry route: (" << first.x << ", " << first.y << ") -> ("
                  << last.x << ", " << last.y << ") through "
                  << result.entryRoute.points.size() << " waypoints\n";
    }
}
void printDuration(std::chrono::seconds duration){
    const auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
    const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration - hours);
    std::cout << hours.count() << "h " << minutes.count() << "m";
}
void printBillingRule(const smartpark::BillingRule &rule){
    std::cout << "  计费规则: 免费 " << rule.freeDuration.count() << " 分钟"
              << " | 计费单元 " << rule.billingUnit.count() << " 分钟"
              << " | 首单元 " << rule.minimumFee << " 元"
              << " | 后续每单元 " << rule.unitFee << " 元"
              << " | 单次封顶 " << rule.dailyCap << " 元\n";
}
int activeRecordCount(const smartpark::ParkingService &service){
    return static_cast<int>(std::count_if(
        service.records().begin(), service.records().end(),
        [](const smartpark::ParkingRecord &record) { return !record.isClosed(); }));
}
std::string uniquePlate(const std::string &prefix){
    static int sequence = 0;
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count();
    std::ostringstream plate;
    plate << prefix << (milliseconds % 1000000) << (sequence++ % 10);
    return plate.str();
}
std::string formatMoney(double value){
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << value;
    return stream.str();
}
void printAllocationBrief(const smartpark::ParkingService &service,
                          const smartpark::AllocationResult &result,
                          int sequence){
    const smartpark::ParkingSpot *spot = findSpot(service, result.spotId);
    std::cout << "  入场 #" << sequence << " " << result.plateNumber
              << " -> " << result.spotId
              << " | 类型: " << (spot != nullptr ? toString(spot->type()) : "unknown")
              << " | 入口: " << result.entryRoute.distance << "m"
              << " | 出口: " << result.exitRoute.distance << "m"
              << " | 得分: " << result.score << '\n';
}
void printClosedRecord(const smartpark::ParkingRecord &record, int sequence){
    std::cout << "  离场 #" << sequence << " " << record.plateNumber()
              << " | 车位: " << record.spotId()
              << " | 时长: ";
    printDuration(record.duration());
    std::cout << " | 费用: " << formatMoney(record.fee()) << " 元\n";
}
const char *bookingStatusText(smartpark::BookingStatus status){
    switch (status){
    case smartpark::BookingStatus::Booked:
        return "已预约";
    case smartpark::BookingStatus::CheckedIn:
        return "已到场";
    case smartpark::BookingStatus::NoShow:
        return "爽约";
    case smartpark::BookingStatus::Cancelled:
        return "已取消";
    }
    return "未知";
}
void printBookingRoute(const smartpark::AllocationResult &result){
    std::cout << "  预期路线: 入口门 in#" << result.entranceIndex
              << " -> " << result.spotId
              << " | 入口距离 " << result.entryRoute.distance << "m"
              << " | 出口门 out#" << result.exitIndex
              << " | 出口距离 " << result.exitRoute.distance << "m"
              << " | 转弯次数 " << (result.entryRoute.turnCount + result.exitRoute.turnCount)
              << " | 得分 " << result.score << '\n';
    if (!result.entryRoute.points.empty()){
        const smartpark::Point &first = result.entryRoute.points.front();
        const smartpark::Point &last = result.entryRoute.points.back();
        std::cout << "  入口路线: (" << first.x << ", " << first.y << ") -> ("
                  << last.x << ", " << last.y << ") through "
                  << result.entryRoute.points.size() << " waypoints\n";
    }
}
std::optional<smartpark::VehicleType> parseVehicleTypeText(const std::string &text){
    if (text.empty() || text == "car"){
        return smartpark::VehicleType::Car;
    }
    if (text == "motorcycle"){
        return smartpark::VehicleType::Motorcycle;
    }
    if (text == "truck"){
        return smartpark::VehicleType::Truck;
    }
    if (text == "electric"){
        return smartpark::VehicleType::Electric;
    }
    return std::nullopt;
}
std::optional<smartpark::ParkingRecord::TimePoint> parseArrivalTime(
    const std::string &text){
    std::tm parts{};
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    input >> std::get_time(&parts, "%Y-%m-%d %H:%M");
    if (input.fail()){
        return std::nullopt;
    }
#ifdef _WIN32
    const std::time_t epoch = _mkgmtime(&parts);
#else
    const std::time_t epoch = timegm(&parts);
#endif
    if (epoch == static_cast<std::time_t>(-1)){
        return std::nullopt;
    }
    return std::chrono::system_clock::from_time_t(epoch);
}
std::string formatBookingTime(smartpark::Booking::TimePoint time){
    const std::time_t epoch = smartpark::Booking::Clock::to_time_t(time);
    std::tm parts{};
#ifdef _WIN32
    if (gmtime_s(&parts, &epoch) != 0){
        return "invalid-time";
    }
#else
    gmtime_r(&epoch, &parts);
#endif
    char buffer[32];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &parts) == 0){
        return "invalid-time";
    }
    return buffer;
}
bool bookCommand(smartpark::ParkingService &service, const Options &options,
                 const smartpark::ParkingRecord::TimePoint &effectiveTime,
                 bool arrivalSpecified){
    const auto type = parseVehicleTypeText(options.vehicleTypeText);
    if (!type){
        std::cerr << "预约失败: 未知车辆类型 \"" << options.vehicleTypeText
                  << "\"（可用: car | motorcycle | truck | electric）\n";
        return false;
    }
    const smartpark::ParkingRecord::TimePoint arrival =
        arrivalSpecified
            ? effectiveTime
            : smartpark::ParkingRecord::Clock::now() + std::chrono::minutes(60);
    const smartpark::Vehicle vehicle(options.bookPlate, *type);
    const auto booked = service.createBooking(
        vehicle, arrival, smartpark::ParkingRecord::Clock::now());
    if (!booked){
        std::cerr << "预约失败: 车牌 " << options.bookPlate
                  << " 无法预约（车牌已在场或已有生效预约、到场时间需在当前时间之后且不超过 "
                  << service.bookingPolicy().advanceDays << " 天、或无可用车位）\n";
        return false;
    }
    std::cout << "预约成功: " << booked->booking.id()
              << " 车牌 " << booked->booking.plateNumber()
              << " | 车位 " << booked->booking.spotId()
              << " | 到场时间 " << formatBookingTime(booked->booking.arrivalTime())
              << " | 宽限截止 " << formatBookingTime(booked->booking.arrivalDeadline())
              << " | 定金 " << formatMoney(booked->booking.deposit())
              << " 元 | 状态 " << bookingStatusText(booked->booking.status()) << '\n';
    printBookingRoute(booked->allocation);
    return true;
}
bool checkinCommand(smartpark::ParkingService &service, const Options &options,
                    const smartpark::ParkingRecord::TimePoint &effectiveTime){
    const auto arrived = service.confirmBooking(options.checkinPlate, effectiveTime);
    if (!arrived){
        std::cerr << "到场确认失败: 车牌 " << options.checkinPlate
                  << " 没有可确认的预约（未预约、已取消、未到到场时间或已超过宽限期）\n";
        return false;
    }
    std::cout << "到场确认成功: 车牌 " << options.checkinPlate
              << " 转入停车，占用车位 " << arrived->spotId
              << "，定金退回（离场时按计费规则结算）\n";
    return true;
}
bool cancelCommand(smartpark::ParkingService &service, const Options &options,
                   const smartpark::ParkingRecord::TimePoint &effectiveTime){
    if (!service.cancelBooking(options.cancelPlate, effectiveTime)){
        std::cerr << "取消失败: 车牌 " << options.cancelPlate
                  << " 没有可取消的预约（取消须在预约到场时间之前）。\n";
        return false;
    }
    std::cout << "取消成功: 车牌 " << options.cancelPlate
              << " 预约已取消，定金退回，车位已释放。\n";
    return true;
}
void listBookingsCommand(const smartpark::ParkingService &service){
    const auto &bookings = service.bookings();
    std::cout << "预约记录: " << bookings.size() << " 条\n";
    for (const smartpark::Booking &booking : bookings){
        std::cout << "  " << booking.id()
                  << " | 车牌 " << booking.plateNumber()
                  << " | 车位 " << booking.spotId()
                  << " | 创建 " << formatBookingTime(booking.createdAt())
                  << " | 到场 " << formatBookingTime(booking.arrivalTime())
                  << " | 截止 " << formatBookingTime(booking.arrivalDeadline())
                  << " | 定金 " << formatMoney(booking.deposit())
                  << " | 状态 " << bookingStatusText(booking.status()) << '\n';
    }
    std::cout << "待结算定金: " << formatMoney(service.pendingDeposits())
              << " 元 | 爽约没收定金: " << formatMoney(service.forfeitedDeposits())
              << " 元\n";
}
const char *reservationStatusText(smartpark::ReservationStatus status){
    switch (status){
    case smartpark::ReservationStatus::PendingPayment:
        return "待支付";
    case smartpark::ReservationStatus::Confirmed:
        return "已确认";
    case smartpark::ReservationStatus::CheckedIn:
        return "已到场";
    case smartpark::ReservationStatus::Completed:
        return "已完成";
    case smartpark::ReservationStatus::Cancelled:
        return "已取消";
    case smartpark::ReservationStatus::NoShow:
        return "爽约";
    case smartpark::ReservationStatus::Expired:
        return "已过期";
    }
    return "未知";
}
const char *depositStateText(smartpark::DepositState state){
    switch (state){
    case smartpark::DepositState::Pending:
        return "待结算";
    case smartpark::DepositState::Refunded:
        return "已退回";
    case smartpark::DepositState::Forfeited:
        return "已没收";
    case smartpark::DepositState::Applied:
        return "已抵扣";
    }
    return "未知";
}
bool reserveCommand(smartpark::ParkingService &service, const Options &options,
                    const smartpark::ParkingRecord::TimePoint &effectiveTime,
                    bool startSpecified, bool accessible){
    const auto type = parseVehicleTypeText(options.vehicleTypeText);
    if (!type){
        std::cerr << "预约失败: 未知车辆类型 \"" << options.vehicleTypeText
                  << "\"（可用: car | motorcycle | truck | electric）\n";
        return false;
    }
    const auto start =
        startSpecified
            ? effectiveTime
            : smartpark::ParkingRecord::Clock::now() + std::chrono::minutes(120);
    const auto end =
        start + std::chrono::minutes(options.durationMinutes < 0 ? 120
                                                                 : options.durationMinutes);
    const smartpark::Vehicle vehicle(options.reservePlate, *type);
    const auto created = service.reservations().create(
        vehicle, start, end, smartpark::ParkingRecord::Clock::now(), accessible);
    if (!created){
        std::cerr << "时段预约失败: " << service.reservations().lastError() << '\n';
        return false;
    }
    std::cout << "时段预约成功" << (accessible ? "（无障碍关怀：免定金、宽限翻倍）" : "")
              << ": " << created->reservation.id()
              << " 车牌 " << created->reservation.plateNumber()
              << " | 车位 " << created->reservation.spotId()
              << " | " << formatBookingTime(created->reservation.startTime())
              << " ~ " << formatBookingTime(created->reservation.endTime())
              << " | 宽限截止 " << formatBookingTime(created->reservation.graceDeadline())
              << " | 定金 " << formatMoney(created->reservation.deposit())
              << " 元已收取\n";
    printBookingRoute(created->allocation);
    return true;
}
bool arriveCommand(smartpark::ParkingService &service, const Options &options,
                   const smartpark::ParkingRecord::TimePoint &effectiveTime){
    const auto arrived = service.reservations().checkIn(options.arrivePlate,
                                                        effectiveTime);
    if (!arrived){
        std::cerr << "到场确认失败: " << service.reservations().lastError() << '\n';
        return false;
    }
    std::cout << "到场确认成功: 车牌 " << options.arrivePlate
              << " 转入停车，占用预约车位 " << arrived->spotId
              << "，定金转为停车预付款（离场时抵扣）\n";
    return true;
}
bool rsvCancelCommand(smartpark::ParkingService &service, const Options &options,
                      const smartpark::ParkingRecord::TimePoint &effectiveTime){
    if (!service.reservations().cancel(options.rsvCancelPlate, effectiveTime)){
        std::cerr << "取消失败: " << service.reservations().lastError() << '\n';
        return false;
    }
    std::cout << "取消成功: 车牌 " << options.rsvCancelPlate
              << " 时段预约已取消，定金已退回。\n";
    return true;
}
void listReservationsCommand(const smartpark::ParkingService &service){
    const auto &reservations = service.reservations().reservations();
    std::cout << "时段预约记录: " << reservations.size() << " 条\n";
    for (const smartpark::Reservation &reservation : reservations){
        std::cout << "  " << reservation.id()
                  << " | 车牌 " << reservation.plateNumber()
                  << " | 车位 " << reservation.spotId()
                  << " | " << formatBookingTime(reservation.startTime())
                  << " ~ " << formatBookingTime(reservation.endTime())
                  << " | 宽限截止 " << formatBookingTime(reservation.graceDeadline())
                  << " | 定金 " << formatMoney(reservation.deposit())
                  << " 元（" << depositStateText(reservation.depositState())
                  << "） | 状态 " << reservationStatusText(reservation.status()) << '\n';
    }
    std::cout << "待结算定金: " << formatMoney(service.reservations().heldDeposits())
              << " 元 | 已抵扣: " << formatMoney(service.reservations().appliedDeposits())
              << " 元 | 已退回: " << formatMoney(service.reservations().refundedDeposits())
              << " 元 | 爽约没收: " << formatMoney(service.reservations().forfeitedDeposits())
              << " 元 | 定金流水: " << service.reservations().payments().size()
              << " 笔\n";
}
} // namespace
int main(int argc, char **argv){
    QCoreApplication application(argc, argv);
    try{
        if (argc == 2
            && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")){
            std::cout << usageText;
            return 0;
        }
        const Options options = parseOptions(argc, argv);
        const smartpark::ParkingLayout layout = loadLayout(options.layoutPath);
        std::string databasePath = options.databasePath;
        if (databasePath.empty()){
            const QString defaultPath = smartpark::Persistence::defaultDatabasePath();
            if (!defaultPath.isEmpty()){
                databasePath = defaultPath.toStdString();
            }
        }
        if (options.resetDatabase && !databasePath.empty()){
            removeDatabaseFile(databasePath);
        }
        std::unique_ptr<smartpark::Persistence> persistence;
        if (!databasePath.empty()){
            persistence = std::make_unique<smartpark::Persistence>(
                QString::fromStdString(databasePath));
        }
        std::unique_ptr<smartpark::AuditLogService> auditService;
        if (persistence != nullptr){
            auditService = std::make_unique<smartpark::AuditLogService>(
                persistence->databaseManager().database());
        }
        smartpark::ParkingService service(
            layout, smartpark::AllocationStrategy::WeightedCost,
            persistence != nullptr ? &persistence->repository() : nullptr);
        if (auditService){
            service.setAuditLog(auditService.get());
        }
        std::cout << "SmartPark CLI - 自动泊车分配\n"
                  << "停车场大小: " << layout.siteWidth() << "m x " << layout.siteHeight()
                  << "m | 区域数: " << layout.regions().size()
                  << " | 入口数: " << layout.entrances().size()
                  << " | 出口数: " << layout.exits().size()
                  << " | 车位数: " << layout.spots().size() << "\n";
        printBillingRule(service.billing().rule());
        if (persistence != nullptr){
            std::cout << "数据库: " << databasePath
                      << " | 已恢复占用车位: " << service.occupiedSpots()
                      << " | 已恢复保留车位: " << service.reservedSpots()
                      << " | 活跃记录数: " << activeRecordCount(service) << "\n";
        } else{
            std::cout << "数据库: 禁用 (内存演示)\n";
        }
        std::cout << "\n";
        if (options.demoScript){
            smartpark::DemoDirector director(service);
            std::cout << "===== 剧本式演示（共 " << director.totalSteps() << " 步） =====\n";
            while (director.step()){
                std::cout << "  [" << director.stepsDone() << "/"
                          << director.totalSteps() << "] "
                          << director.lastDescription() << '\n';
            }
            if (!director.lastAnalysisSummary().empty()){
                std::cout << "  分析结论: " << director.lastAnalysisSummary() << '\n';
            }
            std::cout << "\nRESULT: PASS\n";
            return 0;
        }
        if (!options.emergencyPlate.empty()){
            const smartpark::Vehicle vehicle(options.emergencyPlate,
                                             smartpark::VehicleType::Car);
            const auto arrived = service.emergencyEnter(vehicle, true);
            if (!arrived){
                std::cerr << "应急入场失败: " << options.emergencyPlate << '\n';
                return 1;
            }
            std::cout << "应急生命通道: " << options.emergencyPlate << " 占用 "
                      << arrived->spotId << "（出口距离 " << arrived->exitRoute.distance
                      << "m）\nRESULT: PASS\n";
            return 0;
        }
        if (!options.findPlate.empty()){
            const auto found = service.findCar(options.findPlate);
            if (!found){
                std::cerr << "反向寻车失败: 车牌 " << options.findPlate
                          << " 不在场内\n";
                return 1;
            }
            std::cout << "反向寻车: " << found->plateNumber << " 停在 "
                      << found->zone << " 区 " << found->spotId
                      << "\n  步行路线: 从锚点 #" << found->anchorIndex << " ("
                      << found->anchor.x << ", " << found->anchor.y << ") 出发，步行 "
                      << found->walkRoute.distance << " 米，途经 "
                      << found->walkRoute.points.size() << " 个路点\nRESULT: PASS\n";
            return 0;
        }
        if (options.auditList || options.auditVerify){
            if (persistence == nullptr){
                std::cerr << "审计命令需要 --db 数据库\n";
                return 1;
            }
            smartpark::AuditLogService audit(persistence->databaseManager().database());
            if (options.auditVerify){
                const auto result = audit.verifyChain();
                std::cout << "审计哈希链: " << (result.ok ? "完整" : "断链")
                          << " | 校验记录 " << result.checked << " 条";
                if (!result.ok){
                    std::cout << " | 首个断链 id=" << result.brokenAtId;
                }
                std::cout << "\n";
            } else{
                const auto entries = audit.recent(50);
                std::cout << "审计日志（最近 " << entries.size() << " 条）:\n";
                for (const auto &entry : entries){
                    std::cout << "  #" << entry.id << " " << entry.actor
                              << " " << entry.action
                              << (entry.detail.empty() ? "" : " | " + entry.detail)
                              << "\n";
                }
            }
            std::cout << "\nRESULT: PASS\n";
            return 0;
        }
        if (options.hasAnalyticsCommand()){
            smartpark::AnalyticsEngine engine(service);
            const auto report = engine.analyze();
            std::cout << "===== 数据分析报告（模型 " << report.model << "） =====\n"
                      << "结论: " << report.summary << "\n发现:\n";
            for (const smartpark::AnalysisFinding &finding : report.findings){
                std::cout << "  - [" << smartpark::AnalysisReport::categoryText(finding.category)
                          << "] " << finding.title << "：" << finding.detail << '\n';
            }
            std::cout << "建议:\n";
            int recommendationIndex = 1;
            for (const std::string &recommendation : report.recommendations){
                std::cout << "  " << recommendationIndex++ << ". " << recommendation << '\n';
            }
            std::cout << "占用率预测:";
            for (const auto &point : report.forecast){
                std::cout << " +" << point.first << "h " << std::fixed
                          << std::setprecision(0) << point.second << "%";
            }
            std::cout << std::setprecision(2) << "\n";
            if (std::getenv("SMARTPARK_ANALYST_ENDPOINT") != nullptr){
                std::cout << "远程分析接口: 端点已配置，等待网络层注入传输后启用（P1）。\n";
            } else{
                std::cout << "远程分析接口: 预留中（设置 SMARTPARK_ANALYST_ENDPOINT 并接入传输后启用）。\n";
            }
            std::cout << "\nRESULT: PASS\n";
            return 0;
        }
        if (options.hasOpsCommand() || options.hasAnalyticsCommand()
            || options.hasBookingCommand() || options.hasReservationCommand()){
            std::optional<smartpark::ParkingRecord::TimePoint> base;
            if (!options.atText.empty()){
                base = parseArrivalTime(options.atText);
                if (!base){
                    std::cerr << "RESULT: FAIL - --at 时间格式应为 \"YYYY-MM-DD HH:MM\"，收到 \""
                              << options.atText << "\"\n";
                    return 1;
                }
            }
            const std::chrono::minutes offset(options.inMinutes < 0 ? 0 : options.inMinutes);
            const smartpark::ParkingRecord::TimePoint effectiveTime =
                (base.has_value() ? *base : smartpark::ParkingRecord::Clock::now())
                + offset;
            const bool arrivalSpecified = base.has_value() || options.inMinutes >= 0;
            bool ok = true;
            if (options.settleReservationsNow){
                service.reservations().sweep(effectiveTime);
                std::cout << "已执行延迟锁位与爽约结算扫描: 预约记录 "
                          << service.reservations().reservations().size()
                          << " 条 | 待结算定金 "
                          << formatMoney(service.reservations().heldDeposits())
                          << " 元 | 爽约没收定金 "
                          << formatMoney(service.reservations().forfeitedDeposits())
                          << " 元\n";
            }
            if (!options.reservePlate.empty()){
                ok = reserveCommand(service, options, effectiveTime,
                                    arrivalSpecified, options.accessibleReserve) && ok;
            }
            if (!options.arrivePlate.empty()){
                ok = arriveCommand(service, options, effectiveTime) && ok;
            }
            if (!options.rsvCancelPlate.empty()){
                ok = rsvCancelCommand(service, options, effectiveTime) && ok;
            }
            if (options.listReservations){
                listReservationsCommand(service);
            }
            if (options.expireNow){
                service.expireBookings(effectiveTime);
                std::cout << "已结算爽约预约: 预约记录 " << service.bookings().size()
                          << " 条 | 爽约没收定金 "
                          << formatMoney(service.forfeitedDeposits()) << " 元\n";
            }
            if (!options.bookPlate.empty()){
                ok = bookCommand(service, options, effectiveTime, arrivalSpecified) && ok;
            }
            if (!options.checkinPlate.empty()){
                ok = checkinCommand(service, options, effectiveTime) && ok;
            }
            if (!options.cancelPlate.empty()){
                ok = cancelCommand(service, options, effectiveTime) && ok;
            }
            if (options.listBookings){
                listBookingsCommand(service);
            }
            std::cout << "\nRESULT: " << (ok ? "PASS" : "FAIL") << "\n";
            return ok ? 0 : 1;
        }
        // 每次运行生成唯一车牌，配合持久化可重复执行；停车场满时给出有效输出。
        const smartpark::ParkingRecord::TimePoint entryTime =
            smartpark::ParkingRecord::Clock::now();
        if (service.remainingSpots() == 0){
            std::cout << "Parking lot is full; allocation demo skipped.\n"
                      << "\nRESULT: PASS\n";
            return 0;
        }
        constexpr int initialVehicleCount = 30;
        constexpr int exitVehicleCount = 15;
        constexpr int reentryVehicleCount = 8;
        const smartpark::VehicleType demoTypes[] ={
            smartpark::VehicleType::Car,
            smartpark::VehicleType::Electric,
            smartpark::VehicleType::Truck,
            smartpark::VehicleType::Motorcycle,
        };
        std::vector<smartpark::AllocationResult> allocations;
        allocations.reserve(initialVehicleCount + reentryVehicleCount);
        int enteredCount = 0;
        int exitedCount = 0;
        const auto allocateOne = [&](const std::string &platePrefix,
                                     smartpark::VehicleType type){
            if (service.remainingSpots() == 0){
                return false;
            }
            const smartpark::Vehicle vehicle(uniquePlate(platePrefix), type);
            const auto result = service.enter(vehicle, entryTime);
            if (!result){
                std::cout << "  No suitable spot for " << vehicle.plateNumber()
                          << "; skip it.\n";
                return false;
            }
            ++enteredCount;
            printAllocationBrief(service, *result, enteredCount);
            allocations.push_back(*result);
            return true;
        };
        std::cout << "批量入场: 计划 " << initialVehicleCount << " 辆\n";
        for (int index = 0; index < initialVehicleCount; ++index){
            if (!allocateOne(u8"晋A", demoTypes[index % 4])){
                std::cout << "停车场已满，停止批量入场。\n";
                break;
            }
        }
        const int plannedExits = std::min(
            exitVehicleCount, static_cast<int>(allocations.size()));
        std::cout << "\n批量离场: 计划 " << plannedExits << " 辆\n";
        for (int index = 0; index < plannedExits; ++index){
            const int parkedMinutes = 35 + (index % 6) * 25;
            const auto closedRecord = service.leave(
                allocations[index].plateNumber,
                entryTime + std::chrono::minutes(parkedMinutes));
            if (!closedRecord){
                throw std::runtime_error("leave failed");
            }
            ++exitedCount;
            printClosedRecord(*closedRecord, exitedCount);
        }
        std::cout << "\n二次入场: 计划 " << reentryVehicleCount << " 辆\n";
        for (int index = 0; index < reentryVehicleCount; ++index){
            if (!allocateOne(u8"晋D", demoTypes[(index + 1) % 4])){
                std::cout << "停车场已满，停止二次入场。\n";
                break;
            }
        }
        std::cout << "\n===== 预约系统演示 =====\n";
        printBillingRule(service.billing().rule());
        std::cout << "  预约策略: 定金 " << formatMoney(service.bookingPolicy().deposit)
                  << " 元 | 最多提前 " << service.bookingPolicy().advanceDays
                  << " 天 | 到场宽限 " << service.bookingPolicy().gracePeriod.count()
                  << " 分钟\n";
        {
            const smartpark::Vehicle bookingVehicle(u8"晋Y10001", smartpark::VehicleType::Car);
            const auto arrivalTime = entryTime + std::chrono::minutes(120);
            const auto booked = service.createBooking(bookingVehicle, arrivalTime, entryTime);
            if (booked){
                std::cout << "  远程预约: " << booked->booking.id()
                          << " 车牌 " << booked->booking.plateNumber()
                          << " | 车位 " << booked->booking.spotId()
                          << " | 定金 " << formatMoney(booked->booking.deposit())
                          << " 元 | 状态 " << bookingStatusText(booked->booking.status()) << '\n';
                printBookingRoute(booked->allocation);
                const auto arrived = service.confirmBooking(bookingVehicle.plateNumber(), arrivalTime);
                if (arrived){
                    std::cout << "  到场确认: 车牌 " << bookingVehicle.plateNumber()
                              << " 转入停车，占用车位 " << arrived->spotId
                              << "，定金退回\n";
                } else{
                    std::cout << "  到场确认失败，预约未转为停车。\n";
                }
            } else{
                std::cout << "  远程预约演示跳过（无可用车位或车牌已占用）。\n";
            }
        }
        {
            const smartpark::Vehicle noShowVehicle(u8"晋Y10002", smartpark::VehicleType::Electric);
            const auto arrivalTime = entryTime + std::chrono::minutes(180);
            const auto booked = service.createBooking(noShowVehicle, arrivalTime, entryTime);
            if (booked){
                service.expireBookings(arrivalTime
                                       + service.bookingPolicy().gracePeriod
                                       + std::chrono::minutes(1));
                std::cout << "  爽约示例: 预约 " << booked->booking.id()
                          << " 车牌 " << booked->booking.plateNumber()
                          << " 超过宽限期未到场，没收定金 "
                          << formatMoney(booked->booking.deposit()) << " 元\n";
            } else{
                std::cout << "  爽约演示跳过（无可用车位或车牌已占用）。\n";
            }
        }
        std::cout << "  预约记录: " << service.bookings().size()
                  << " 条 | 待结算定金: " << formatMoney(service.pendingDeposits())
                  << " 元 | 爽约没收定金: " << formatMoney(service.forfeitedDeposits())
                  << " 元\n";
        std::cout << "\n===== 远程时间段预约演示（SmartPark 0.7） =====\n"
                  << "  预约规则: 定金 " << formatMoney(service.reservations().rule().deposit)
                  << " 元 | 可提前 " << service.reservations().rule().maxAdvanceDays
                  << " 天 | 最短提前 " << service.reservations().rule().minLeadTime.count()
                  << " 分钟 | 到场宽限 " << service.reservations().rule().gracePeriod.count()
                  << " 分钟\n";
        {
            const smartpark::Vehicle rsvVehicle(u8"晋Z20001", smartpark::VehicleType::Car);
            const auto startTime = entryTime + std::chrono::minutes(120);
            const auto endTime = startTime + std::chrono::minutes(180);
            const auto created = service.reservations().create(
                rsvVehicle, startTime, endTime, entryTime);
            if (created){
                std::cout << "  时段预约: " << created->reservation.id()
                          << " 车牌 " << created->reservation.plateNumber()
                          << " | 车位 " << created->reservation.spotId()
                          << " | " << formatBookingTime(created->reservation.startTime())
                          << " ~ " << formatBookingTime(created->reservation.endTime())
                          << " | 定金 " << formatMoney(created->reservation.deposit())
                          << " 元已收取\n";
                printBookingRoute(created->allocation);
                // 延迟锁位：进入到场窗口（开始前 30 分钟）后物理车位才被短时锁定。
                service.reservations().sweep(startTime - std::chrono::minutes(15));
                const auto arrived = service.reservations().checkIn(
                    rsvVehicle.plateNumber(), startTime);
                if (arrived){
                    std::cout << "  到场确认: 占用预约车位 " << arrived->spotId
                              << "，定金转停车预付款\n";
                    const double appliedBefore =
                        service.reservations().appliedDeposits();
                    const auto closed = service.leave(rsvVehicle.plateNumber(), endTime);
                    const double applied =
                        service.reservations().appliedDeposits() - appliedBefore;
                    if (closed){
                        std::cout << "  离场结算: 时长 ";
                        printDuration(closed->duration());
                        std::cout << " | 定金抵扣 " << formatMoney(applied)
                                  << " 元 | 实收停车费 " << formatMoney(closed->fee())
                                  << " 元\n";
                    }
                } else{
                    std::cout << "  到场确认失败: " << service.reservations().lastError()
                              << '\n';
                }
            } else{
                std::cout << "  时段预约演示跳过: " << service.reservations().lastError()
                          << '\n';
            }
        }
        {
            const smartpark::Vehicle noShowVehicle(u8"晋Z20002",
                                                   smartpark::VehicleType::Electric);
            const auto startTime = entryTime + std::chrono::minutes(120);
            const auto created = service.reservations().create(
                noShowVehicle, startTime, startTime + std::chrono::minutes(120),
                entryTime);
            if (created){
                service.reservations().sweep(
                    startTime + service.reservations().rule().gracePeriod
                    + std::chrono::minutes(1));
                std::cout << "  爽约示例: 预约 " << created->reservation.id()
                          << " 车牌 " << created->reservation.plateNumber()
                          << " 超过宽限期未到场，没收定金 "
                          << formatMoney(created->reservation.deposit()) << " 元\n";
            } else{
                std::cout << "  爽约演示跳过: " << service.reservations().lastError()
                          << '\n';
            }
        }
        std::cout << "  时段预约: " << service.reservations().reservations().size()
                  << " 条 | 待结算定金: "
                  << formatMoney(service.reservations().heldDeposits())
                  << " 元 | 已抵扣: " << formatMoney(service.reservations().appliedDeposits())
                  << " 元 | 已退回: " << formatMoney(service.reservations().refundedDeposits())
                  << " 元 | 爽约没收: "
                  << formatMoney(service.reservations().forfeitedDeposits())
                  << " 元\n";
        std::cout << "\n===== 结算汇总 =====\n"
                  << "  本次新入场: " << enteredCount << " 辆\n"
                  << "  本次离场: " << exitedCount << " 辆\n"
                  << "  当前在场: " << service.occupiedSpots() << " 辆\n"
                  << "  历史记录总数: " << service.records().size() << " 条\n"
                  << "  累计停车费: " << formatMoney(service.totalRevenue())
                  << " 元\n";
        std::cout << "\nRemaining spots: " << service.remainingSpots()
                  << "/" << service.spots().size()
                  << " | Occupied: " << service.occupiedSpots()
                  << " | Reserved: " << service.reservedSpots()
                  << " | Records: " << service.records().size() << '\n';
        if (options.layoutPath.empty() && layout.spots().size() != 60){
            throw std::runtime_error("default layout must contain 60 spots");
        }
        std::cout << "\nRESULT: PASS\n";
        return 0;
    } catch (const std::exception &error){
        std::cerr << "RESULT: FAIL - " << error.what() << '\n';
        return 1;
    }
}
