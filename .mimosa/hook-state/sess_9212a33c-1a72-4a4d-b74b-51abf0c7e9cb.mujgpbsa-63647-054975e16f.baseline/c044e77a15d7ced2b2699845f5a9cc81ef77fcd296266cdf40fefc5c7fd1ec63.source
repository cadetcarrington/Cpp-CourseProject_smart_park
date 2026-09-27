#include "core/model/ParkingSpot.h"
#include "core/model/Booking.h"
#include "core/model/ParkingRecord.h"
#include "core/persistence/DatabaseManager.h"
#include "core/persistence/ParkingRepository.h"
#include "core/persistence/Persistence.h"
#include "core/util/TimeUtil.h"
#include "core/model/Vehicle.h"
#include "core/service/AnalyticsEngine.h"
#include "core/service/AuditLogService.h"
#include "core/service/DemoDirector.h"
#include "core/service/ParkingService.h"
#include "core/service/RemoteAnalystClient.h"
#include "core/service/ParkingInsightEngine.h"
#include "core/service/ReservationService.h"
#include "core/service/FakePaymentGateway.h"
#include "core/service/Billing.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSqlQuery>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <string>
namespace{
int failureCount = 0;
void expect(bool condition, const std::string &message){
    if (!condition){
        std::cerr << "FAILED: " << message << '\n';
        ++failureCount;
    }
}
template<typename Exception, typename Function>
void expectThrows(Function function, const std::string &message){
    try{
        function();
        expect(false, message);
    } catch (const Exception &){
    } catch (...){
        expect(false, message + " (unexpected exception type)");
    }
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
void testVehicle(){
    const smartpark::Vehicle vehicle(u8"晋A12345", smartpark::VehicleType::Car);
    expect(vehicle.plateNumber() == u8"晋A12345", "vehicle keeps its plate number");
    expect(vehicle.type() == smartpark::VehicleType::Car, "vehicle keeps its type");
    expectThrows<std::invalid_argument>(
        [] { smartpark::Vehicle vehicle("", smartpark::VehicleType::Car); },
        "vehicle rejects an empty plate number");
}
void testParkingSpotLifecycle(){
    smartpark::ParkingSpot spot("A001");
    const smartpark::Vehicle firstVehicle(u8"晋A12345", smartpark::VehicleType::Car);
    const smartpark::Vehicle secondVehicle(u8"晋A88888", smartpark::VehicleType::Electric);
    expect(spot.identifier() == "A001", "parking spot keeps its identifier");
    expect(spot.isAvailable(), "new parking spot is available");
    expect(spot.status() == smartpark::SpotStatus::Available,
           "new parking spot has available status");
    expect(!spot.parkedVehicle().has_value(), "new parking spot has no vehicle");
    expect(spot.occupy(firstVehicle), "available parking spot accepts a vehicle");
    expect(!spot.isAvailable(), "occupied parking spot is unavailable");
    expect(spot.status() == smartpark::SpotStatus::Occupied,
           "occupied parking spot has occupied status");
    expect(spot.parkedVehicle().has_value(), "occupied parking spot keeps its vehicle");
    expect(spot.parkedVehicle()->plateNumber() == firstVehicle.plateNumber(),
           "parking spot keeps the correct vehicle");
    expect(!spot.occupy(secondVehicle), "occupied parking spot rejects another vehicle");
    expect(spot.parkedVehicle()->plateNumber() == firstVehicle.plateNumber(),
           "failed occupancy does not replace the parked vehicle");
    expect(spot.release(), "occupied parking spot can be released");
    expect(spot.isAvailable(), "released parking spot becomes available");
    expect(!spot.parkedVehicle().has_value(), "released parking spot clears its vehicle");
    expect(!spot.release(), "available parking spot cannot be released twice");
    expectThrows<std::invalid_argument>(
        [] { smartpark::ParkingSpot spot(""); },
        "parking spot rejects an empty identifier");
}
void testParkingSpotReservation(){
    using namespace std::chrono_literals;
    smartpark::ParkingSpot spot("A001");
    const smartpark::Vehicle owner(u8"晋A12345", smartpark::VehicleType::Car);
    const smartpark::Vehicle other(u8"晋A88888", smartpark::VehicleType::Car);
    const auto now = smartpark::ParkingSpot::Clock::from_time_t(1000);
    expect(spot.reserve(owner, now + 30s), "available parking spot can be reserved");
    expect(spot.status() == smartpark::SpotStatus::Reserved, "reserved spot has reserved status");
    expect(!spot.isAvailable(), "reserved spot is not available");
    expect(!spot.reserve(other, now + 30s), "reserved spot rejects another reservation");
    expect(!spot.occupy(other), "reserved spot rejects a different vehicle");
    expect(!spot.expireReservation(now + 29s), "reservation stays valid before ttl");
    expect(spot.occupy(owner), "the reserved vehicle can occupy the spot");
    expect(spot.status() == smartpark::SpotStatus::Occupied, "confirmed reservation becomes occupied");
    expect(spot.release(), "occupied reserved spot can be released");
    expect(spot.reserve(owner, now + 30s), "spot can be reserved again");
    expect(spot.expireReservation(now + 30s), "expired reservation is released");
    expect(spot.isAvailable(), "expired reservation returns the spot to available");
    expect(spot.occupy(other), "another vehicle can occupy a spot after ttl");
}
void testParkingRecordLifecycle(){
    using namespace std::chrono_literals;
    const smartpark::ParkingRecord::TimePoint entry =
        smartpark::ParkingRecord::Clock::from_time_t(1700000000);
    smartpark::ParkingRecord record(u8"晋A12345", "A001", entry);
    expect(record.plateNumber() == u8"晋A12345", "parking record keeps the plate number");
    expect(record.spotId() == "A001", "parking record keeps the spot id");
    expect(record.entryTime() == entry, "parking record keeps the entry time");
    expect(!record.isClosed(), "new parking record is active");
    expect(record.duration() >= 0s, "new parking record has a non-negative duration");
    expect(record.fee() == 0.0, "new parking record starts with zero fee");
    expect(!record.close(entry - 1s), "parking record rejects an earlier exit time");
    expect(record.close(entry + 90min, 15.0), "parking record can be closed");
    expect(record.isClosed(), "closed parking record is no longer active");
    expect(record.duration() == 90min, "parking record calculates duration");
    expect(record.fee() == 15.0, "parking record keeps the fee");
    expect(!record.close(entry + 100min), "parking record cannot be closed twice");
    expectThrows<std::invalid_argument>(
        [] { smartpark::ParkingRecord record("", "A001", smartpark::ParkingRecord::Clock::now()); },
        "parking record rejects an empty plate number");
    expectThrows<std::invalid_argument>(
        [] { smartpark::ParkingRecord record(u8"晋A12345", "", smartpark::ParkingRecord::Clock::now()); },
        "parking record rejects an empty spot id");
}

void testBillingService(){
    using namespace std::chrono_literals;
    const smartpark::BillingService billing;

    expect(billing.calculateFee(std::chrono::seconds(-1)) == 0.0,
           "billing rejects negative durations");
    expect(billing.calculateFee(0s) == 0.0,
           "billing charges nothing for zero duration");
    expect(billing.calculateFee(29min + 59s) == 0.0,
           "billing keeps the first thirty minutes free");
    expect(billing.calculateFee(30min) == 0.0,
           "billing keeps the thirtieth minute free");
    expect(billing.calculateFee(30min + 1s) == 5.0,
           "billing charges the first unit after the free period");
    expect(billing.calculateFee(60min) == 5.0,
           "billing charges one unit for exactly thirty paid minutes");
    expect(billing.calculateFee(60min + 1s) == 10.0,
           "billing rounds the second unit up");
    expect(billing.calculateFee(90min) == 10.0,
           "billing charges two units for sixty paid minutes");
    expect(billing.calculateFee(11h) == 100.0,
           "billing applies the daily fee cap");
}
void testDefaultLayout(){
    const smartpark::ParkingLayout layout = smartpark::ParkingLayout::defaultLayout();
    std::set<std::string> identifiers;
    expect(layout.spots().size() == 60, "default layout contains 60 spots");
    expect(layout.regions().size() == 3, "default layout contains three regions");
    expect(layout.entrances().size() == 1 && layout.exits().size() == 1,
           "default layout has a single entrance and exit");
    expect(layout.spots().front().identifier() == "A001", "default layout starts at A001");
    expect(layout.spots().back().identifier() == "A060", "default layout ends at A060");
    for (const smartpark::ParkingSpot &spot : layout.spots()){
        identifiers.insert(spot.identifier());
        expect(spot.bounds().width > 0.0 && spot.bounds().height > 0.0,
               "each generated spot has positive bounds");
        expect(spot.type() == smartpark::SpotType::Normal, "default spots are normal");
    }
    expect(identifiers.size() == layout.spots().size(), "generated spot identifiers are unique");
}
void testCustomLayoutAndAutomaticAllocation(){
    const std::string description =
        "site 80 40\n"
        "entrance 0 20\n"
        "exit 80 20\n"
        "region A 5 5 8 2 1.2 5.5 6 left\n"
        "region B 35 15 8 2 1.4 6.0 6 right\n";
    smartpark::ParkingService service(smartpark::ParkingLayout::fromDescription(description));
    expect(service.spots().size() == 32, "custom layout generates all requested spots");
    const smartpark::ParkingRecord::TimePoint entry =
        smartpark::ParkingRecord::Clock::from_time_t(1700000000);
    const auto first = service.enter({u8"晋A12345", smartpark::VehicleType::Car}, entry);
    const auto second = service.enter({u8"晋A88888", smartpark::VehicleType::Electric}, entry);
    expect(first.has_value() && second.has_value(), "automatic allocation succeeds");
    expect(service.remainingSpots() == 30, "parking service reports remaining spots");
    expect(service.occupiedSpots() == 2, "parking service reports occupied spots");
    expect(service.records().size() == 2, "parking service creates entry records");
    expect(service.activeRecord(u8"晋A12345").has_value(), "parking service tracks active records");
    expect(!service.enter({u8"晋A12345", smartpark::VehicleType::Car}, entry).has_value(),
           "the same vehicle cannot enter twice");
    expect(service.remainingSpots() == 30, "rejected duplicate entry does not consume a spot");
    expect(first->spotId != second->spotId, "automatic allocation does not reuse a spot");
    expect(first->entryRoute.points.size() >= 2 && first->exitRoute.points.size() >= 2,
           "allocation includes entry and exit routes");
    expect(first->entryRoute.distance > 0.0 && first->exitRoute.distance > 0.0,
           "route distances are positive");
    expect(first->breakdown.total == first->score, "allocation score matches the breakdown total");
    const auto firstSpot = findSpot(service, first->spotId);
    expect(firstSpot != nullptr && firstSpot->parkedVehicle().has_value(),
           "allocated spot is occupied");
    const auto closedRecord = service.leave(u8"晋A12345", entry + std::chrono::minutes(90));
    expect(closedRecord.has_value(), "parking service can process vehicle exit");
    expect(closedRecord->duration() == std::chrono::minutes(90),
           "closed record contains parking duration");
    expect(service.remainingSpots() == 31, "exit restores the parking spot");
    expect(!service.activeRecord(u8"晋A12345").has_value(), "exit closes the active record");
}
void testVerticalAislesAndObstacles(){
    const std::string description =
        "site 40 30\n"
        "entrance 8 0\n"
        "exit 32 0\n"
        "region A 10 4 1 4 2.5 5.5 3.0 up charging\n"
        "region B 10 16 2 1 2.4 5.5 3.0 left\n"
        "obstacle 0.5 8 8 10 设备房\n";
    const smartpark::ParkingLayout layout = smartpark::ParkingLayout::fromDescription(description);
    expect(layout.spots().size() == 6, "vertical layout generates stall and side-bay spots");
    expect(layout.obstacles().size() == 1, "layout stores named obstacles");
    expect(layout.obstacles().front().name == "设备房", "obstacle keeps its display name");
    const auto &north = layout.spots().front();
    expect(north.bounds().width > 2.49 && north.bounds().width < 2.51
               && north.bounds().height > 5.49 && north.bounds().height < 5.51,
           "up aisle orients stall length along Y");
    expect(north.accessPoint().y < north.bounds().origin.y,
           "up aisle places the access point on the north side");
    expect(north.type() == smartpark::SpotType::Charging,
           "vertical region still applies spot types");
    const auto &side = layout.spots().back();
    expect(side.bounds().width > 5.49 && side.bounds().width < 5.51
               && side.bounds().height > 2.39 && side.bounds().height < 2.41,
           "left aisle keeps stall length along X");
    expectThrows<std::invalid_argument>(
        []{
            smartpark::ParkingLayout::fromDescription(
                "site 40 30\nentrance 5 0\nexit 35 0\n"
                "region A 10 4 1 3 2.5 5.5 3.0 sideways\n");
        },
        "layout rejects an unknown aisle side");
    expectThrows<std::invalid_argument>(
        []{
            smartpark::ParkingLayout::fromDescription(
                "site 40 30\nentrance 5 0\nexit 35 0\n"
                "region A 10 4 1 3 2.5 5.5 3.0 up\n"
                "obstacle 10 4 4 4 机房\n");
        },
        "layout rejects an obstacle that overlaps a region");
    smartpark::ParkingService service(layout);
    expect(service.spots().size() == 6, "vertical spots are reachable from the north gates");
    const auto parked = service.enter({u8"晋A12345", smartpark::VehicleType::Electric});
    expect(parked.has_value(), "allocator can park into a vertical stall");
}
void testGarageFloorplanLayout(){
    const smartpark::ParkingLayout layout = smartpark::ParkingLayout::garageLayout();
    expect(layout.siteWidth() > 57.9 && layout.siteWidth() < 58.1
               && layout.siteHeight() > 42.3 && layout.siteHeight() < 42.5,
           "garage layout uses the 6F drawing site size");
    expect(layout.entrances().size() == 2 && layout.exits().size() == 1,
           "garage layout has two north entrances and one exit");
    expect(layout.spots().size() == 75, "garage layout converts service rooms into stalls");
    expect(layout.obstacles().size() == 2, "garage layout only keeps the two stair cores");
    expect(layout.obstacles().front().name == u8"楼梯间"
               && layout.obstacles().back().name == u8"楼梯间",
           "remaining obstacles are the west and east stair cores");
    int accessible = 0;
    int charging = 0;
    int vip = 0;
    for (const smartpark::ParkingSpot &spot : layout.spots()){
        if (spot.type() == smartpark::SpotType::Accessible){
            ++accessible;
        } else if (spot.type() == smartpark::SpotType::Charging){
            ++charging;
        } else if (spot.type() == smartpark::SpotType::Vip){
            ++vip;
        }
    }
    expect(accessible == 8 && charging == 15 && vip == 10,
           "garage layout adds accessible, charging and VIP stalls");
    bool hasVertical = false;
    bool hasHorizontal = false;
    for (const smartpark::ParkingSpot &spot : layout.spots()){
        if (spot.bounds().height > spot.bounds().width){
            hasVertical = true;
        }
        if (spot.bounds().width > spot.bounds().height){
            hasHorizontal = true;
        }
    }
    expect(hasVertical && hasHorizontal, "garage layout mixes north-south and east-west stalls");
    smartpark::ParkingService service(layout);
    const auto parked = service.enter({u8"晋A12345", smartpark::VehicleType::Car});
    expect(parked.has_value() && parked->entryRoute.points.size() >= 2,
           "garage layout can allocate a stall and a route");
}
void testLayoutTypesAndMultipleGates(){
    const std::string description =
        "site 90 50\n"
        "entrance 0 8\n"
        "entrance 0 42\n"
        "exit 90 25\n"
        "exit 90 40\n"
        "region A 20 5 3 1 1.2 5.5 6 left charging\n"
        "region B 20 35 3 1 1.2 5.5 6 left type=accessible\n";
    const smartpark::ParkingLayout layout = smartpark::ParkingLayout::fromDescription(description);
    expect(layout.entrances().size() == 2, "layout accepts multiple entrances");
    expect(layout.exits().size() == 2, "layout accepts multiple exits");
    expect(layout.spots().size() == 6, "typed layout generates all requested spots");
    expect(layout.spots().front().type() == smartpark::SpotType::Charging,
           "region type is applied to generated spots");
    expect(layout.spots().back().type() == smartpark::SpotType::Accessible,
           "type= prefix is accepted");
    expectThrows<std::invalid_argument>(
        []{
            smartpark::ParkingLayout::fromDescription(
                "site 40 30\nentrance 0 15\nregion A 10 10 1 1 1.2 5.5 6 left\n");
        },
        "layout rejects a missing exit");
    expectThrows<std::invalid_argument>(
        []{
            smartpark::ParkingLayout::fromDescription(
                "site 40 30\nentrance 0 15\nexit 40 15\nregion A 10 10 1 1 1.2 5.5 6 left hover\n");
        },
        "layout rejects an unknown spot type");
}
void testReservationTtlAndConflict(){
    using namespace std::chrono_literals;
    const std::string description =
        "site 40 30\n"
        "entrance 0 15\n"
        "exit 40 15\n"
        "region A 10 10 1 1 1.2 5.5 6 left\n";
    smartpark::ParkingService service(smartpark::ParkingLayout::fromDescription(description));
    const auto now = smartpark::ParkingRecord::Clock::from_time_t(4000000000);
    const smartpark::Vehicle owner(u8"晋A12345", smartpark::VehicleType::Car);
    const smartpark::Vehicle other(u8"晋A88888", smartpark::VehicleType::Car);
    const auto reserved = service.reserve(owner, now, 30s);
    expect(reserved.has_value(), "allocator can reserve the only available spot");
    expect(service.reservedSpots() == 1, "reserved spots are counted separately");
    expect(service.remainingSpots() == 0, "a reserved spot is no longer remaining");
    expect(!service.enter(other, now).has_value(), "another vehicle cannot take a reserved spot");
    expect(!service.reserve(other, now, 30s).has_value(),
           "another vehicle cannot reserve an already reserved spot");
    const auto confirmed = service.enter(owner, now);
    expect(confirmed.has_value() && confirmed->spotId == reserved->spotId,
           "the reserved vehicle can confirm occupancy");
    expect(service.occupiedSpots() == 1, "confirmed reservation becomes occupied");
    expect(service.leave(owner.plateNumber(), now + 5min).has_value(), "confirmed vehicle can leave");
    const auto again = service.reserve(owner, now + 6min, 30s);
    expect(again.has_value(), "spot can be reserved after it is released");
    service.expireReservations(now + 6min + 29s);
    expect(service.reservedSpots() == 1, "reservation remains before ttl");
    expect(service.enter(other, now + 6min + 30s).has_value(),
           "expired reservations are released on the next operation");
    expect(service.occupiedSpots() == 1, "the next vehicle occupies the expired reservation");
}
void testTypeMatching(){
    const std::string description =
        "site 80 40\n"
        "entrance 0 20\n"
        "exit 80 20\n"
        "region A 12 14 2 1 1.2 5.5 6 left normal\n"
        "region B 28 14 2 1 1.2 5.5 6 left charging\n";
    smartpark::ParkingService service(smartpark::ParkingLayout::fromDescription(description));
    const auto now = smartpark::ParkingRecord::Clock::from_time_t(4000000000);
    const auto car = service.enter({u8"晋A12345", smartpark::VehicleType::Car}, now);
    const auto ev = service.enter({u8"晋A88888", smartpark::VehicleType::Electric}, now);
    expect(car.has_value() && ev.has_value(), "type-aware allocation succeeds");
    const auto carSpot = findSpot(service, car->spotId);
    const auto evSpot = findSpot(service, ev->spotId);
    expect(carSpot != nullptr && carSpot->type() == smartpark::SpotType::Normal,
           "ordinary cars prefer normal spots over charging spots");
    expect(evSpot != nullptr && evSpot->type() == smartpark::SpotType::Charging,
           "electric vehicles prefer charging spots");
}
void testCongestionAvoidanceAndStrategies(){
    const std::string description =
        "site 100 60\n"
        "entrance 0 8\n"
        "exit 100 52\n"
        "region A 36 4 6 2 1.2 5.5 6 left\n"
        "region B 36 40 6 2 1.2 5.5 6 left\n";
    const auto layout = smartpark::ParkingLayout::fromDescription(description);
    const auto now = smartpark::ParkingRecord::Clock::from_time_t(4000000000);
    smartpark::ParkingService nearest(layout, smartpark::AllocationStrategy::Nearest);
    std::optional<smartpark::AllocationResult> lastNearest;
    for (int index = 0; index < 6; ++index){
        lastNearest = nearest.enter(
            {std::string(u8"晋N000") + std::to_string(index), smartpark::VehicleType::Car}, now);
        expect(lastNearest.has_value(), "nearest strategy keeps allocating");
    }
    const auto nearestSpot = findSpot(nearest, lastNearest->spotId);
    expect(nearestSpot != nullptr && nearestSpot->zone() == "A",
           "nearest strategy continues filling the closer cluster");
    smartpark::ParkingService weighted(layout, smartpark::AllocationStrategy::Nearest);
    smartpark::AllocationWeights weights;
    weights.laneCongestion = 8.0;
    weighted.setWeights(weights);
    for (int index = 0; index < 5; ++index){
        expect(weighted.enter(
                   {std::string(u8"晋W000") + std::to_string(index), smartpark::VehicleType::Car}, now)
                   .has_value(),
               "setup cars occupy the closer cluster");
    }
    weighted.setStrategy(smartpark::AllocationStrategy::WeightedCost);
    const auto diverted = weighted.enter({u8"晋W99999", smartpark::VehicleType::Car}, now);
    const auto divertedSpot = diverted ? findSpot(weighted, diverted->spotId) : nullptr;
    expect(divertedSpot != nullptr && divertedSpot->zone() == "B",
           "weighted cost avoids a congested nearby cluster");
    expect(diverted->nearbyOccupiedSpots == 0, "the chosen cluster is not locally congested");
}
void testZonePressureBalancing(){
    const std::string description =
        "site 100 60\n"
        "entrance 50 0\n"
        "exit 50 60\n"
        "region A 20 20 1 4 2.0 5.0 3.0 up normal\n"
        "region B 76 20 1 4 2.0 5.0 3.0 up normal\n";
    smartpark::ParkingService service(
        smartpark::ParkingLayout::fromDescription(description),
        smartpark::AllocationStrategy::WeightedCost);
    smartpark::AllocationWeights weights;
    weights.entryPath = 1.0;
    weights.exitPath = 0.0;
    weights.laneCongestion = 0.0;
    weights.turnCount = 0.0;
    weights.typePenalty = 0.0;
    weights.zonePressure = 0.0;
    service.setWeights(weights);
    const auto now = smartpark::ParkingRecord::Clock::from_time_t(4000000000);
    for (int index = 0; index < 2; ++index){
        const auto result = service.enter(
            {std::string(u8"晋Z000") + std::to_string(index), smartpark::VehicleType::Car}, now);
        expect(result.has_value(), "zone-balancing setup car can enter");
        const auto *spot = result ? findSpot(service, result->spotId) : nullptr;
        expect(spot != nullptr && spot->zone() == "A",
               "with zone balancing disabled, the closer symmetric cluster fills first");
    }
    // 新压力语义：计入本车后的分区负载取平方，乘权重与场地对角线，
    // 换算成等效步行米数。半满的 A 区代价已超过空 B 区与 A 区的微小距离差。
    weights.zonePressure = 0.8;
    service.setWeights(weights);
    for (int index = 0; index < 2; ++index){
        const auto diverted = service.enter(
            {std::string(u8"晋Z9999") + std::to_string(index), smartpark::VehicleType::Car}, now);
        const auto *divertedSpot = diverted ? findSpot(service, diverted->spotId) : nullptr;
        expect(divertedSpot != nullptr && divertedSpot->zone() == "B",
               "zone pressure sends the next cars to the under-occupied zone");
        expect(diverted && diverted->breakdown.zonePressureCost > 0.0
                   && diverted->breakdown.zonePressureCost
                          < weights.zonePressure * 116.62,
               "the winning under-occupied zone pays a small progressive pressure cost");
    }
}
void testZonePressureSpreadsLoadAcrossZones(){
    // 默认布局 A/B/C 三区各 20 位，A 离入口最近、C 最远。
    // 压力均衡下连续入场不应把一个区塞满而其他区空置。
    smartpark::ParkingService service(smartpark::ParkingLayout::defaultLayout(),
                                      smartpark::AllocationStrategy::WeightedCost);
    const auto now = smartpark::ParkingRecord::Clock::from_time_t(4000000000);
    std::map<std::string, int> zoneCounts;
    for (int index = 0; index < 30; ++index){
        const auto result = service.enter(
            {std::string(u8"晋B") + std::to_string(10000 + index),
             smartpark::VehicleType::Car}, now);
        expect(result.has_value(), "zone spread test can allocate a spot");
        const auto *spot = result ? findSpot(service, result->spotId) : nullptr;
        if (spot != nullptr){
            ++zoneCounts[spot->zone()];
        }
    }
    expect(zoneCounts.size() == 3, "all three zones receive vehicles");
    for (const auto &entry : zoneCounts){
        expect(entry.second >= 5,
               "no zone is left empty while others fill: " + entry.first);
        expect(entry.second <= 15,
               "no zone absorbs most of the load: " + entry.first);
    }
}
void testMultiEntranceSelection(){
    const std::string description =
        "site 90 60\n"
        "entrance 0 8\n"
        "entrance 0 54\n"
        "exit 90 8\n"
        "region A 25 4 3 1 1.2 5.5 6 left\n"
        "region B 25 38 3 1 1.2 5.5 6 left\n";
    smartpark::ParkingService service(
        smartpark::ParkingLayout::fromDescription(description),
        smartpark::AllocationStrategy::Nearest);
    const auto now = smartpark::ParkingRecord::Clock::from_time_t(4000000000);
    const auto first = service.enter({u8"晋A12345", smartpark::VehicleType::Car}, now);
    expect(first.has_value(), "multi-entrance allocation succeeds");
    const auto firstSpot = findSpot(service, first->spotId);
    expect(firstSpot != nullptr && firstSpot->zone() == "A",
           "the first vehicle uses the closer southern region");
    expect(first->entranceIndex == 0, "the first vehicle uses the southern entrance");
    expect(distance(first->entryRoute.points.front(), service.layout().entrances()[0])
               <= distance(first->entryRoute.points.front(), service.layout().entrances()[1]) + 1.0,
           "the selected entry route starts near the chosen entrance");
    expect(service.enter({u8"晋A22222", smartpark::VehicleType::Car}, now).has_value(),
           "second southern spot can be filled");
    expect(service.enter({u8"晋A33333", smartpark::VehicleType::Car}, now).has_value(),
           "third southern spot can be filled");
    service.setStrategy(smartpark::AllocationStrategy::WeightedCost);
    const auto northern = service.enter({u8"晋B12345", smartpark::VehicleType::Car}, now);
    const auto northernSpot = northern ? findSpot(service, northern->spotId) : nullptr;
    expect(northernSpot != nullptr && northernSpot->zone() == "B",
           "later vehicles move to the northern region");
    expect(northern && northern->entranceIndex == 1, "northern parking uses the northern entrance");
}
void testSqlitePersistenceAndRecovery(){
    using namespace std::chrono_literals;
    const std::string description =
        "site 60 30\n"
        "entrance 0 15\n"
        "exit 60 15\n"
        "region A 10 10 1 3 1.2 5.5 6 left\n";
    const auto layout = smartpark::ParkingLayout::fromDescription(description);
    const auto now = smartpark::ParkingRecord::Clock::from_time_t(4000000000);
    QTemporaryDir directory;
    expect(directory.isValid(), "SQLite test can create a temporary directory");
    const QString databasePath = directory.filePath("smartpark.db");{
        smartpark::DatabaseManager database(databasePath);
        expect(database.database().isOpen(), "SQLite database opens");
        smartpark::ParkingRepository repository(database.database());
        expect(repository.saveLayout(layout), "the first database can store the layout");
        smartpark::ParkingService service(layout, smartpark::AllocationStrategy::Nearest,
                                          &repository);
        const auto first = service.enter({u8"晋A12345", smartpark::VehicleType::Car}, now);
        const auto second = service.enter({u8"晋A22222", smartpark::VehicleType::Car}, now);
        expect(first.has_value() && second.has_value(), "SQLite service accepts entries");
        expect(service.leave(u8"晋A12345", now + 15min).has_value(),
               "SQLite service closes the first record");
        expect(service.reserve({u8"晋A33333", smartpark::VehicleType::Car}, now + 15min, 30min)
                   .has_value(),
               "SQLite service reserves a third spot");
        expect(service.occupiedSpots() == 1 && service.reservedSpots() == 1,
               "SQLite service has one occupied and one reserved spot");
    }
    smartpark::DatabaseManager database(databasePath);
    smartpark::ParkingRepository repository(database.database());
    expect(repository.saveLayout(layout), "the restored database accepts the same layout");
    smartpark::ParkingService restored(layout, smartpark::AllocationStrategy::Nearest,
                                       &repository);
    expect(restored.spots().size() == 3, "restart restores every parking spot");
    expect(restored.records().size() == 2, "restart restores closed and active records");
    expect(restored.occupiedSpots() == 1 && restored.reservedSpots() == 1,
           "restart restores occupied and reserved states");
    expect(restored.activeRecord(u8"晋A22222").has_value(),
           "restart restores the active record");
    expect(!restored.activeRecord(u8"晋A12345").has_value(),
           "restart does not reopen a closed record");
    restored.expireReservations(now + 46min);
    expect(restored.reservedSpots() == 0 && restored.remainingSpots() == 2,
           "restart preserves the reservation TTL");
    expectThrows<std::invalid_argument>(
        [] { const smartpark::Vehicle invalid("", smartpark::VehicleType::Car); },
        "persistence rejects an empty plate number");
    QSqlQuery invalidate(database.database());
    invalidate.prepare(QStringLiteral(
        "UPDATE parking_spots SET status=99 WHERE identifier='A001'"));
    expect(invalidate.exec(), "SQLite test can corrupt a spot status");
    smartpark::ParkingRepository invalidRepository(database.database());
    expect(invalidRepository.loadSpotStates().empty(),
           "loader rejects an invalid persisted spot status");
    expect(!invalidRepository.lastError().empty(),
           "loader reports an invalid persisted spot status");
}
void testVehicleTypeUpdate(){
    using namespace std::chrono_literals;
    const std::string description =
        "site 60 30\n"
        "entrance 0 15\n"
        "exit 60 15\n"
        "region A 10 10 1 3 1.2 5.5 6 left\n";
    const auto layout = smartpark::ParkingLayout::fromDescription(description);
    const auto now = smartpark::ParkingRecord::Clock::from_time_t(4000000000);

    smartpark::ParkingService service(layout, smartpark::AllocationStrategy::Nearest);
    const auto occupied = service.enter({u8"晋A66666", smartpark::VehicleType::Car}, now);
    expect(occupied.has_value(), "vehicle type test accepts an occupied vehicle");
    const auto occupiedSpotId = occupied ? occupied->spotId : std::string{};
    expect(service.updateVehicleType(u8"晋A66666", smartpark::VehicleType::Truck),
           "occupied vehicle type can be updated");
    const auto *occupiedSpot = findSpot(service, occupiedSpotId);
    expect(occupiedSpot != nullptr && occupiedSpot->parkedVehicle()
               && occupiedSpot->parkedVehicle()->type() == smartpark::VehicleType::Truck,
           "occupied vehicle type update changes the in-memory vehicle");
    expect(service.updateVehicleType(u8"晋A66666", smartpark::VehicleType::Truck),
           "updating to the same vehicle type is idempotent");
    expect(!service.updateVehicleType(u8"晋A99999", smartpark::VehicleType::Electric),
           "vehicle type update rejects a vehicle that is not in the parking lot");

    const auto reserved = service.reserve(
        {u8"晋A77777", smartpark::VehicleType::Motorcycle}, now, 30min);
    expect(reserved.has_value(), "vehicle type test accepts a reserved vehicle");
    const auto reservedSpotId = reserved ? reserved->spotId : std::string{};
    expect(service.updateVehicleType(u8"晋A77777", smartpark::VehicleType::Electric),
           "reserved vehicle type can be updated");
    const auto *reservedSpot = findSpot(service, reservedSpotId);
    expect(reservedSpot != nullptr && reservedSpot->status() == smartpark::SpotStatus::Reserved
               && reservedSpot->parkedVehicle()
               && reservedSpot->parkedVehicle()->type() == smartpark::VehicleType::Electric,
           "reserved vehicle type update preserves reservation state");
    expect(reservedSpot != nullptr && reservedSpot->reservationExpiresAt()
               && *reservedSpot->reservationExpiresAt() == now + 30min,
           "reserved vehicle type update preserves reservation expiry");

    QTemporaryDir directory;
    expect(directory.isValid(), "vehicle type persistence test can create a temporary directory");
    const QString databasePath = directory.filePath("vehicle-type.db");
    std::string persistedSpotId;
    {
        smartpark::Persistence persistence(databasePath);
        smartpark::ParkingService persisted(layout, smartpark::AllocationStrategy::Nearest,
                                            &persistence.repository());
        const auto allocation = persisted.enter(
            {u8"晋A88888", smartpark::VehicleType::Car}, now);
        expect(allocation.has_value(), "persistent vehicle type test accepts an entry");
        persistedSpotId = allocation ? allocation->spotId : std::string{};
        expect(persisted.updateVehicleType(u8"晋A88888", smartpark::VehicleType::Truck),
               "persistent occupied vehicle type can be updated");
    }
    {
        smartpark::Persistence persistence(databasePath);
        smartpark::ParkingService restored(layout, smartpark::AllocationStrategy::Nearest,
                                           &persistence.repository());
        const auto *restoredSpot = findSpot(restored, persistedSpotId);
        expect(restoredSpot != nullptr && restoredSpot->parkedVehicle()
                   && restoredSpot->parkedVehicle()->type() == smartpark::VehicleType::Truck,
               "restart restores the updated occupied vehicle type");
    }
}
void testPersistenceHelperRestoresAcrossRestart(){
    using namespace std::chrono_literals;
    const std::string description =
        "site 60 30\n"
        "entrance 0 15\n"
        "exit 60 15\n"
        "region A 10 10 1 3 1.2 5.5 6 left\n";
    const auto layout = smartpark::ParkingLayout::fromDescription(description);
    const auto now = smartpark::ParkingRecord::Clock::from_time_t(4000000000);
    QTemporaryDir directory;
    expect(directory.isValid(), "Persistence test can create a temporary directory");
    const QString databasePath = directory.filePath("smartpark.db");{
        smartpark::Persistence persistence(databasePath);
        expect(persistence.databaseManager().database().isOpen(),
               "Persistence helper opens the SQLite database");
        smartpark::ParkingService service(layout, smartpark::AllocationStrategy::Nearest,
                                          &persistence.repository());
        expect(service.enter({u8"晋A11111", smartpark::VehicleType::Car}, now).has_value(),
               "Persistence-backed service accepts an entry");
        expect(service.reserve({u8"晋A22222", smartpark::VehicleType::Car}, now + 5min, 30min)
                   .has_value(),
               "Persistence-backed service accepts a reservation");
        expect(service.leave(u8"晋A11111", now + 15min).has_value(),
               "Persistence-backed service closes a record");
    }
    smartpark::Persistence restored(databasePath);
    smartpark::ParkingService service(layout, smartpark::AllocationStrategy::Nearest,
                                      &restored.repository());
    expect(service.records().size() == 1, "Persistence helper restores the closed record");
    expect(service.reservedSpots() == 1 && service.occupiedSpots() == 0,
           "Persistence helper restores the reservation but not the released spot");
    const auto reserved = std::find_if(
        service.spots().begin(), service.spots().end(),
        [](const smartpark::ParkingSpot &spot){
            return spot.status() == smartpark::SpotStatus::Reserved;
        });
    expect(reserved != service.spots().end() && reserved->parkedVehicle()
               && reserved->parkedVehicle()->plateNumber() == u8"晋A22222",
           "Persistence helper restores the reserved vehicle");
    service.expireReservations(now + 46min);
    expect(service.reservedSpots() == 0, "restart preserves the reservation TTL");
}
void testLayoutMismatchRejected(){
    const std::string firstDescription =
        "site 60 30\n"
        "entrance 0 15\n"
        "exit 60 15\n"
        "region A 10 10 1 3 1.2 5.5 6 left\n";
    const std::string secondDescription =
        "site 70 30\n"
        "entrance 0 15\n"
        "exit 70 15\n"
        "region A 10 10 1 3 1.2 5.5 6 left\n";
    const auto firstLayout = smartpark::ParkingLayout::fromDescription(firstDescription);
    const auto secondLayout = smartpark::ParkingLayout::fromDescription(secondDescription);
    QTemporaryDir directory;
    expect(directory.isValid(), "mismatch test can create a temporary directory");
    smartpark::Persistence persistence(directory.filePath("smartpark.db"));
    smartpark::ParkingService first(firstLayout, smartpark::AllocationStrategy::Nearest,
                                    &persistence.repository());
    expect(first.spots().size() == 3, "first layout initializes against the database");
    expectThrows<std::runtime_error>(
        [&]{
            smartpark::ParkingService mismatched(secondLayout,
                                                 smartpark::AllocationStrategy::Nearest,
                                                 &persistence.repository());
        },
        "a different layout is rejected against the persisted signature");
}
void testExpiredReservationPersisted(){
    using namespace std::chrono_literals;
    const std::string description =
        "site 60 30\n"
        "entrance 0 15\n"
        "exit 60 15\n"
        "region A 10 10 1 3 1.2 5.5 6 left\n";
    const auto layout = smartpark::ParkingLayout::fromDescription(description);
    const auto now = smartpark::ParkingRecord::Clock::from_time_t(4000000000);
    QTemporaryDir directory;
    expect(directory.isValid(), "expiry test can create a temporary directory");
    const QString databasePath = directory.filePath("smartpark.db");{
        smartpark::Persistence persistence(databasePath);
        smartpark::ParkingService service(layout, smartpark::AllocationStrategy::Nearest,
                                          &persistence.repository());
        expect(service.reserve({u8"晋A12345", smartpark::VehicleType::Car}, now + 5min, 30min)
                   .has_value(),
               "expiry test can reserve a spot");
        service.expireReservations(now + 46min);
        expect(service.reservedSpots() == 0, "expired reservation is released in memory");
    }
    smartpark::Persistence restored(databasePath);
    smartpark::ParkingService service(layout, smartpark::AllocationStrategy::Nearest,
                                      &restored.repository());
    expect(service.reservedSpots() == 0 && service.occupiedSpots() == 0,
           "expired reservation stays released after restart");
    expect(service.remainingSpots() == 3,
           "the expired spot is available after restart");
}
void testDirtyActiveRecordsRejected(){
    using namespace std::chrono_literals;
    const std::string description =
        "site 60 30\n"
        "entrance 0 15\n"
        "exit 60 15\n"
        "region A 10 10 1 3 1.2 5.5 6 left\n";
    const auto layout = smartpark::ParkingLayout::fromDescription(description);
    const auto now = smartpark::ParkingRecord::Clock::from_time_t(4000000000);
    QTemporaryDir directory;
    expect(directory.isValid(), "dirty record test can create a temporary directory");
    smartpark::Persistence persistence(directory.filePath("smartpark.db"));
    smartpark::ParkingService service(layout, smartpark::AllocationStrategy::Nearest,
                                      &persistence.repository());
    expect(service.enter({u8"晋A12345", smartpark::VehicleType::Car}, now).has_value(),
           "dirty record test can enter a vehicle");
    QSqlQuery duplicate(persistence.databaseManager().database());
    duplicate.prepare(QStringLiteral(
        "INSERT INTO parking_records(plate_number,spot_id,entry_time_ms,exit_time_ms,fee)"
        " VALUES(:plate,:spot,:entry,NULL,0)"));
    duplicate.bindValue(QStringLiteral(":plate"), QString::fromUtf8(u8"晋A12345"));
    duplicate.bindValue(QStringLiteral(":spot"), QStringLiteral("A002"));
    duplicate.bindValue(QStringLiteral(":entry"), 0);
    expect(!duplicate.exec(),
           "partial unique index blocks a duplicate active record");
    QSqlQuery drop(persistence.databaseManager().database());
    expect(drop.exec(QStringLiteral("DROP INDEX idx_active_parking_record")),
           "can drop the active record index for the corruption scenario");
    QSqlQuery secondDuplicate(persistence.databaseManager().database());
    secondDuplicate.prepare(QStringLiteral(
        "INSERT INTO parking_records(plate_number,spot_id,entry_time_ms,exit_time_ms,fee)"
        " VALUES(:plate,:spot,:entry,NULL,0)"));
    secondDuplicate.bindValue(QStringLiteral(":plate"), QString::fromUtf8(u8"晋A12345"));
    secondDuplicate.bindValue(QStringLiteral(":spot"), QStringLiteral("A002"));
    secondDuplicate.bindValue(QStringLiteral(":entry"), 0);
    expect(secondDuplicate.exec(),
           "duplicate active record can be inserted without the index");
    expectThrows<std::runtime_error>(
        [&]{
            smartpark::ParkingService dirty(layout,
                                            smartpark::AllocationStrategy::Nearest,
                                            &persistence.repository());
        },
        "dirty duplicate active records are rejected on restore");
}
void testDisabledStateAndExpiryInvariants(){
    const std::string description =
        "site 60 30\n"
        "entrance 0 15\n"
        "exit 60 15\n"
        "region A 10 10 1 3 1.2 5.5 6 left\n";
    const auto layout = smartpark::ParkingLayout::fromDescription(description);
    const auto now = smartpark::ParkingRecord::Clock::from_time_t(4000000000);
    QTemporaryDir directory;
    expect(directory.isValid(), "invariant test can create a temporary directory");
    const auto corrupt = [&](const QString &path, const char *update){
        smartpark::Persistence persistence(path);
        smartpark::ParkingService service(layout, smartpark::AllocationStrategy::Nearest,
                                          &persistence.repository());
        QSqlQuery query(persistence.databaseManager().database());
        query.prepare(QStringLiteral(
            "UPDATE parking_spots SET %1 WHERE identifier='A001'").arg(QString::fromUtf8(update)));
        expect(query.exec(), "invariant test can corrupt a parking spot");
    };
    const auto reopen = [&](const QString &path){
        smartpark::Persistence persistence(path);
        smartpark::ParkingService service(layout, smartpark::AllocationStrategy::Nearest,
                                          &persistence.repository());
    };
    corrupt(directory.filePath("reserved_without_expiry.db"),
            "status=2, plate_number='晋A9', vehicle_type=0, reserved_until_ms=NULL");
    expectThrows<std::runtime_error>(
        [&] { reopen(directory.filePath("reserved_without_expiry.db")); },
        "a reserved spot without an expiry time is rejected on restore");
    corrupt(directory.filePath("available_with_expiry.db"),
            "status=0, plate_number=NULL, vehicle_type=NULL, reserved_until_ms=12345");
    expectThrows<std::runtime_error>(
        [&] { reopen(directory.filePath("available_with_expiry.db")); },
        "a non-reserved spot with an expiry time is rejected on restore");
    corrupt(directory.filePath("disabled_with_vehicle.db"),
            "status=3, plate_number='晋A9', vehicle_type=0, reserved_until_ms=NULL");
    expectThrows<std::runtime_error>(
        [&] { reopen(directory.filePath("disabled_with_vehicle.db")); },
        "a disabled spot with a vehicle is rejected on restore");
    corrupt(directory.filePath("disabled_ok.db"),
            "status=3, plate_number=NULL, vehicle_type=NULL, reserved_until_ms=NULL");{
        smartpark::Persistence persistence(directory.filePath("disabled_ok.db"));
        smartpark::ParkingService service(layout, smartpark::AllocationStrategy::Nearest,
                                          &persistence.repository());
        const smartpark::ParkingSpot *disabled = findSpot(service, "A001");
        expect(disabled != nullptr
                   && disabled->status() == smartpark::SpotStatus::Disabled
                   && !disabled->parkedVehicle().has_value(),
               "a disabled spot is restored without a vehicle");
        expect(service.remainingSpots() == 2,
               "a disabled spot is not counted as available");
    }
}
void testLayoutSpotSetValidation(){
    const std::string description =
        "site 60 30\n"
        "entrance 0 15\n"
        "exit 60 15\n"
        "region A 10 10 1 3 1.2 5.5 6 left\n";
    const auto layout = smartpark::ParkingLayout::fromDescription(description);
    QTemporaryDir directory;
    expect(directory.isValid(), "layout set test can create a temporary directory");
    smartpark::Persistence persistence(directory.filePath("smartpark.db"));
    smartpark::ParkingService service(layout, smartpark::AllocationStrategy::Nearest,
                                      &persistence.repository());
    expect(service.spots().size() == 3, "layout set test initializes the layout");
    QSqlQuery extra(persistence.databaseManager().database());
    extra.prepare(QStringLiteral(
        "INSERT INTO parking_spots("
        "identifier,zone,type,row_index,column_index,x,y,width,height,access_x,access_y,status)"
        " VALUES('A099','X',0,0,0,1,1,1,1,1,1,0)"));
    expect(extra.exec(), "layout set test can insert an extra spot row");
    expectThrows<std::runtime_error>(
        [&]{
            smartpark::ParkingService mismatched(layout,
                                                 smartpark::AllocationStrategy::Nearest,
                                                 &persistence.repository());
        },
        "a persisted spot id set differing from the layout is rejected");
}
void testTimeValidationAndSafeConversion(){
    using namespace std::chrono_literals;
    const std::string description =
        "site 60 30\n"
        "entrance 0 15\n"
        "exit 60 15\n"
        "region A 10 10 1 3 1.2 5.5 6 left\n";
    const auto layout = smartpark::ParkingLayout::fromDescription(description);
    const auto valid = smartpark::ParkingRecord::Clock::from_time_t(4000000000);
    const auto tooEarly = smartpark::ParkingRecord::Clock::from_time_t(500);
    const auto tooLate = smartpark::ParkingRecord::Clock::from_time_t(8000000000LL);
    const auto maxPoint = smartpark::ParkingRecord::Clock::time_point{} +
        std::chrono::milliseconds(smartpark::timeutil::maxValidSeconds * 1000);
    expect(smartpark::timeutil::isValid(maxPoint),
           "isValid accepts the exact max boundary");
    expect(!smartpark::timeutil::isValid(maxPoint + std::chrono::milliseconds(1)),
           "isValid rejects max + 1ms");
    expect(!smartpark::timeutil::canAdd(maxPoint - std::chrono::milliseconds(999),
                                        std::chrono::seconds(1)),
           "canAdd rejects max - 999ms + 1s");
    expect(smartpark::timeutil::canAdd(maxPoint - std::chrono::milliseconds(1000),
                                       std::chrono::seconds(1)),
           "canAdd accepts exactly max - 1s + 1s");
    expect(!smartpark::timeutil::canAdd(maxPoint - std::chrono::milliseconds(999),
                                        std::chrono::seconds(2)),
           "canAdd rejects an addend crossing the max boundary");
    smartpark::ParkingService service(layout);
    expect(!service.enter({u8"晋A11111", smartpark::VehicleType::Car}, tooEarly).has_value(),
           "entry rejects a timestamp before the business range");
    expect(!service.enter({u8"晋A11111", smartpark::VehicleType::Car}, tooLate).has_value(),
           "entry rejects a timestamp after the business range");
    expect(service.enter({u8"晋A11111", smartpark::VehicleType::Car}, valid).has_value(),
           "entry accepts a timestamp inside the business range");
    expect(!service.leave(u8"晋A11111", tooEarly).has_value(),
           "exit rejects a timestamp before the business range");
    expect(service.leave(u8"晋A11111", valid + 15min).has_value(),
           "exit accepts a timestamp inside the business range");
    expect(!service.reserve({u8"晋A22222", smartpark::VehicleType::Car}, tooLate, 30min)
               .has_value(),
           "reservation rejects a timestamp after the business range");
    expect(!service.reserve({u8"晋A22222", smartpark::VehicleType::Car}, valid,
                            std::chrono::seconds(std::numeric_limits<std::int64_t>::max()))
               .has_value(),
           "reservation rejects an expiry that would overflow the clock");
    expect(service.reserve({u8"晋A22222", smartpark::VehicleType::Car}, valid, 30min)
               .has_value(),
           "reservation accepts a timestamp inside the business range");
    QTemporaryDir directory;
    expect(directory.isValid(), "time validation test can create a temporary directory");
    const QString databasePath = directory.filePath("smartpark.db");{
        smartpark::DatabaseManager database(databasePath);
        expect(database.database().isOpen(), "time validation database opens");
        smartpark::ParkingRepository repository(database.database());
        expect(repository.saveLayout(layout), "time validation test stores the layout");
        smartpark::ParkingSpot spot("A001");
        const smartpark::Vehicle vehicle(u8"晋A55555", smartpark::VehicleType::Car);
        expect(spot.reserve(vehicle, tooLate),
               "model spot accepts an out-of-range reservation");
        expect(!repository.saveReservation(spot),
               "repository rejects an out-of-range reservation expiry");
        expect(!repository.lastError().empty(),
               "repository reports the out-of-range reservation expiry");
    }{
        smartpark::DatabaseManager database(databasePath);
        smartpark::ParkingRepository repository(database.database());
        QSqlQuery spotCorruption(database.database());
        expect(spotCorruption.exec(QStringLiteral(
                   "UPDATE parking_spots SET status=2, plate_number='晋A66666',"
                   "vehicle_type=0, reserved_until_ms=99999999999999"
                   " WHERE identifier='A001'")),
               "time validation test can corrupt the reserved expiry");
        expect(repository.loadSpotStates().empty(),
               "loader rejects an out-of-range persisted reservation expiry");
        expect(!repository.lastError().empty(),
               "loader reports the out-of-range persisted reservation expiry");
        QSqlQuery recordCorruption(database.database());
        expect(recordCorruption.exec(QStringLiteral(
                   "INSERT INTO parking_records(plate_number,spot_id,"
                   "entry_time_ms,exit_time_ms,fee)"
                   " VALUES('晋A88888','A002',99999999999999,NULL,0)")),
               "time validation test can insert an out-of-range entry time");
        expect(repository.loadRecords().empty(),
               "loader rejects an out-of-range persisted entry time");
        expect(!repository.lastError().empty(),
               "loader reports the out-of-range persisted entry time");
    }
}
} // namespace
void testBookingModelLifecycle(){
    using namespace std::chrono_literals;
    const auto created = smartpark::Booking::Clock::from_time_t(1700000000);
    const auto arrival = created + 2h;
    const auto deadline = arrival + 30min;
    smartpark::Booking booked("BK001", u8"晋A12345", "A001", created, arrival, deadline, 20.0);
    expect(booked.id() == "BK001", "booking keeps its id");
    expect(booked.plateNumber() == u8"晋A12345", "booking keeps its plate number");
    expect(booked.spotId() == "A001", "booking keeps its spot id");
    expect(booked.createdAt() == created && booked.arrivalTime() == arrival
               && booked.arrivalDeadline() == deadline,
           "booking keeps its timestamps");
    expect(booked.deposit() == 20.0, "booking keeps its deposit");
    expect(booked.status() == smartpark::BookingStatus::Booked, "new booking is booked");
    expect(booked.isActive(), "new booking is active");
    expect(booked.checkIn(), "booked booking can check in");
    expect(booked.status() == smartpark::BookingStatus::CheckedIn,
           "check-in updates the booking status");
    expect(!booked.isActive(), "checked-in booking is no longer active");
    expect(!booked.checkIn(), "checked-in booking cannot check in twice");

    smartpark::Booking noShow("BK002", u8"晋A12345", "A001", created, arrival, deadline, 20.0);
    expect(noShow.markNoShow(), "booked booking can be marked no-show");
    expect(noShow.status() == smartpark::BookingStatus::NoShow,
           "no-show updates the booking status");
    expect(!noShow.markNoShow(), "no-show booking cannot transition again");

    smartpark::Booking cancelled("BK003", u8"晋A12345", "A001", created, arrival, deadline, 20.0);
    expect(cancelled.cancel(), "booked booking can be cancelled");
    expect(cancelled.status() == smartpark::BookingStatus::Cancelled,
           "cancel updates the booking status");

    expectThrows<std::invalid_argument>(
        [&] { smartpark::Booking b("", u8"晋A12345", "A001", created, arrival, deadline, 20.0); },
        "booking rejects an empty id");
    expectThrows<std::invalid_argument>(
        [&] { smartpark::Booking b("BK", "", "A001", created, arrival, deadline, 20.0); },
        "booking rejects an empty plate number");
    expectThrows<std::invalid_argument>(
        [&] { smartpark::Booking b("BK", u8"晋A12345", "", created, arrival, deadline, 20.0); },
        "booking rejects an empty spot id");
    expectThrows<std::invalid_argument>(
        [&] { smartpark::Booking b("BK", u8"晋A12345", "A001", created, arrival, deadline, -1.0); },
        "booking rejects a negative deposit");
    expectThrows<std::invalid_argument>(
        [&] { smartpark::Booking b("BK", u8"晋A12345", "A001", created, created - 1h, deadline, 20.0); },
        "booking rejects an arrival before creation");
    expectThrows<std::invalid_argument>(
        [&] { smartpark::Booking b("BK", u8"晋A12345", "A001", created, arrival, arrival - 1min, 20.0); },
        "booking rejects a deadline before arrival");
}
const std::string bookingLayoutDescription =
    "site 60 30\n"
    "entrance 0 15\n"
    "exit 60 15\n"
    "region A 10 10 1 3 1.2 5.5 6 left\n";
void testBookingCreateConfirmAndRoutes(){
    using namespace std::chrono_literals;
    smartpark::ParkingService service(
        smartpark::ParkingLayout::fromDescription(bookingLayoutDescription));
    const auto now = smartpark::ParkingRecord::Clock::from_time_t(4000000000);
    const smartpark::Vehicle vehicle(u8"晋A12345", smartpark::VehicleType::Car);
    const auto booked = service.createBooking(vehicle, now + 2h, now);
    expect(booked.has_value(), "booking can be created within the advance window");
    expect(booked->booking.status() == smartpark::BookingStatus::Booked,
           "created booking is active");
    expect(booked->booking.deposit() == service.bookingPolicy().deposit,
           "created booking charges the configured deposit");
    expect(booked->allocation.entryRoute.points.size() >= 2
               && booked->allocation.exitRoute.points.size() >= 2,
           "booking returns the expected entry and exit routes");
    expect(booked->allocation.entryRoute.distance > 0.0,
           "booking entry route has a positive distance");
    expect(service.reservedSpots() == 1, "booking reserves one spot");
    expect(service.pendingDeposits() == 20.0, "booking holds the deposit before arrival");
    expect(service.forfeitedDeposits() == 0.0, "booking does not forfeit before no-show");
    const auto arrived = service.confirmBooking(u8"晋A12345", now + 2h);
    expect(arrived.has_value(), "booking can be confirmed at its arrival time");
    expect(arrived->spotId == booked->booking.spotId(),
           "confirmation occupies the reserved spot");
    expect(service.occupiedSpots() == 1, "confirmed booking becomes occupied");
    expect(service.reservedSpots() == 0, "confirmed booking releases the reservation hold");
    expect(service.pendingDeposits() == 0.0, "arrival refunds the deposit");
    expect(service.forfeitedDeposits() == 0.0, "arrival does not forfeit the deposit");
    expect(!service.activeBooking(u8"晋A12345").has_value(),
           "confirmed booking is no longer active");
    expect(service.bookings().size() == 1 && service.bookings().front().status()
               == smartpark::BookingStatus::CheckedIn,
           "confirmed booking is recorded as checked-in");
}
void testBookingNoShowForfeitsDeposit(){
    using namespace std::chrono_literals;
    smartpark::ParkingService service(
        smartpark::ParkingLayout::fromDescription(bookingLayoutDescription));
    const auto now = smartpark::ParkingRecord::Clock::from_time_t(4000000000);
    const smartpark::Vehicle vehicle(u8"晋A12345", smartpark::VehicleType::Car);
    const auto booked = service.createBooking(vehicle, now + 1h, now);
    expect(booked.has_value(), "no-show test can create a booking");
    service.expireBookings(now + 1h + 29min);
    expect(service.reservedSpots() == 1, "booking stays reserved within the grace period");
    service.expireBookings(now + 1h + 31min);
    expect(service.reservedSpots() == 0, "expired booking releases its spot");
    expect(service.forfeitedDeposits() == 20.0, "no-show forfeits the deposit");
    expect(service.pendingDeposits() == 0.0, "no-show clears the pending deposit");
    expect(service.bookings().front().status() == smartpark::BookingStatus::NoShow,
           "expired booking is marked no-show");
    expect(service.remainingSpots() == 3, "the released spot is available again");
}
void testBookingRejectsOutOfWindow(){
    using namespace std::chrono_literals;
    smartpark::ParkingService service(
        smartpark::ParkingLayout::fromDescription(bookingLayoutDescription));
    const auto now = smartpark::ParkingRecord::Clock::from_time_t(4000000000);
    const smartpark::Vehicle vehicle(u8"晋A12345", smartpark::VehicleType::Car);
    expect(!service.createBooking(vehicle, now - 1h, now).has_value(),
           "booking rejects a past arrival time");
    expect(!service.createBooking(vehicle, now, now).has_value(),
           "booking rejects an arrival time equal to now");
    expect(!service.createBooking(vehicle, now + 8 * 24h, now).has_value(),
           "booking rejects an arrival beyond seven days");
    const auto booked = service.createBooking(vehicle, now + 2h, now);
    expect(booked.has_value(), "booking accepts an arrival within the week");
    expect(!service.createBooking(vehicle, now + 3h, now).has_value(),
           "booking rejects a duplicate active booking");
    const smartpark::Vehicle other(u8"晋A88888", smartpark::VehicleType::Electric);
    expect(service.createBooking(other, now + 2h, now).has_value(),
           "a different vehicle can book a different spot");
}
void testBookingCancelRefunds(){
    using namespace std::chrono_literals;
    smartpark::ParkingService service(
        smartpark::ParkingLayout::fromDescription(bookingLayoutDescription));
    const auto now = smartpark::ParkingRecord::Clock::from_time_t(4000000000);
    const smartpark::Vehicle vehicle(u8"晋A12345", smartpark::VehicleType::Car);
    const auto booked = service.createBooking(vehicle, now + 2h, now);
    expect(booked.has_value(), "cancel test can create a booking");
    expect(!service.cancelBooking(u8"晋A12345", now + 2h),
           "booking cannot be cancelled after its arrival time");
    expect(service.reservedSpots() == 1, "late cancel leaves the booking reserved");
    expect(service.cancelBooking(u8"晋A12345", now + 1h),
           "booking can be cancelled before its arrival time");
    expect(service.reservedSpots() == 0, "cancelled booking releases its spot");
    expect(service.forfeitedDeposits() == 0.0, "cancellation does not forfeit the deposit");
    expect(service.bookings().front().status() == smartpark::BookingStatus::Cancelled,
           "cancelled booking is recorded as cancelled");
}
void testBookingPersistenceAcrossRestart(){
    using namespace std::chrono_literals;
    const auto layout = smartpark::ParkingLayout::fromDescription(bookingLayoutDescription);
    const auto now = smartpark::ParkingRecord::Clock::from_time_t(4000000000);
    QTemporaryDir directory;
    expect(directory.isValid(), "booking persistence test can create a temporary directory");
    const QString databasePath = directory.filePath("smartpark.db");{
        smartpark::Persistence persistence(databasePath);
        smartpark::ParkingService service(layout, smartpark::AllocationStrategy::Nearest,
                                          &persistence.repository());
        const auto kept = service.createBooking(
            {u8"晋A12345", smartpark::VehicleType::Car}, now + 2h, now);
        expect(kept.has_value(), "booking persistence test can book the first vehicle");
        const auto noShow = service.createBooking(
            {u8"晋A22222", smartpark::VehicleType::Car}, now + 1h, now);
        expect(noShow.has_value(), "booking persistence test can book the second vehicle");
        service.expireBookings(now + 1h + 31min);
        expect(service.forfeitedDeposits() == 20.0,
               "booking persistence test forfeits the no-show deposit");
    }
    smartpark::Persistence restoredPersistence(databasePath);
    smartpark::ParkingService restored(layout, smartpark::AllocationStrategy::Nearest,
                                       &restoredPersistence.repository());
    expect(restored.bookings().size() == 2, "restart restores every booking");
    expect(restored.reservedSpots() == 1, "restart restores the active booking reservation");
    const auto active = restored.activeBooking(u8"晋A12345");
    expect(active.has_value(), "restart restores the active booking");
    expect(restored.forfeitedDeposits() == 20.0,
           "restart preserves the forfeited deposit");
    const auto arrived = restored.confirmBooking(u8"晋A12345", now + 2h);
    expect(arrived.has_value(), "an active booking can be confirmed after restart");
    expect(restored.occupiedSpots() == 1, "post-restart confirmation occupies the spot");
}
void testPreviewAllocationReadOnly(){
    const std::string description =
        "site 80 40\n"
        "entrance 0 20\n"
        "exit 80 20\n"
        "region A 12 14 2 1 1.2 5.5 6 left normal\n"
        "region B 28 14 2 1 1.2 5.5 6 left charging\n";
    smartpark::ParkingService service(
        smartpark::ParkingLayout::fromDescription(description),
        smartpark::AllocationStrategy::WeightedCost);
    const auto now = smartpark::ParkingRecord::Clock::from_time_t(4000000000);
    const auto first = service.enter({u8"晋A12345", smartpark::VehicleType::Car}, now);
    expect(first.has_value(), "preview setup accepts the first vehicle");
    const int occupiedBefore = service.occupiedSpots();
    const int recordsBefore = static_cast<int>(service.records().size());
    const int bookingsBefore = static_cast<int>(service.bookings().size());
    const auto strategyBefore = service.strategy();

    const smartpark::Vehicle candidate(u8"晋B99999", smartpark::VehicleType::Electric);
    const auto weighted =
        service.previewAllocation(candidate, smartpark::AllocationStrategy::WeightedCost);
    const auto nearest =
        service.previewAllocation(candidate, smartpark::AllocationStrategy::Nearest);
    expect(weighted.has_value(), "read-only weighted preview succeeds");
    expect(nearest.has_value(), "read-only nearest preview succeeds");

    expect(service.occupiedSpots() == occupiedBefore, "preview does not occupy a spot");
    expect(static_cast<int>(service.records().size()) == recordsBefore,
           "preview does not append a record");
    expect(static_cast<int>(service.bookings().size()) == bookingsBefore,
           "preview does not append a booking");
    expect(service.strategy() == strategyBefore, "preview restores the active strategy");
    expect(!service.activeRecord(candidate.plateNumber()).has_value(),
           "preview does not create an active record");
    expect(findSpot(service, weighted->spotId) != nullptr,
           "weighted preview returns a real spot identifier");
    expect(findSpot(service, nearest->spotId) != nullptr,
           "nearest preview returns a real spot identifier");
}

void testParkingInsightEngine(){
    const std::string description =
        "site 80 40\n"
        "entrance 0 20\n"
        "exit 80 20\n"
        "region A 12 14 2 1 1.2 5.5 6 left normal\n"
        "region B 28 14 2 1 1.2 5.5 6 left charging\n";
    smartpark::ParkingService service(smartpark::ParkingLayout::fromDescription(description));
    const auto now = smartpark::ParkingRecord::Clock::from_time_t(4000000000);
    service.enter({u8"晋A12345", smartpark::VehicleType::Car}, now - std::chrono::minutes(30));
    service.enter({u8"晋A22222", smartpark::VehicleType::Electric}, now - std::chrono::minutes(10));
    service.leave(u8"晋A12345", now - std::chrono::minutes(5));

    const auto insights = smartpark::ParkingInsightEngine::analyze(
        service.spots(), service.records(), service.bookings(), now);

    expect(insights.effectiveCapacity == static_cast<int>(service.spots().size()),
           "insight effective capacity equals total spots when nothing is disabled");
    expect(insights.currentOccupied == 1, "insight counts the occupied spot");
    expect(insights.currentDisabled == 0, "insight counts no disabled spot");

    int zoneTotal = 0;
    for (const auto &zone : insights.zones){
        zoneTotal += zone.total;
        expect(zone.pressure >= 0.0 && zone.pressure <= 1.0,
               "zone pressure stays within [0,1]");
    }
    expect(zoneTotal == static_cast<int>(service.spots().size()),
           "zones partition every spot");

    expect(insights.forecasts.size() == 3, "three forecast horizons are produced");
    bool has30 = false;
    bool has60 = false;
    bool has120 = false;
    for (const auto &forecast : insights.forecasts){
        has30 = has30 || forecast.minutes == 30;
        has60 = has60 || forecast.minutes == 60;
        has120 = has120 || forecast.minutes == 120;
        expect(forecast.predictedOccupied >= 0.0
                   && forecast.predictedOccupied <= insights.effectiveCapacity,
               "forecast stays inside capacity bounds");
        expect(forecast.predictedRate >= 0.0 && forecast.predictedRate <= 100.0,
               "forecast rate is a percentage");
    }
    expect(has30 && has60 && has120, "forecasts cover 30/60/120 minutes");

    expect(insights.arrivals180 >= insights.arrivals60,
           "180-minute arrivals include 60-minute arrivals");
    expect(insights.departures180 >= insights.departures60,
           "180-minute departures include 60-minute departures");

    expect(!insights.alerts.empty(), "insight always produces at least one alert");
}


// ================= 远程时间段预约（SmartPark 0.7 Reservation） =================
const char *reservationTestLayout =
    "site 60 30\n"
    "entrance 0 15\n"
    "exit 60 15\n"
    "region A 10 10 1 3 1.2 5.5 6 left\n";
const auto reservationTestNow = smartpark::ParkingRecord::Clock::from_time_t(4000000000);

// ReservationResult 里的 reservation 是下单时的快照。
std::optional<smartpark::Reservation> reservationById(
    const smartpark::ReservationService &reservations, const std::string &id){
    const auto match = std::find_if(
        reservations.reservations().begin(), reservations.reservations().end(),
        [&id](const smartpark::Reservation &item) { return item.id() == id; });
    if (match == reservations.reservations().end()){
        return std::nullopt;
    }
    return *match;
}

void testReservationModelLifecycle(){
    using namespace std::chrono_literals;
    const auto created = reservationTestNow;
    smartpark::ExpectedRoute route;
    route.entryRoute.points = {smartpark::Point{0.0, 15.0}, smartpark::Point{10.5, 15.0}};
    route.entryRoute.distance = 10.5;
    route.entryRoute.turnCount = 1;
    route.exitRoute.points = {smartpark::Point{10.5, 15.0}, smartpark::Point{60.0, 15.0}};
    route.exitRoute.distance = 49.5;
    const auto encoded = route.encode();
    const auto decoded = smartpark::ExpectedRoute::decode(encoded);
    expect(decoded.has_value(), "expected route snapshot can be decoded");
    expect(decoded && decoded->entryRoute.points.size() == 2
               && std::abs(decoded->entryRoute.points[1].x - 10.5) < 1e-9
               && decoded->entryRoute.turnCount == 1
               && decoded->exitIndex == route.exitIndex,
           "expected route snapshot round-trips through its text encoding");
    expect(smartpark::ExpectedRoute::decode("not-a-route").has_value() == false,
           "route decoder rejects malformed snapshots");
    expect(route.encode().empty() == false, "non-empty route encodes to text");
    expect(smartpark::ExpectedRoute{}.encode().empty(),
           "empty route snapshot encodes to an empty string");

    smartpark::Reservation pending("R-P", u8"晋A12345", smartpark::VehicleType::Car,
                                   "A001", created, created + 2h, created + 5h,
                                   created + 2h + 30min, 20.0, "PAY-1",
                                   smartpark::ReservationStatus::PendingPayment);
    expect(pending.isOpen(), "pending payment reservation is open");
    expect(pending.expire(), "pending payment reservation can expire");
    expect(!pending.isOpen(), "expired reservation is closed");
    expect(!pending.checkIn(), "expired reservation cannot check in");

    smartpark::Reservation confirmed("R-C", u8"晋A12345", smartpark::VehicleType::Car,
                                     "A001", created, created + 2h, created + 5h,
                                     created + 2h + 30min, 20.0, "PAY-1",
                                     smartpark::ReservationStatus::Confirmed);
    expect(confirmed.isOpen(), "confirmed reservation is open");
    expect(confirmed.checkIn(), "confirmed reservation can check in");
    expect(!confirmed.cancel(), "checked-in reservation cannot be cancelled");
    expect(!confirmed.markNoShow(), "checked-in reservation cannot become a no-show");
    expect(confirmed.complete(), "checked-in reservation completes on exit");
    expect(!confirmed.isOpen(), "completed reservation is closed");
    expect(!confirmed.complete(), "completed reservation is terminal");

    smartpark::Reservation noShow("R-N", u8"晋A12345", smartpark::VehicleType::Car,
                                  "A001", created, created + 2h, created + 5h,
                                  created + 2h + 30min, 20.0, "PAY-1",
                                  smartpark::ReservationStatus::Confirmed);
    expect(noShow.markNoShow(), "confirmed reservation can become a no-show");
    expect(!noShow.markNoShow(), "no-show reservation is terminal");

    smartpark::Reservation cancelled("R-X", u8"晋A12345", smartpark::VehicleType::Car,
                                     "A001", created, created + 2h, created + 5h,
                                     created + 2h + 30min, 20.0, "PAY-1",
                                     smartpark::ReservationStatus::Confirmed);
    expect(cancelled.cancel(), "confirmed reservation can be cancelled");
    expect(!cancelled.isOpen(), "cancelled reservation is closed");

    expectThrows<std::invalid_argument>(
        [=] { smartpark::Reservation("", u8"晋A12345", smartpark::VehicleType::Car,
                                     "A001", created, created + 2h, created + 5h,
                                     created + 2h + 30min, 20.0, "PAY-1"); },
        "reservation rejects an empty id");
    expectThrows<std::invalid_argument>(
        [=] { smartpark::Reservation("R-1", u8"晋A12345", smartpark::VehicleType::Car,
                                     "A001", created + 3h, created + 2h, created + 5h,
                                     created + 2h + 30min, 20.0, "PAY-1"); },
        "reservation rejects a start time before its creation");
    expectThrows<std::invalid_argument>(
        [=] { smartpark::Reservation("R-1", u8"晋A12345", smartpark::VehicleType::Car,
                                     "A001", created, created + 5h, created + 2h,
                                     created + 2h + 30min, 20.0, "PAY-1"); },
        "reservation rejects an end time before its start");
}

void testReservationCreateAndConflicts(){
    using namespace std::chrono_literals;
    const auto layout = smartpark::ParkingLayout::fromDescription(reservationTestLayout);
    const auto now = reservationTestNow;
    smartpark::ParkingService service(layout, smartpark::AllocationStrategy::Nearest);
    auto &reservations = service.reservations();

    // 先占住其余车位，使后续预约只能选剩下的车位，再验证窗口冲突。
    expect(service.enter({u8"晋A77777", smartpark::VehicleType::Car}, now).has_value(),
           "conflict test parks the first blocker");
    expect(service.enter({u8"晋A88888", smartpark::VehicleType::Car}, now).has_value(),
           "conflict test parks the second blocker");
    const auto first = reservations.create({u8"晋A12345", smartpark::VehicleType::Car},
                                           now + 2h, now + 5h, now);
    expect(first.has_value(), "time-slot reservation can be created");
    expect(first && first->reservation.status() == smartpark::ReservationStatus::Confirmed,
           "paid time-slot reservation is confirmed");
    expect(first && first->allocation.spotId == first->reservation.spotId(),
           "time-slot reservation returns the reserved spot");
    expect(first && !first->reservation.expectedRoute().empty(),
           "time-slot reservation returns an expected route snapshot");
    const auto *firstSpot = first ? findSpot(service, first->reservation.spotId()) : nullptr;
    expect(firstSpot != nullptr && firstSpot->status() == smartpark::SpotStatus::Available,
           "delayed locking keeps the spot physically available at creation");
    expect(service.reservedSpots() == 0,
           "no physical spot is reserved before the arrival window");
    expect(std::abs(reservations.heldDeposits() - 20.0) < 1e-9,
           "deposit is charged on creation");

    expect(!reservations.create({u8"晋A12345", smartpark::VehicleType::Car},
                                now + 6h, now + 8h, now).has_value(),
           "same plate cannot hold two open time-slot reservations");
    expect(!reservations.create({u8"晋A22222", smartpark::VehicleType::Car},
                                now + 4h, now + 6h, now).has_value(),
           "overlapping window on the reserved spot is rejected");
    expect(reservations.create({u8"晋A22222", smartpark::VehicleType::Car},
                               now + 5h, now + 8h, now).has_value(),
           "adjacent window on the same spot is accepted");
    expect(service.leave(u8"晋A77777", now + 35min).has_value(),
           "conflict test releases the first blocker");
    expect(reservations.create({u8"晋A33333", smartpark::VehicleType::Car},
                               now + 2h, now + 4h, now).has_value(),
           "same window on another spot is accepted");
    expect(service.leave(u8"晋A88888", now + 35min).has_value(),
           "conflict test releases the second blocker");
    expect(std::abs(reservations.heldDeposits() - 60.0) < 1e-9,
           "each reservation charges its own deposit");

    expect(!service.createBooking({u8"晋A12345", smartpark::VehicleType::Car},
                                  now + 1h, now).has_value(),
           "plate with an open time-slot reservation cannot create a booking");

    expect(service.enter({u8"晋A77777", smartpark::VehicleType::Car}, now).has_value(),
           "parked-plate test enters a vehicle");
    expect(!reservations.create({u8"晋A77777", smartpark::VehicleType::Car},
                                now + 2h, now + 4h, now).has_value(),
           "parked plate cannot create a time-slot reservation");
    expect(service.leave(u8"晋A77777", now + 35min).has_value(),
           "parked-plate test leaves the vehicle");

    expect(!reservations.create({u8"晋A55555", smartpark::VehicleType::Car},
                                now + 15min, now + 2h, now).has_value(),
           "reservation below the minimum lead time is rejected");
    expect(!reservations.create({u8"晋A55555", smartpark::VehicleType::Car},
                                now + 2h, now + 2h + 15min, now).has_value(),
           "reservation below the minimum duration is rejected");
    expect(!reservations.create({u8"晋A55555", smartpark::VehicleType::Car},
                                now + std::chrono::hours(8 * 24),
                                now + std::chrono::hours(8 * 24) + 2h, now).has_value(),
           "reservation beyond the 7-day horizon is rejected");
    expect(!reservations.lastError().empty(), "failed creation reports a reason");

    expect(service.createBooking({u8"晋A88888", smartpark::VehicleType::Car},
                                 now + 1h, now).has_value(),
           "booking works for a plate without time-slot reservations");
    expect(!reservations.create({u8"晋A88888", smartpark::VehicleType::Car},
                                now + 3h, now + 5h, now).has_value(),
           "plate with an active booking cannot create a time-slot reservation");
}

void testReservationDelayedLockCheckInAndDepositDeduction(){
    using namespace std::chrono_literals;
    const auto layout = smartpark::ParkingLayout::fromDescription(reservationTestLayout);
    const auto now = reservationTestNow;
    smartpark::ParkingService service(layout, smartpark::AllocationStrategy::Nearest);
    auto &reservations = service.reservations();

    const auto created = reservations.create({u8"晋A12345", smartpark::VehicleType::Car},
                                             now + 2h, now + 5h, now);
    expect(created.has_value(), "delayed-lock test can create a reservation");
    if (!created){
        return;
    }
    const std::string spotId = created->reservation.spotId();
    reservations.sweep(now + 91min);
    const auto *lockedSpot = findSpot(service, spotId);
    expect(lockedSpot != nullptr
               && lockedSpot->status() == smartpark::SpotStatus::Reserved,
           "arrival window locks the physical spot");
    expect(lockedSpot && lockedSpot->reservationExpiresAt()
               && *lockedSpot->reservationExpiresAt() == now + 2h + 30min,
           "short-term lock expires at the grace deadline");
    expect(service.reservedSpots() == 1, "locked spot is counted as reserved");
    expect(!reservations.checkIn(u8"晋A12345", now + 30min).has_value(),
           "check-in before the arrival window is rejected");

    const auto arrived = reservations.checkIn(u8"晋A12345", now + 2h);
    expect(arrived.has_value(), "check-in at the start time succeeds");
    expect(arrived && arrived->spotId == spotId,
           "check-in occupies the reserved spot");
    expect(service.activeRecord(u8"晋A12345").has_value(),
           "check-in creates a parking record");
    expect(reservationById(reservations, created->reservation.id())
               .has_value()
           && reservationById(reservations, created->reservation.id())->status()
                  == smartpark::ReservationStatus::CheckedIn,
           "check-in moves the reservation to checked-in state");
    expect(std::abs(reservations.heldDeposits() - 20.0) < 1e-9,
           "deposit is held as prepaid credit after check-in");

    // 3 小时停车：免费 30 分钟后按 5 个计费单元收费 25 元，定金抵扣 20 元。
    const auto closed = service.leave(u8"晋A12345", now + 5h);
    expect(closed.has_value(), "reserved vehicle can leave");
    expect(closed && std::abs(closed->fee() - 5.0) < 1e-9,
           "deposit is deducted from the exit fee");
    expect(reservationById(reservations, created->reservation.id())
               .has_value()
           && reservationById(reservations, created->reservation.id())->status()
                  == smartpark::ReservationStatus::Completed,
           "exit completes the reservation");
    expect(std::abs(reservations.appliedDeposits() - 20.0) < 1e-9,
           "applied deposit is recorded");
    expect(reservations.heldDeposits() == 0.0, "no deposit stays held after exit");
    const auto *releasedSpot = findSpot(service, spotId);
    expect(releasedSpot && releasedSpot->status() == smartpark::SpotStatus::Available,
           "spot is available again after exit");

    // 提前到场：开始前 15 分钟（锁位窗口内）允许到场。
    const auto second = reservations.create({u8"晋A22222", smartpark::VehicleType::Car},
                                            now + 6h, now + 8h, now);
    expect(second.has_value(), "second reservation can be created");
    const auto earlyArrival = reservations.checkIn(u8"晋A22222", now + 5h + 45min);
    expect(earlyArrival.has_value(),
           "check-in within the lock window accepts an early arrival");
    expect(reservations.findCheckedIn(u8"晋A22222").has_value(),
           "early arrival checks the reservation in");

    // 开始前超过锁位窗口：拒绝。
    const auto third = reservations.create({u8"晋A33333", smartpark::VehicleType::Car},
                                           now + 10h, now + 12h, now);
    expect(third.has_value(), "third reservation can be created");
    expect(!reservations.checkIn(u8"晋A33333", now + 9h).has_value(),
           "check-in before the lock window is rejected");
}

void testReservationNoShowAndCancel(){
    using namespace std::chrono_literals;
    const auto layout = smartpark::ParkingLayout::fromDescription(reservationTestLayout);
    const auto now = reservationTestNow;
    smartpark::ParkingService service(layout, smartpark::AllocationStrategy::Nearest);
    auto &reservations = service.reservations();

    const auto cancelled = reservations.create({u8"晋A12345", smartpark::VehicleType::Car},
                                               now + 2h, now + 4h, now);
    expect(cancelled.has_value(), "cancel test can create a reservation");
    expect(reservations.cancel(u8"晋A12345", now + 1h),
           "reservation can be cancelled before its start");
    expect(reservationById(reservations, cancelled->reservation.id())
               .has_value()
           && reservationById(reservations, cancelled->reservation.id())->status()
                  == smartpark::ReservationStatus::Cancelled,
           "cancelled reservation has cancelled status");
    expect(reservationById(reservations, cancelled->reservation.id())
               .has_value()
           && reservationById(reservations, cancelled->reservation.id())->depositState()
                  == smartpark::DepositState::Refunded,
           "cancellation refunds the deposit");
    expect(std::abs(reservations.refundedDeposits() - 20.0) < 1e-9,
           "refund is recorded");
    expect(reservations.heldDeposits() == 0.0, "no deposit stays held after refund");
    expect(!reservations.cancel(u8"晋A12345", now + 1h),
           "resolved reservation cannot be cancelled twice");

    // 预约开始后不允许取消；该预约放在远端时段，避免影响后续爽约扫描。
    const auto lateCancel = reservations.create({u8"晋A22222", smartpark::VehicleType::Car},
                                                now + 10h, now + 12h, now);
    expect(lateCancel.has_value(), "late-cancel test can create a reservation");
    expect(!reservations.cancel(u8"晋A22222", now + 10h + 1min),
           "cancellation after the start time is rejected");

    const auto noShow = reservations.create({u8"晋A33333", smartpark::VehicleType::Car},
                                            now + 2h, now + 4h, now);
    expect(noShow.has_value(), "no-show test can create a reservation");
    if (!noShow){
        return;
    }
    const std::string spotId = noShow->reservation.spotId();
    reservations.sweep(now + 91min);
    expect(findSpot(service, spotId)
               && findSpot(service, spotId)->status() == smartpark::SpotStatus::Reserved,
           "no-show scenario locks the spot in the arrival window");
    reservations.sweep(now + 2h + 30min + 1min);
    expect(reservationById(reservations, noShow->reservation.id())
               .has_value()
           && reservationById(reservations, noShow->reservation.id())->status()
                  == smartpark::ReservationStatus::NoShow,
           "reservation past the grace deadline becomes a no-show");
    expect(reservationById(reservations, noShow->reservation.id())
               .has_value()
           && reservationById(reservations, noShow->reservation.id())->depositState()
                  == smartpark::DepositState::Forfeited,
           "no-show forfeits the deposit");
    expect(std::abs(reservations.forfeitedDeposits() - 20.0) < 1e-9,
           "forfeited deposit is recorded");
    const auto *freedSpot = findSpot(service, spotId);
    expect(freedSpot && freedSpot->status() == smartpark::SpotStatus::Available,
           "no-show releases the locked spot");
    // 被拒的迟到取消（晋A22222）仍是有效预约且已进入锁位窗口，其车位保持短时锁。
    expect(service.remainingSpots() == 2,
           "no-show returns its spot while the rejected cancellation keeps its lock");
}

void testReservationPaymentFailure(){
    using namespace std::chrono_literals;
    const auto layout = smartpark::ParkingLayout::fromDescription(reservationTestLayout);
    const auto now = reservationTestNow;
    smartpark::ParkingService service(layout, smartpark::AllocationStrategy::Nearest);
    auto &reservations = service.reservations();

    reservations.failNextDepositCharge();
    expect(!reservations.create({u8"晋A12345", smartpark::VehicleType::Car},
                                now + 2h, now + 5h, now).has_value(),
           "failed deposit charge rejects the reservation");
    expect(!reservations.lastError().empty(),
           "payment failure reports a reason");
    expect(reservations.reservations().empty(),
           "payment failure does not keep a partial order");
    expect(reservations.payments().empty(),
           "payment failure does not record a charge");
    expect(service.remainingSpots() == 3,
           "payment failure leaves all spots untouched");

    const auto retried = reservations.create({u8"晋A12345", smartpark::VehicleType::Car},
                                             now + 2h, now + 5h, now);
    expect(retried.has_value(), "retry after a failed charge succeeds");
}

void testReservationEnterAutoCheckIn(){
    using namespace std::chrono_literals;
    const auto layout = smartpark::ParkingLayout::fromDescription(reservationTestLayout);
    const auto now = reservationTestNow;
    smartpark::ParkingService service(layout, smartpark::AllocationStrategy::Nearest);
    auto &reservations = service.reservations();

    const auto created = reservations.create({u8"晋A12345", smartpark::VehicleType::Car},
                                             now + 2h, now + 4h, now);
    expect(created.has_value(), "auto check-in test can create a reservation");
    if (!created){
        return;
    }
    const std::string spotId = created->reservation.spotId();
    reservations.sweep(now + 91min);
    const auto entered = service.enter({u8"晋A12345", smartpark::VehicleType::Car},
                                       now + 2h);
    expect(entered.has_value(), "enter with a reserved plate succeeds");
    expect(entered && entered->spotId == spotId,
           "enter occupies the reserved spot automatically");
    expect(reservations.findCheckedIn(u8"晋A12345").has_value(),
           "enter checks the reservation in by plate match");
    expect(!service.enter({u8"晋A12345", smartpark::VehicleType::Car}, now + 3h).has_value(),
           "same plate cannot enter twice");
    // 2 小时停车：免费 30 分钟后 90 分钟 = 3 个计费单元 = 15 元 < 定金 20 元，
    // 定金抵扣后实收 0 元（余额不退）。
    const auto closed = service.leave(u8"晋A12345", now + 4h);
    expect(closed.has_value() && std::abs(closed->fee()) < 1e-9,
           "exit fee can be fully covered by the deposit");
}

void testReservationPersistenceAcrossRestart(){
    using namespace std::chrono_literals;
    const auto layout = smartpark::ParkingLayout::fromDescription(reservationTestLayout);
    const auto now = reservationTestNow;
    QTemporaryDir directory;
    expect(directory.isValid(), "reservation persistence test can create a directory");
    const QString databasePath = directory.filePath("reservations.db");
    {
        smartpark::Persistence persistence(databasePath);
        smartpark::ParkingService service(layout, smartpark::AllocationStrategy::Nearest,
                                          &persistence.repository());
        auto &reservations = service.reservations();
        // 爽约：锁位后超过宽限期。
        const auto noShow = reservations.create({u8"晋A11111", smartpark::VehicleType::Car},
                                                now + 2h, now + 4h, now);
        expect(noShow.has_value(), "persistence test creates the no-show order");
        reservations.sweep(now + 91min);
        reservations.sweep(now + 2h + 31min);
        // 取消退定金。
        expect(reservations.create({u8"晋A22222", smartpark::VehicleType::Car},
                                   now + 5h, now + 7h, now).has_value(),
               "persistence test creates the cancelled order");
        expect(reservations.cancel(u8"晋A22222", now), "persistence test cancels an order");
        // 完成并抵扣：3 小时停车，25 元减定金 20 元。
        const auto completed = reservations.create({u8"晋A33333", smartpark::VehicleType::Car},
                                                   now + 8h, now + 11h, now);
        expect(completed.has_value(), "persistence test creates the completed order");
        reservations.sweep(now + 7h + 31min);
        expect(reservations.checkIn(u8"晋A33333", now + 8h).has_value(),
               "persistence test checks the completed order in");
        expect(service.leave(u8"晋A33333", now + 11h).has_value(),
               "persistence test closes the completed record");
        // 重启前保持开放的确认订单。
        expect(reservations.create({u8"晋A44444", smartpark::VehicleType::Car},
                                   now + 13h, now + 15h, now).has_value(),
               "persistence test creates the open order");
        // 重启前处于 CheckedIn 的订单；到场时间须在上一单宽限期截止
        //（开始时间 + 30 分钟）之前，避免到场扫描把开放订单判成爽约。
        expect(reservations.create({u8"晋A55555", smartpark::VehicleType::Car},
                                   now + 14h, now + 17h, now).has_value(),
               "persistence test creates the checked-in order");
        expect(reservations.checkIn(u8"晋A55555", now + 13h + 30min).has_value(),
               "persistence test checks the last order in");
    }
    smartpark::Persistence restored(databasePath);
    smartpark::ParkingService service(layout, smartpark::AllocationStrategy::Nearest,
                                      &restored.repository());
    auto &reservations = service.reservations();
    expect(reservations.reservations().size() == 5,
           "restart restores every time-slot reservation");
    expect(reservations.payments().size() == 8,
           "restart restores every deposit payment");
    const auto openOrder = reservations.findOpen(u8"晋A44444");
    expect(openOrder.has_value()
               && openOrder->status() == smartpark::ReservationStatus::Confirmed,
           "restart restores the open confirmed order");
    expect(!reservations.create({u8"晋A44444", smartpark::VehicleType::Car},
                                now + 20h, now + 22h, now).has_value(),
           "restored open order still blocks duplicate reservations for its plate");
    const auto checkedInOrder = reservations.findCheckedIn(u8"晋A55555");
    expect(checkedInOrder.has_value()
               && checkedInOrder->status() == smartpark::ReservationStatus::CheckedIn,
           "restart restores the checked-in order");
    expect(service.activeRecord(u8"晋A55555").has_value(),
           "restart restores the checked-in parking record");
    expect(std::abs(reservations.forfeitedDeposits() - 20.0) < 1e-9,
           "restart keeps the forfeited deposit total");
    expect(std::abs(reservations.refundedDeposits() - 20.0) < 1e-9,
           "restart keeps the refunded deposit total");
    expect(std::abs(reservations.appliedDeposits() - 20.0) < 1e-9,
           "restart keeps the applied deposit total");
    expect(std::abs(reservations.heldDeposits() - 40.0) < 1e-9,
           "restart keeps the held deposit total");
    // 重启后继续业务：确认订单到场并完成，CheckedIn 订单离场结算。
    expect(reservations.checkIn(u8"晋A44444", now + 13h).has_value(),
           "restored confirmed order can check in after restart");
    const auto exitFee = service.leave(u8"晋A55555", now + 16h + 30min);
    expect(exitFee.has_value() && std::abs(exitFee->fee() - 5.0) < 1e-9,
           "restored checked-in order deducts the deposit on exit");
}

// ================= 数据分析（本地小模型 + 预留远程 API） =================
void testAnalyticsLinearModel(){
    using namespace std::chrono_literals;
    const auto base = reservationTestNow;
    std::vector<smartpark::OccupancySample> samples;
    for (int index = 0; index < 10; ++index){
        samples.push_back({base + std::chrono::hours(index), 0.1 * index});
    }
    const auto trend = smartpark::AnalyticsEngine::fitLinear(samples);
    expect(trend.valid(), "OLS fit is valid on a linear series");
    expect(std::abs(trend.slope - 0.1) < 1e-9, "OLS recovers the slope");
    expect(std::abs(trend.intercept) < 1e-9, "OLS recovers the intercept");
    expect(trend.r2 > 0.999, "linear series fits with R2 near 1");
    expect(!smartpark::AnalyticsEngine::fitLinear({samples.front(), samples[1]}).valid(),
           "OLS needs at least three samples");
    const auto flat = smartpark::AnalyticsEngine::fitLinear(
        std::vector<smartpark::OccupancySample>(10, {base, 0.5}));
    expect(flat.valid() && std::abs(flat.slope) < 1e-12 && std::abs(flat.intercept - 0.5) < 1e-9,
           "constant series yields a flat trend");
}

void testAnalyticsReportConclusions(){
    using namespace std::chrono_literals;
    const std::string description =
        "site 60 30\n"
        "entrance 0 15\n"
        "exit 60 15\n"
        "region A 10 10 10 2 1.2 5.5 6 left\n";
    const auto layout = smartpark::ParkingLayout::fromDescription(description);
    const auto now = reservationTestNow;
    smartpark::ParkingService service(layout);
    expect(service.enter({u8"晋A10001", smartpark::VehicleType::Car}, now - 90min).has_value(),
           "analytics setup entry 1");
    expect(service.enter({u8"晋A10002", smartpark::VehicleType::Car}, now - 90min).has_value(),
           "analytics setup entry 2");
    expect(service.leave(u8"晋A10001", now - 30min).has_value(), "analytics setup exit 1");
    expect(service.enter({u8"晋A10003", smartpark::VehicleType::Car}, now - 60min).has_value(),
           "analytics setup entry 3");
    expect(service.leave(u8"晋A10003", now - 10min).has_value(), "analytics setup exit 2");
    expect(service.enter({u8"晋A10004", smartpark::VehicleType::Car}, now - 30min).has_value(),
           "analytics setup entry 4");
    expect(service.enter({u8"晋A10005", smartpark::VehicleType::Car}, now).has_value(),
           "analytics setup entry 5");

    smartpark::AnalyticsEngine engine(service);
    const auto report = engine.analyze(now);
    expect(report.model == smartpark::AnalyticsEngine::modelName(),
           "report records the local model name");
    expect(!report.summary.empty(), "report produces a summary");
    expect(!report.findings.empty(), "report produces findings");
    expect(report.forecast.size() == static_cast<std::size_t>(
               smartpark::AnalyticsEngine::kForecastHorizonHours),
           "forecast covers the six-hour horizon");
    expect(std::abs(report.currentOccupancyRate - 3.0 / 20.0) < 1e-9,
           "report reflects the current occupancy rate");
    expect(report.peakEntryHour >= 0, "report detects an entry peak hour");
    expect(std::abs(report.totalRevenue - 10.0) < 1e-9,
           "report reflects the collected revenue");
    expect(!report.recommendations.empty(), "report produces recommendations");
    for (const auto &point : report.forecast){
        expect(point.second >= 0.0 && point.second <= 100.0,
               "forecast stays within 0-100 percent");
    }

    // 空数据时诚实给出数据质量提示，而不是编造结论。
    smartpark::ParkingService emptyService(layout);
    smartpark::AnalyticsEngine emptyEngine(emptyService);
    const auto emptyReport = emptyEngine.analyze(now);
    const bool hasDataQuality = std::any_of(
        emptyReport.findings.begin(), emptyReport.findings.end(),
        [](const smartpark::AnalysisFinding &finding){
            return finding.category == smartpark::AnalysisFinding::Category::DataQuality;
        });
    expect(hasDataQuality, "empty service reports a data-quality finding");
    expect(emptyReport.summary.find("不足") != std::string::npos,
           "empty data summary admits insufficient history");
}

void testRemoteAnalystInterface(){
    smartpark::RemoteAnalystConfig config;
    config.endpoint = "https://api.example.com/v1/chat/completions";
    config.model = "glm-4-flash";
    smartpark::RemoteAnalystClient client(config);
    expect(client.configured(), "client with endpoint is configured");

    smartpark::OperationalSnapshot snapshot;
    snapshot.capacity = 20;
    snapshot.occupied = 5;
    snapshot.occupancyRate = 0.25;
    snapshot.closedRecords = 3;
    snapshot.totalRevenue = 10.0;
    expect(!client.analyze(snapshot).has_value(),
           "analyze returns nothing before a transport is injected");

    std::string capturedUrl;
    std::string capturedRequest;
    client.setTransport([&](const std::string &url, const std::string &apiKey,
                            const std::string &requestJson) -> std::optional<std::string> {
        capturedUrl = url;
        capturedRequest = requestJson;
        return std::string(
            R"({"choices":[{"message":{"content":"{\"summary\":\"占用率上升，建议扩容。\",\"recommendations\":[\"扩充 B 区\"]}"}}]})");
    });
    const auto report = client.analyze(snapshot, reservationTestNow);
    expect(report.has_value(), "injected transport produces a report");
    expect(report && report->model == "glm-4-flash", "remote report records the model id");
    expect(report && report->summary == "占用率上升，建议扩容。",
           "remote conclusion is parsed from the model output");
    expect(report && report->recommendations.size() == 1
               && report->recommendations.front() == "扩充 B 区",
           "remote recommendations are parsed");
    expect(capturedUrl == config.endpoint, "transport receives the configured endpoint");
    expect(capturedRequest.find("capacity") != std::string::npos
               && capturedRequest.find("occupancySeriesLast72h") != std::string::npos,
           "request carries aggregated metrics only");

    smartpark::RemoteAnalystClient unconfigured;
    expect(!unconfigured.configured(), "default client has no endpoint");
}


