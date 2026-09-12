#include "core/model/ParkingSpot.h"
#include "core/model/Booking.h"
#include "core/model/ParkingRecord.h"
#include "core/persistence/DatabaseManager.h"
#include "core/persistence/ParkingRepository.h"
#include "core/persistence/Persistence.h"
#include "core/util/TimeUtil.h"
#include "core/model/Vehicle.h"
#include "core/service/ParkingService.h"
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
    testLayoutTypesAndMultipleGates();
    testReservationTtlAndConflict();
    testTypeMatching();
    testCongestionAvoidanceAndStrategies();
    testMultiEntranceSelection();
    testSqlitePersistenceAndRecovery();
    testPersistenceHelperRestoresAcrossRestart();
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
    if (failureCount != 0){
        std::cerr << failureCount << " test assertion(s) failed\n";
        return 1;
    }
    std::cout << "All core model tests passed\n";
    return 0;
}
