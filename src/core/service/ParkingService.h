#pragma once

#include "core/model/ParkingLayout.h"
#include "core/model/Vehicle.h"
#include "core/service/GridPlanner.h"

#include <optional>
#include <string>

namespace smartpark {

struct AllocationResult
{
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
    std::optional<AllocationResult> allocate(const Vehicle &vehicle);
    bool release(const std::string &spotId);

private:
    int nearbyOccupiedSpots(const ParkingSpot &candidate) const;

    ParkingLayout layout_;
    std::vector<ParkingSpot> spots_;
    GridPlanner planner_;
};

} // namespace smartpark