// ================= 创新功能（应急/无障碍/审计/寻车/演示） =================
void testAuditHashChain(){
    using namespace std::chrono_literals;
    const std::string description =
        "site 60 30\n"
        "entrance 0 15\n"
        "exit 60 15\n"
        "region A 10 10 1 3 1.2 5.5 6 left\n";
    const auto layout = smartpark::ParkingLayout::fromDescription(description);
    QTemporaryDir directory;
    expect(directory.isValid(), "audit test can create a directory");
    const QString databasePath = directory.filePath("audit.db");
    smartpark::Persistence persistence(databasePath);
    smartpark::AuditLogService audit(persistence.databaseManager().database());
    expect(audit.lastError().empty(), "audit service opens");
    expect(audit.record("system", "vehicle_enter", u8"晋A12345@A001"),
           "audit records the first entry");
    expect(audit.record("system", "reservation_cancel", u8"晋A12345"),
           "audit records a second entry");
    expect(audit.record("op01", "login_success", ""), "audit records a third entry");
    const auto result = audit.verifyChain();
    expect(result.ok && result.checked == 3, "hash chain verifies after normal writes");
    // 篡改中间记录的 detail，链应断裂并定位到该行。
    QSqlQuery tamper(persistence.databaseManager().database());
    expect(tamper.exec(QStringLiteral(
               "UPDATE audit_logs SET detail='篡改' WHERE id=2")),
           "audit test can tamper a row");
    const auto broken = audit.verifyChain();
    expect(!broken.ok && broken.brokenAtId == 2,
           "tampering is detected at the modified row");
}

