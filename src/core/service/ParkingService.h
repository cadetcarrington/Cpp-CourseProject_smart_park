#pragma once

#include "core/model/ParkingLayout.h"
#include "core/model/ParkingRecord.h"
#include "core/model/Vehicle.h"
#include "core/service/GridPlanner.h"

#include <optional>
#include <string>
#include <vector>

namespace smartpark {

struct AllocationResult
{
    std::string plateNumber;
    std::string spotId;
    Route entryRoute;
    Route exitRoute;
    double score{0.0};
    int nearbyOccupiedSpots{0};
};

class ParkingService
{
public:
    explicit ParkingService(ParkingLayout layout);

    const ParkingLayout &layout() const noexcept;
    const std::vector<ParkingSpot> &spots() const noexcept;
    std::optional<AllocationResult> enter(
        const Vehicle &vehicle,
        ParkingRecord::TimePoint entryTime = ParkingRecord::Clock::now());
    std::optional<ParkingRecord> leave(
        const std::string &plateNumber,
        ParkingRecord::TimePoint exitTime = ParkingRecord::Clock::now());
    std::optional<AllocationResult> allocate(const Vehicle &vehicle);
    bool release(const std::string &spotId);
    int remainingSpots() const noexcept;
    int occupiedSpots() const noexcept;
    const std::vector<ParkingRecord> &records() const noexcept;
    std::optional<ParkingRecord> activeRecord(const std::string &plateNumber) const;

private:
    int nearbyOccupiedSpots(const ParkingSpot &candidate) const;

    ParkingLayout layout_;
    std::vector<ParkingSpot> spots_;
    GridPlanner planner_;
    std::vector<ParkingRecord> records_;
};

} // namespace smartpark
