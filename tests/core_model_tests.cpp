#include "core/model/ParkingSpot.h"
#include "core/model/ParkingRecord.h"
#include "core/persistence/DatabaseManager.h"
#include "core/persistence/ParkingRepository.h"
#include "core/model/Vehicle.h"
#include "core/service/ParkingService.h"

#include <QTemporaryDir>
#include <QSqlQuery>

#include <algorithm>
#include <chrono>
#include <exception>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace {

int failureCount = 0;

void expect(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failureCount;
    }
}

template<typename Exception, typename Function>
void expectThrows(Function function, const std::string &message)
{
    try {
        function();
        expect(false, message);
    } catch (const Exception &) {
    } catch (...) {
        expect(false, message + " (unexpected exception type)");
    }
}

const smartpark::ParkingSpot *findSpot(const smartpark::ParkingService &service,
                                       const std::string &spotId)
{
    const auto spot = std::find_if(
        service.spots().begin(), service.spots().end(),
        [&spotId](const smartpark::ParkingSpot &item) { return item.identifier() == spotId; });
    if (spot == service.spots().end()) {
        return nullptr;
    }
    return &(*spot);
}

void testVehicle()
{
    const smartpark::Vehicle vehicle(u8"晋A12345", smartpark::VehicleType::Car);

    expect(vehicle.plateNumber() == u8"晋A12345", "vehicle keeps its plate number");
    expect(vehicle.type() == smartpark::VehicleType::Car, "vehicle keeps its type");
    expectThrows<std::invalid_argument>(
        [] { smartpark::Vehicle vehicle("", smartpark::VehicleType::Car); },
        "vehicle rejects an empty plate number");
}

void testParkingSpotLifecycle()
{
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

void testParkingSpotReservation()
{
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

void testParkingRecordLifecycle()
{
    using namespace std::chrono_literals;
    const smartpark::ParkingRecord::TimePoint entry =
        smartpark::ParkingRecord::Clock::from_time_t(1000);
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

void testDefaultLayout()
{
    const smartpark::ParkingLayout layout = smartpark::ParkingLayout::defaultLayout();
    std::set<std::string> identifiers;

    expect(layout.spots().size() == 60, "default layout contains 60 spots");
    expect(layout.regions().size() == 3, "default layout contains three regions");
    expect(layout.entrances().size() == 1 && layout.exits().size() == 1,
           "default layout has a single entrance and exit");
    expect(layout.spots().front().identifier() == "A001", "default layout starts at A001");
    expect(layout.spots().back().identifier() == "A060", "default layout ends at A060");
    for (const smartpark::ParkingSpot &spot : layout.spots()) {
        identifiers.insert(spot.identifier());
        expect(spot.bounds().width > 0.0 && spot.bounds().height > 0.0,
               "each generated spot has positive bounds");
        expect(spot.type() == smartpark::SpotType::Normal, "default spots are normal");
    }
    expect(identifiers.size() == layout.spots().size(), "generated spot identifiers are unique");
}

void testCustomLayoutAndAutomaticAllocation()
{
    const std::string description =
        "site 80 40\n"
        "entrance 0 20\n"
        "exit 80 20\n"
        "region A 5 5 8 2 1.2 5.5 6 left\n"
        "region B 35 15 8 2 1.4 6.0 6 right\n";
    smartpark::ParkingService service(smartpark::ParkingLayout::fromDescription(description));
    expect(service.spots().size() == 32, "custom layout generates all requested spots");

    const smartpark::ParkingRecord::TimePoint entry =
        smartpark::ParkingRecord::Clock::from_time_t(1000);
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

void testLayoutTypesAndMultipleGates()
{
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
        [] {
            smartpark::ParkingLayout::fromDescription(
                "site 40 30\nentrance 0 15\nregion A 10 10 1 1 1.2 5.5 6 left\n");
        },
        "layout rejects a missing exit");
    expectThrows<std::invalid_argument>(
        [] {
            smartpark::ParkingLayout::fromDescription(
                "site 40 30\nentrance 0 15\nexit 40 15\nregion A 10 10 1 1 1.2 5.5 6 left hover\n");
        },
        "layout rejects an unknown spot type");
}

void testReservationTtlAndConflict()
{
    using namespace std::chrono_literals;
    const std::string description =
        "site 40 30\n"
        "entrance 0 15\n"
        "exit 40 15\n"
        "region A 10 10 1 1 1.2 5.5 6 left\n";
    smartpark::ParkingService service(smartpark::ParkingLayout::fromDescription(description));
    const auto now = smartpark::ParkingRecord::Clock::from_time_t(2000);
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

void testTypeMatching()
{
    const std::string description =
        "site 80 40\n"
        "entrance 0 20\n"
        "exit 80 20\n"
        "region A 12 14 2 1 1.2 5.5 6 left normal\n"
        "region B 28 14 2 1 1.2 5.5 6 left charging\n";
    smartpark::ParkingService service(smartpark::ParkingLayout::fromDescription(description));
    const auto now = smartpark::ParkingRecord::Clock::from_time_t(3000);

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

void testCongestionAvoidanceAndStrategies()
{
    const std::string description =
        "site 100 60\n"
        "entrance 0 8\n"
        "exit 100 52\n"
        "region A 36 4 6 2 1.2 5.5 6 left\n"
        "region B 36 40 6 2 1.2 5.5 6 left\n";
    const auto layout = smartpark::ParkingLayout::fromDescription(description);
    const auto now = smartpark::ParkingRecord::Clock::from_time_t(4000);

    smartpark::ParkingService nearest(layout, smartpark::AllocationStrategy::Nearest);
    std::optional<smartpark::AllocationResult> lastNearest;
    for (int index = 0; index < 6; ++index) {
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
    for (int index = 0; index < 5; ++index) {
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

void testMultiEntranceSelection()
{
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
    const auto now = smartpark::ParkingRecord::Clock::from_time_t(5000);
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

void testSqlitePersistenceAndRecovery()
{
    using namespace std::chrono_literals;
    const std::string description =
        "site 60 30\n"
        "entrance 0 15\n"
        "exit 60 15\n"
        "region A 10 10 1 3 1.2 5.5 6 left\n";
    const auto layout = smartpark::ParkingLayout::fromDescription(description);
    const auto now = smartpark::ParkingRecord::Clock::from_time_t(1000);

    QTemporaryDir directory;
    expect(directory.isValid(), "SQLite test can create a temporary directory");
    const QString databasePath = directory.filePath("smartpark.db");
    {
        smartpark::DatabaseManager database(databasePath);
        expect(database.database().isOpen(), "SQLite database opens");
        smartpark::ParkingRepository repository(database.database());
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

} // namespace

int main()
{
    testVehicle();
    testParkingSpotLifecycle();
    testParkingSpotReservation();
    testParkingRecordLifecycle();
    testDefaultLayout();
    testCustomLayoutAndAutomaticAllocation();
    testLayoutTypesAndMultipleGates();
    testReservationTtlAndConflict();
    testTypeMatching();
    testCongestionAvoidanceAndStrategies();
    testMultiEntranceSelection();
    testSqlitePersistenceAndRecovery();

    if (failureCount != 0) {
        std::cerr << failureCount << " test assertion(s) failed\n";
        return 1;
    }

    std::cout << "All core model tests passed\n";
    return 0;
}
