#include "core/model/ParkingSpot.h"
#include "core/model/ParkingRecord.h"
#include "core/model/Vehicle.h"
#include "core/service/ParkingService.h"

#include <algorithm>
#include <exception>
#include <set>
#include <iostream>
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
    expect(layout.spots().front().identifier() == "A001", "default layout starts at A001");
    expect(layout.spots().back().identifier() == "A060", "default layout ends at A060");
    for (const smartpark::ParkingSpot &spot : layout.spots()) {
        identifiers.insert(spot.identifier());
        expect(spot.bounds().width > 0.0 && spot.bounds().height > 0.0,
               "each generated spot has positive bounds");
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

    const auto firstSpot = std::find_if(
        service.spots().begin(), service.spots().end(),
        [&first](const smartpark::ParkingSpot &spot) {
            return spot.identifier() == first->spotId;
        });
    expect(firstSpot != service.spots().end() && firstSpot->parkedVehicle().has_value(),
           "allocated spot is occupied");
    const auto closedRecord = service.leave(u8"晋A12345", entry + std::chrono::minutes(90));
    expect(closedRecord.has_value(), "parking service can process vehicle exit");
    expect(closedRecord->duration() == std::chrono::minutes(90),
           "closed record contains parking duration");
    expect(service.remainingSpots() == 31, "exit restores the parking spot");
    expect(!service.activeRecord(u8"晋A12345").has_value(), "exit closes the active record");
}

} // namespace

int main()
{
    testVehicle();
    testParkingSpotLifecycle();
    testParkingRecordLifecycle();
    testDefaultLayout();
    testCustomLayoutAndAutomaticAllocation();

    if (failureCount != 0) {
        std::cerr << failureCount << " test assertion(s) failed\n";
        return 1;
    }

    std::cout << "All core model tests passed\n";
    return 0;
}
