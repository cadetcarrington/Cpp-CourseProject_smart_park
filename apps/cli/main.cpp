#include "core/model/ParkingSpot.h"
#include "core/model/Vehicle.h"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
    std::cout << "[PASS] " << message << '\n';
}

void printSpot(const smartpark::ParkingSpot &spot)
{
    std::cout << spot.identifier() << " | "
              << (spot.isAvailable() ? "Available" : "Occupied") << " | "
              << (spot.parkedVehicle() ? spot.parkedVehicle()->plateNumber() : "-")
              << '\n';
}

void printState(const smartpark::ParkingSpot &first, const smartpark::ParkingSpot &second)
{
    std::cout << "Spot | Status | Plate\n";
    printSpot(first);
    printSpot(second);
    const int available = static_cast<int>(first.isAvailable())
        + static_cast<int>(second.isAvailable());
    std::cout << "Available: " << available << "/2\n\n";
}

} // namespace

int main()
{
    try {
        std::cout << "SmartPark CLI - core model verification\n"
                  << "Two explicit spots; no GUI, database or automatic allocation.\n\n";

        smartpark::ParkingSpot first("A001");
        smartpark::ParkingSpot second("A002");
        const smartpark::Vehicle car(u8"\u664bA12345", smartpark::VehicleType::Car);
        const smartpark::Vehicle electric(u8"\u664bA88888", smartpark::VehicleType::Electric);

        require(first.isAvailable() && second.isAvailable(), "Both spots start available");
        printState(first, second);

        require(first.occupy(car), "First vehicle occupies A001");
        require(second.occupy(electric), "Second vehicle occupies A002");
        require(first.parkedVehicle() && first.parkedVehicle()->plateNumber() == car.plateNumber()
                    && second.parkedVehicle()
                    && second.parkedVehicle()->plateNumber() == electric.plateNumber(),
                "Both spots retain the correct vehicles");
        printState(first, second);

        require(!first.occupy(electric), "Repeated occupancy is rejected");
        require(first.parkedVehicle() && first.parkedVehicle()->plateNumber() == car.plateNumber(),
                "Rejected occupancy leaves original vehicle unchanged");

        require(first.release(), "First vehicle leaves A001");
        require(first.isAvailable() && !first.parkedVehicle(), "A001 is empty and available");
        require(!first.release(), "Repeated release is rejected");
        printState(first, second);

        require(second.release(), "Second vehicle leaves A002");
        require(first.isAvailable() && second.isAvailable()
                    && !first.parkedVehicle() && !second.parkedVehicle(),
                "All spots are empty and available again");
        printState(first, second);
        std::cout << "RESULT: PASS\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "RESULT: FAIL - " << error.what() << '\n';
        return 1;
    }
}