void testAuditIntegrationHooks(){
    using namespace std::chrono_literals;
    const std::string description =
        "site 60 30\n"
        "entrance 0 15\n"
        "exit 60 15\n"
        "region A 10 10 1 3 1.2 5.5 6 left\n";
    const auto layout = smartpark::ParkingLayout::fromDescription(description);
    QTemporaryDir directory;
    expect(directory.isValid(), "audit integration test can create a directory");
    smartpark::Persistence persistence(directory.filePath("audit-hook.db"));
    smartpark::AuditLogService audit(persistence.databaseManager().database());
    const auto now = reservationTestNow;
    smartpark::ParkingService service(layout, smartpark::AllocationStrategy::Nearest,
                                      &persistence.repository());
    service.setAuditLog(&audit);
    expect(service.enter({u8"晋A10001", smartpark::VehicleType::Car}, now).has_value(),
           "audit integration can enter a vehicle");
    expect(service.leave(u8"晋A10001", now + 35min).has_value(),
           "audit integration can leave a vehicle");
    expect(service.reservations().create({u8"晋A20001", smartpark::VehicleType::Car},
                                         now + 2h, now + 4h, now).has_value(),
           "audit integration can create a reservation");
    expect(service.reservations().cancel(u8"晋A20001", now + 30min),
           "audit integration can cancel a reservation");
    const auto entries = audit.recent(20);
    auto hasAction = [&entries](const char *action){
        return std::any_of(entries.begin(), entries.end(),
                           [action](const smartpark::AuditLogService::Entry &entry){
                               return entry.action == action;
                           });
    };
    expect(hasAction("vehicle_enter") && hasAction("vehicle_exit")
               && hasAction("reservation_create") && hasAction("reservation_cancel"),
           "business actions are audited");
    expect(audit.verifyChain().ok, "audit chain stays consistent after integration");
}

