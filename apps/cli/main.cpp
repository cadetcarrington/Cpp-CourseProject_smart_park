#include "core/model/ParkingLayout.h"
#include "core/model/Vehicle.h"
#include "core/service/ParkingService.h"

#include <algorithm>
#include <exception>
#include <chrono>
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

void printAllocation(const smartpark::ParkingService &service,
                     const smartpark::AllocationResult &result)
{
    const smartpark::ParkingSpot *spot = findSpot(service, result.spotId);
    std::cout << "  Spot: " << result.spotId
              << " | Type: " << (spot != nullptr ? toString(spot->type()) : "unknown")
              << " | Entry: " << result.entryRoute.distance << "m"
              << " | Exit: " << result.exitRoute.distance << "m"
              << " | Turns: " << (result.entryRoute.turnCount + result.exitRoute.turnCount)
              << " | Nearby occupied: " << result.nearbyOccupiedSpots
              << " | Score: " << result.score << '\n';
    std::cout << "  Breakdown: entry=" << result.breakdown.entryPathCost
              << " exit=" << result.breakdown.exitPathCost
              << " congestion=" << result.breakdown.laneCongestionCost
              << " turns=" << result.breakdown.turnCountCost
              << " type=" << result.breakdown.typePenalty
              << " | Gate: in#" << result.entranceIndex
              << " out#" << result.exitIndex << '\n';
    if (!result.entryRoute.points.empty()) {
        const smartpark::Point &first = result.entryRoute.points.front();
        const smartpark::Point &last = result.entryRoute.points.back();
        std::cout << "  Entry route: (" << first.x << ", " << first.y << ") -> ("
                  << last.x << ", " << last.y << ") through "
                  << result.entryRoute.points.size() << " waypoints\n";
    }
}

void printDuration(std::chrono::seconds duration)
{
    const auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
    const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration - hours);
    std::cout << hours.count() << "h " << minutes.count() << "m";
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
                  << " | Entrances: " << layout.entrances().size()
                  << " | Exits: " << layout.exits().size()
                  << " | Spots: " << layout.spots().size() << "\n\n";

        const std::vector<smartpark::Vehicle> vehicles = {
            {u8"晋A12345", smartpark::VehicleType::Car},
            {u8"晋A88888", smartpark::VehicleType::Electric},
            {u8"晋B67890", smartpark::VehicleType::Truck},
        };

        const smartpark::ParkingRecord::TimePoint entryTime =
            smartpark::ParkingRecord::Clock::from_time_t(1000);
        std::vector<smartpark::AllocationResult> allocations;
        allocations.reserve(vehicles.size());
        for (const smartpark::Vehicle &vehicle : vehicles) {
            const auto result = service.enter(vehicle, entryTime);
            if (!result) {
                throw std::runtime_error("automatic allocation failed");
            }
            std::cout << "Allocate " << vehicle.plateNumber() << ":\n";
            printAllocation(service, *result);
            allocations.push_back(*result);
        }

        std::cout << "\nRelease " << allocations.front().plateNumber
                  << " and allocate another vehicle.\n";
        const auto closedRecord = service.leave(
            allocations.front().plateNumber, entryTime + std::chrono::minutes(90));
        if (!closedRecord) {
            throw std::runtime_error("leave failed");
        }
        std::cout << "  Closed record: " << closedRecord->plateNumber()
                  << " | Spot: " << closedRecord->spotId()
                  << " | Duration: ";
        printDuration(closedRecord->duration());
        std::cout << " | Fee: " << closedRecord->fee() << " yuan\n";

        const auto replacement = service.enter({u8"晋C24680", smartpark::VehicleType::Car}, entryTime);
        if (!replacement || replacement->spotId == allocations[1].spotId
            || replacement->spotId == allocations[2].spotId) {
            throw std::runtime_error("replacement allocation failed");
        }
        printAllocation(service, *replacement);
        std::cout << "\nRemaining spots: " << service.remainingSpots()
                  << "/" << service.spots().size()
                  << " | Occupied: " << service.occupiedSpots()
                  << " | Reserved: " << service.reservedSpots()
                  << " | Records: " << service.records().size() << '\n';

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
