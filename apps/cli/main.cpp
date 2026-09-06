#include "core/model/ParkingLayout.h"
#include "core/model/Vehicle.h"
#include "core/service/ParkingService.h"

#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

smartpark::ParkingLayout loadLayout(const std::string &path)
{
    if (path.empty()) {
        return smartpark::ParkingLayout::defaultLayout();
    }

    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open layout file: " + path);
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return smartpark::ParkingLayout::fromDescription(buffer.str());
}

void printAllocation(const smartpark::AllocationResult &result)
{
    std::cout << "  Spot: " << result.spotId
              << " | Entry: " << result.entryRoute.distance << "m"
              << " | Exit: " << result.exitRoute.distance << "m"
              << " | Nearby occupied: " << result.nearbyOccupiedSpots
              << " | Score: " << result.score << '\n';
    if (!result.entryRoute.points.empty()) {
        const smartpark::Point &first = result.entryRoute.points.front();
        const smartpark::Point &last = result.entryRoute.points.back();
        std::cout << "  Entry route: (" << first.x << ", " << first.y << ") -> ("
                  << last.x << ", " << last.y << ") through "
                  << result.entryRoute.points.size() << " waypoints\n";
    }
}

} // namespace

int main(int argc, char **argv)
{
    try {
        if (argc > 2) {
            throw std::runtime_error("usage: smartpark_cli [layout.txt]");
        }

        const std::string layoutPath = argc == 2 ? argv[1] : std::string();
        const smartpark::ParkingLayout layout = loadLayout(layoutPath);
        smartpark::ParkingService service(layout);

        std::cout << "SmartPark CLI - automatic parking allocation\n"
                  << "Site: " << layout.siteWidth() << "m x " << layout.siteHeight()
                  << "m | Regions: " << layout.regions().size()
                  << " | Spots: " << layout.spots().size() << "\n\n";

        const std::vector<smartpark::Vehicle> vehicles = {
            {u8"晋A12345", smartpark::VehicleType::Car},
            {u8"晋A88888", smartpark::VehicleType::Electric},
            {u8"晋B67890", smartpark::VehicleType::Truck},
        };

        std::vector<smartpark::AllocationResult> allocations;
        allocations.reserve(vehicles.size());
        for (const smartpark::Vehicle &vehicle : vehicles) {
            const auto result = service.allocate(vehicle);
            if (!result) {
                throw std::runtime_error("automatic allocation failed");
            }
            std::cout << "Allocate " << vehicle.plateNumber() << ":\n";
            printAllocation(*result);
            allocations.push_back(*result);
        }

        std::cout << "\nRelease " << allocations.front().spotId << " and allocate another vehicle.\n";
        if (!service.release(allocations.front().spotId)) {
            throw std::runtime_error("release failed");
        }
        const auto replacement = service.allocate({u8"晋C24680", smartpark::VehicleType::Car});
        if (!replacement || replacement->spotId == allocations[1].spotId
            || replacement->spotId == allocations[2].spotId) {
            throw std::runtime_error("replacement allocation failed");
        }
        printAllocation(*replacement);

        if (layoutPath.empty() && layout.spots().size() != 60) {
            throw std::runtime_error("default layout must contain 60 spots");
        }

        std::cout << "\nRESULT: PASS\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "RESULT: FAIL - " << error.what() << '\n';
        return 1;
    }
}