void testEmergencyEnterEviction(){
    using namespace std::chrono_literals;
    const std::string description =
        "site 60 30\n"
        "entrance 0 15\n"
        "exit 60 15\n"
        "region A 10 10 1 3 1.2 5.5 6 left\n";
    const auto layout = smartpark::ParkingLayout::fromDescription(description);
    QTemporaryDir directory;
    expect(directory.isValid(), "emergency test can create a directory");
    smartpark::Persistence persistence(directory.filePath("emergency.db"));
    smartpark::AuditLogService audit(persistence.databaseManager().database());
    const auto now = reservationTestNow;
    smartpark::ParkingService service(layout, smartpark::AllocationStrategy::Nearest,
                                      &persistence.repository());
    service.setAuditLog(&audit);
    for (int index = 0; index < 3; ++index){
        expect(service.enter({std::string(u8"晋A3000") + std::to_string(index),
                              smartpark::VehicleType::Car}, now).has_value(),
               "emergency test fills the lot");
    }
    const auto rejected = service.emergencyEnter(
        {u8"晋A911", smartpark::VehicleType::Car}, false, now);
    expect(!rejected.has_value(),
           "emergency entry without eviction fails when the lot is full");
    const auto arrived = service.emergencyEnter(
        {u8"晋A911", smartpark::VehicleType::Car}, true, now + 1min);
    expect(arrived.has_value(), "emergency entry evicts the nearest vehicle");
    expect(arrived && arrived->exitRoute.distance > 0.0,
           "emergency allocation includes an exit route");
    expect(service.activeRecord(u8"晋A911").has_value(),
           "emergency vehicle holds a record");
    const auto entries = audit.recent(20);
    const bool hasEmergency = std::any_of(
        entries.begin(), entries.end(),
        [](const smartpark::AuditLogService::Entry &entry){
            return entry.action == "emergency_enter";
        });
    const bool hasEvict = std::any_of(
        entries.begin(), entries.end(),
        [](const smartpark::AuditLogService::Entry &entry){
            return entry.action == "emergency_evict";
        });
    expect(hasEmergency && hasEvict, "emergency and eviction are audited");
}

