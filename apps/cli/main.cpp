#include "core/model/ParkingLayout.h"
#include "core/model/Vehicle.h"
#include "core/persistence/Persistence.h"
#include "core/service/ParkingService.h"
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
    "  --at \"YYYY-MM-DD HH:MM\"  绝对基准时间（默认当前时间）\n"
    "  --in <minutes>           相对基准时间的分钟偏移（默认 0）\n"
    "  --type <name>            车辆类型 car|motorcycle|truck|electric（默认 car）\n"
    "示例：\n"
    "  smartpark_cli --db p.db --book 晋A12345 --in 90\n"
    "  smartpark_cli --db p.db --checkin 晋A12345 --in 90\n"
    "  smartpark_cli --db p.db --cancel 晋A12345\n"
    "  smartpark_cli --db p.db --bookings\n"
    "  --help       显示本帮助\n";
struct Options{
    std::string layoutPath;
    std::string databasePath;
    bool resetDatabase{false};
    std::string bookPlate;
    std::string checkinPlate;
    std::string cancelPlate;
    std::string atText;
    int inMinutes{-1};
    std::string vehicleTypeText;
    bool listBookings{false};
    bool expireNow{false};
    bool hasBookingCommand() const{
        return !bookPlate.empty() || !checkinPlate.empty() || !cancelPlate.empty()
            || listBookings || expireNow;
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
                   || argument == "--cancel"){
            if (index + 1 >= argc){
                throw std::runtime_error(argument + " requires a plate number");
            }
            const std::string plate = argv[++index];
            if (argument == "--book"){
                options.bookPlate = plate;
            } else if (argument == "--checkin"){
                options.checkinPlate = plate;
            } else{
                options.cancelPlate = plate;
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
        } else if (argument == "--bookings"){
            options.listBookings = true;
        } else if (argument == "--expire-bookings"){
            options.expireNow = true;
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
    return std::chrono::system_clock::from_time_t(timegm(&parts));
}
std::string formatBookingTime(smartpark::Booking::TimePoint time){
    const std::time_t epoch = smartpark::Booking::Clock::to_time_t(time);
    std::tm parts{};
    gmtime_r(&epoch, &parts);
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
        smartpark::ParkingService service(
            layout, smartpark::AllocationStrategy::WeightedCost,
            persistence != nullptr ? &persistence->repository() : nullptr);
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
        if (options.hasBookingCommand()){
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
