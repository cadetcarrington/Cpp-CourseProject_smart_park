#pragma once

#include "core/model/ParkingLayout.h"
#include "core/model/Vehicle.h"
#include "core/service/GridPlanner.h"

#include <optional>
#include <string>
#include <vector>

namespace smartpark {

enum class AllocationStrategy
{
    WeightedCost,
    Nearest
};

struct AllocationWeights
{
    double entryPath{1.0};
    double exitPath{0.35};
    double laneCongestion{2.5};
    double turnCount{0.4};
    double typePenalty{1.0};
    double occupancyRadius{12.0};
    double occupancyK{0.35};
};

struct ScoreBreakdown
{
    double entryPathCost{0.0};
    double exitPathCost{0.0};
    double laneCongestionCost{0.0};
    double turnCountCost{0.0};
    double typePenalty{0.0};
    double total{0.0};
};

struct AllocationProposal
{
    std::string plateNumber;
    std::string spotId;
    Route entryRoute;
    Route exitRoute;
    ScoreBreakdown score;
    int nearbyOccupiedSpots{0};
    std::size_t entranceIndex{0};
    std::size_t exitIndex{0};
    AllocationStrategy strategy{AllocationStrategy::WeightedCost};
};

class SpotAllocator
{
public:
    SpotAllocator(const ParkingLayout &layout, const GridPlanner &planner);

    void setStrategy(AllocationStrategy strategy) noexcept;
    void setWeights(AllocationWeights weights) noexcept;
    AllocationStrategy strategy() const noexcept;
    const AllocationWeights &weights() const noexcept;

    std::optional<AllocationProposal> propose(
        const Vehicle &vehicle,
        const std::vector<ParkingSpot> &spots,
        const std::string &requiredSpotId = {}) const;

private:
    ScoreBreakdown makeScore(const Vehicle &vehicle, const ParkingSpot &spot,
                             const Route &entryRoute, const Route &exitRoute,
                             int nearbyOccupied) const;
    double typePenaltyFor(const Vehicle &vehicle, SpotType type) const;
    int nearbyOccupiedSpots(const ParkingSpot &candidate,
                            const std::vector<ParkingSpot> &spots) const;

    const ParkingLayout *layout_;
    const GridPlanner *planner_;
    AllocationStrategy strategy_{AllocationStrategy::WeightedCost};
    AllocationWeights weights_;
};

} // namespace smartpark
