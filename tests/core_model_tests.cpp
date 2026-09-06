#include "core/model/ParkingSpot.h"
#include "core/model/Vehicle.h"

#include <exception>
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

} // namespace

int main()
{
    testVehicle();
    testParkingSpotLifecycle();

    if (failureCount != 0) {
        std::cerr << failureCount << " test assertion(s) failed\n";
        return 1;
    }

    std::cout << "All core model tests passed\n";
    return 0;
}