void testAccessibleReservationBenefits(){
    using namespace std::chrono_literals;
    const std::string description =
        "site 60 30\n"
        "entrance 0 15\n"
        "exit 60 15\n"
        "region A 10 10 1 1 1.2 5.5 6 left accessible\n"
        "region B 26 10 1 2 1.2 5.5 6 left normal\n";
    const auto layout = smartpark::ParkingLayout::fromDescription(description);
    QTemporaryDir directory;
    expect(directory.isValid(), "accessible test can create a directory");
    const QString databasePath = directory.filePath("accessible.db");
    smartpark::Reservation *unused = nullptr;
    (void)unused;
    {
        smartpark::Persistence persistence(databasePath);
        smartpark::ParkingService service(layout, smartpark::AllocationStrategy::Nearest,
                                          &persistence.repository());
        auto &reservations = service.reservations();
        const auto created = reservations.create(
            {u8"晋A88888", smartpark::VehicleType::Car},
            reservationTestNow + 2h, reservationTestNow + 5h, reservationTestNow,
            true);
        expect(created.has_value(), "accessible reservation can be created");
        expect(created && created->reservation.deposit() == 0.0,
               "accessible reservation is deposit-free");
        expect(created && created->reservation.isAccessible(),
               "accessible flag is stored on the order");
        expect(std::abs(reservations.heldDeposits()) < 1e-9,
               "no deposit is charged for accessible orders");
        expect(reservations.payments().empty(),
               "no charge payment is recorded for accessible orders");
        const auto *spot = created ? findSpot(service, created->reservation.spotId())
                                   : nullptr;
        expect(spot != nullptr && spot->type() == smartpark::SpotType::Accessible,
               "accessible reservation only occupies an accessible spot");
        const auto *normalSpot = findSpot(service, std::string("A002"));
        expect(normalSpot == nullptr || normalSpot->zone() != "A",
               "identifier prefix no longer implies the zone");
        // 普通预约仍收定金。
        const auto normal = reservations.create(
            {u8"晋A77777", smartpark::VehicleType::Car},
            reservationTestNow + 2h, reservationTestNow + 4h, reservationTestNow);
        expect(normal && std::abs(normal->reservation.deposit() - 20.0) < 1e-9,
               "normal reservation keeps its deposit");
    }
    {
        smartpark::Persistence persistence(databasePath);
        smartpark::ParkingService service(layout, smartpark::AllocationStrategy::Nearest,
                                          &persistence.repository());
        const auto open = service.reservations().findOpen(u8"晋A88888");
        expect(open.has_value() && open->isAccessible(),
               "restart restores the accessible flag");
        expect(open && std::abs(open->deposit()) < 1e-9,
               "restart keeps the deposit-free benefit");
    }
}

