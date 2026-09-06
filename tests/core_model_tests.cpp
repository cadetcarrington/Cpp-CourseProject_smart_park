#include "core/model/ParkingSpot.h"
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

void testDefaultLayout()
{
    const smartpark::ParkingLayout layout = smartpark::ParkingLayout::defaultLayout();
    std::set<std::string> identifiers;

    expect(layout.spots().size() == 60, "default layout contains 60 spots");
    expect(layout.regions().size() == 3, "default layout contains three regions");
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

    const auto first = service.allocate({u8"晋A12345", smartpark::VehicleType::Car});
    const auto second = service.allocate({u8"晋A88888", smartpark::VehicleType::Electric});
    expect(first.has_value() && second.has_value(), "automatic allocation succeeds");
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
    expect(service.release(first->spotId), "allocated spot can be released");
}

} // namespace

int main()
{
    testVehicle();
    testParkingSpotLifecycle();
    testDefaultLayout();
    testCustomLayoutAndAutomaticAllocation();

    if (failureCount != 0) {
        std::cerr << failureCount << " test assertion(s) failed\n";
        return 1;
    }

    std::cout << "All core model tests passed\n";
    return 0;
}
