#include "core/model/ParkingLayout.h"
#include "core/model/Vehicle.h"
#include "core/persistence/Persistence.h"
#include "core/service/ParkingService.h"
#include <QCoreApplication>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace{
const char *usageText =
    "usage: smartpark_cli [layout.txt] [--db <path>] [--reset]\n"
    "  layout.txt   自定义停车场布局文本文件，缺省使用内置 60 车位布局\n"
    "  --db <path>  SQLite 数据库文件路径，缺省使用用户数据目录 smartpark/smartpark.db\n"
    "  --reset      启动前删除数据库文件，保证从空库开始\n"
    "  --help       显示本帮助\n";
struct Options{
    std::string layoutPath;
    std::string databasePath;
    bool resetDatabase{false};
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
        // 每次运行生成唯一车牌，配合持久化可重复执行；停车场满时给出有效输出。
        const smartpark::ParkingRecord::TimePoint entryTime =
            smartpark::ParkingRecord::Clock::now();
        if (service.remainingSpots() == 0){
            std::cout << "Parking lot is full; allocation demo skipped.\n"
                      << "\nRESULT: PASS\n";
            return 0;
        }
        const smartpark::VehicleType demoTypes[] ={
            smartpark::VehicleType::Car,
            smartpark::VehicleType::Electric,
            smartpark::VehicleType::Truck,
        };
        std::vector<smartpark::AllocationResult> allocations;
        allocations.reserve(3);
        for (const smartpark::VehicleType type : demoTypes){
            if (service.remainingSpots() == 0){
                std::cout << "Parking lot is full; skip allocating the rest.\n";
                break;
            }
            const smartpark::Vehicle vehicle(uniquePlate(u8"晋A"), type);
            const auto result = service.enter(vehicle, entryTime);
            if (!result){
                std::cout << "No suitable spot for " << vehicle.plateNumber()
                          << "; skip it.\n";
                continue;
            }
            std::cout << "Allocate " << vehicle.plateNumber() << ":\n";
            printAllocation(service, *result);
            allocations.push_back(*result);
        }
        if (allocations.size() >= 2){
            std::cout << "\nRelease " << allocations.front().plateNumber
                      << " and allocate another vehicle.\n";
            const auto closedRecord = service.leave(
                allocations.front().plateNumber, entryTime + std::chrono::minutes(90));
            if (!closedRecord){
                throw std::runtime_error("leave failed");
            }
            std::cout << "  Closed record: " << closedRecord->plateNumber()
                      << " | Spot: " << closedRecord->spotId()
                      << " | Duration: ";
            printDuration(closedRecord->duration());
            std::cout << " | Fee: " << closedRecord->fee() << " yuan\n";
            const auto replacement = service.enter(
                smartpark::Vehicle(uniquePlate(u8"晋C"), smartpark::VehicleType::Car),
                entryTime);
            if (!replacement){
                std::cout << "  Replacement allocation skipped: no suitable spot.\n";
            } else{
                if (replacement->spotId == allocations[1].spotId
                    || (allocations.size() >= 3
                        && replacement->spotId == allocations[2].spotId)){
                    throw std::runtime_error("replacement reused an occupied spot");
                }
                printAllocation(service, *replacement);
            }
        } else if (!allocations.empty()){
            std::cout << "\nOnly one allocation; release it to keep the lot tidy.\n";
            const auto closedRecord = service.leave(
                allocations.front().plateNumber, entryTime + std::chrono::minutes(90));
            if (closedRecord){
                std::cout << "  Closed record: " << closedRecord->plateNumber()
                          << " | Spot: " << closedRecord->spotId()
                          << " | Duration: ";
                printDuration(closedRecord->duration());
                std::cout << " | Fee: " << closedRecord->fee() << " yuan\n";
            }
        } else{
            std::cout << "\nNo allocations performed this run.\n";
        }
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