void testFindCarPedestrianRoute(){
    using namespace std::chrono_literals;
    const std::string description =
        "site 60 30\n"
        "entrance 0 15\n"
        "exit 60 15\n"
        "region A 10 10 1 3 1.2 5.5 6 left\n"
        "obstacle 40 20 4 4 机房\n";
    const auto layout = smartpark::ParkingLayout::fromDescription(description);
    smartpark::ParkingService service(layout);
    const auto now = reservationTestNow;
    expect(service.enter({u8"晋A40001", smartpark::VehicleType::Car}, now).has_value(),
           "find-car test can enter a vehicle");
    const auto found = service.findCar(u8"晋A40001");
    expect(found.has_value(), "findCar locates a parked plate");
    expect(found && found->spotId == service.activeRecord(u8"晋A40001")->spotId(),
           "findCar points at the occupied spot");
    expect(found && !found->walkRoute.points.empty() && found->walkRoute.distance > 0.0,
           "findCar returns a walking route");
    expect(found && found->zone == "A", "findCar reports the zone");
    expect(!service.findCar(u8"晋A40002").has_value(),
           "findCar rejects plates that are not on site");
}

void testDemoDirectorScript(){
    smartpark::ParkingService service(smartpark::ParkingLayout::defaultLayout(),
                                      smartpark::AllocationStrategy::WeightedCost);
    smartpark::DemoDirector director(service);
    const int total = director.totalSteps();
    expect(total > 5, "demo script has multiple steps");
    int executed = 0;
    while (director.step()){
        ++executed;
        expect(!director.lastDescription().empty(),
               "every demo step produces a description");
    }
    expect(executed == total, "demo script runs every step exactly once");
    expect(!director.lastAnalysisSummary().empty(),
           "demo script ends with an analysis summary");
}

void testFakePaymentGateway(){
    using namespace std::chrono_literals;
    smartpark::FakePaymentGateway gateway;
    const auto now = reservationTestNow;
    const auto charge = gateway.charge("R-1", u8"晋A12345", 20.0, now);
    expect(charge.has_value() && !charge->empty(),
           "fake gateway charges a deposit and returns a transaction id");
    expect(charge && charge->rfind("PAY-", 0) == 0,
           "charge transaction ids use the PAY prefix");
    expect(!gateway.charge("", u8"晋A12345", 20.0, now).has_value(),
           "fake gateway rejects a charge without a reservation id");
    expect(!gateway.charge("R-1", u8"晋A12345", -1.0, now).has_value(),
           "fake gateway rejects a negative amount");
    expect(gateway.refund(*charge, 20.0, now).has_value(),
           "fake gateway refunds against the charge transaction");
    expect(gateway.forfeit(*charge, 20.0, now).has_value(),
           "fake gateway forfeits against the charge transaction");
    gateway.failNextCharge();
    expect(!gateway.charge("R-2", u8"晋A12345", 20.0, now).has_value(),
           "injected charge failure is deterministic");
    const auto afterFailure = gateway.charge("R-2", u8"晋A12345", 20.0, now);
    expect(afterFailure.has_value(),
           "the failure flag resets after one charge");
}
int main(){
    int argc = 0;
    char programName[] = "smartpark_core_tests";
    char *argv[] = {programName, nullptr};
    QCoreApplication app(argc, argv);
    testVehicle();
    testParkingSpotLifecycle();
    testParkingSpotReservation();
    testParkingRecordLifecycle();
    testBillingService();
    testDefaultLayout();
    testCustomLayoutAndAutomaticAllocation();
    testVerticalAislesAndObstacles();
    testGarageFloorplanLayout();
    testLayoutTypesAndMultipleGates();
    testReservationTtlAndConflict();
    testZonePressureBalancing();
    testZonePressureSpreadsLoadAcrossZones();
    testTypeMatching();
    testCongestionAvoidanceAndStrategies();
    testZonePressureBalancing();
    testMultiEntranceSelection();
    testSqlitePersistenceAndRecovery();
    testPersistenceHelperRestoresAcrossRestart();
    testVehicleTypeUpdate();
    testLayoutMismatchRejected();
    testExpiredReservationPersisted();
    testDirtyActiveRecordsRejected();
    testDisabledStateAndExpiryInvariants();
    testLayoutSpotSetValidation();
    testTimeValidationAndSafeConversion();
    testBookingModelLifecycle();
    testBookingCreateConfirmAndRoutes();
    testBookingNoShowForfeitsDeposit();
    testBookingRejectsOutOfWindow();
    testBookingCancelRefunds();
    testBookingPersistenceAcrossRestart();
    testPreviewAllocationReadOnly();
    testParkingInsightEngine();
    testAnalyticsLinearModel();
    testAnalyticsReportConclusions();
    testRemoteAnalystInterface();
    testAuditHashChain();
    testAuditIntegrationHooks();
    testEmergencyEnterEviction();
    testAccessibleReservationBenefits();
    testFindCarPedestrianRoute();
    testDemoDirectorScript();
    testReservationModelLifecycle();
    testReservationCreateAndConflicts();
    testReservationDelayedLockCheckInAndDepositDeduction();
    testReservationNoShowAndCancel();
    testReservationPaymentFailure();
    testReservationEnterAutoCheckIn();
    testReservationPersistenceAcrossRestart();
    testFakePaymentGateway();
    if (failureCount != 0){
        std::cerr << failureCount << " test assertion(s) failed\n";
        return 1;
    }
    std::cout << "All core model tests passed\n";
    return 0;
}
