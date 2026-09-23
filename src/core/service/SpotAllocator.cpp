#include "core/service/SpotAllocator.h"
#include <algorithm>
#include <limits>
#include <map>
#include <utility>
namespace smartpark{
namespace{
Route reversedRoute(Route route){
    std::reverse(route.points.begin(), route.points.end());
    return route;
}
bool isBetterRoute(const Route &candidate, const Route &current, AllocationStrategy strategy){
    if (current.points.empty()){
        return true;
    }
    if (strategy == AllocationStrategy::Nearest){
        return candidate.distance < current.distance;
    }
    return candidate.cost < current.cost;
}
std::map<std::string, double> zonePressureMap(const std::vector<ParkingSpot> &spots){
    std::map<std::string, int> totals;
    std::map<std::string, double> loads;
    for (const ParkingSpot &spot : spots){
        totals[spot.zone()] += 1;
        switch (spot.status()){
        case SpotStatus::Occupied:
            loads[spot.zone()] += 1.0;
            break;
        case SpotStatus::Reserved:
            loads[spot.zone()] += 0.7;
            break;
        case SpotStatus::Disabled:
            loads[spot.zone()] += 0.5;
            break;
        case SpotStatus::Available:
        default:
            break;
        }
    }
    std::map<std::string, double> pressures;
    for (const auto &entry : totals){
        pressures[entry.first] = entry.second > 0
            ? loads[entry.first] / static_cast<double>(entry.second)
            : 0.0;
    }
    return pressures;
}
} // namespace
SpotAllocator::SpotAllocator(const ParkingLayout &layout, const GridPlanner &planner)
    : layout_(&layout)
    , planner_(&planner){
}
void SpotAllocator::setStrategy(AllocationStrategy strategy) noexcept{
    strategy_ = strategy;
}
void SpotAllocator::setWeights(AllocationWeights weights) noexcept{
    weights_ = weights;
}
AllocationStrategy SpotAllocator::strategy() const noexcept{
    return strategy_;
}
const AllocationWeights &SpotAllocator::weights() const noexcept{
    return weights_;
}
std::optional<AllocationProposal> SpotAllocator::propose(
    const Vehicle &vehicle,
    const std::vector<ParkingSpot> &spots,
    const std::string &requiredSpotId) const{
    struct Candidate{
        std::size_t spotIndex{0};
        Point access;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(spots.size());
    for (std::size_t index = 0; index < spots.size(); ++index){
        const ParkingSpot &spot = spots[index];
        if (!requiredSpotId.empty() && spot.identifier() != requiredSpotId){
            continue;
        }
        const bool sameReservation = spot.status() == SpotStatus::Reserved
            && spot.parkedVehicle()
            && spot.parkedVehicle()->plateNumber() == vehicle.plateNumber();
        if (!spot.isAvailable() && !sameReservation){
            continue;
        }
        candidates.push_back({index, spot.accessPoint()});
    }
    if (candidates.empty()){
        return std::nullopt;
    }
    const std::map<std::string, double> zonePressures = zonePressureMap(spots);
    OccupancyField occupancy;
    const OccupancyField *occupancyPtr = nullptr;
    if (strategy_ == AllocationStrategy::WeightedCost){
        occupancy = planner_->buildOccupancy(spots, weights_.occupancyRadius, weights_.occupancyK);
        occupancyPtr = &occupancy;
    }
    std::vector<Point> targets;
    targets.reserve(candidates.size());
    for (const Candidate &candidate : candidates){
        targets.push_back(candidate.access);
    }
    std::vector<Route> bestEntries(candidates.size());
    std::vector<std::size_t> bestEntryGates(candidates.size(), 0);
    std::vector<Route> bestExits(candidates.size());
    std::vector<std::size_t> bestExitGates(candidates.size(), 0);
    const std::vector<Point> &entrances = layout_->entrances();
    for (std::size_t gate = 0; gate < entrances.size(); ++gate){
        const std::vector<Route> routes =
            planner_->planFromToTargets(entrances[gate], targets, occupancyPtr);
        for (std::size_t index = 0; index < candidates.size(); ++index){
            if (routes[index].points.empty()){
                continue;
            }
            if (isBetterRoute(routes[index], bestEntries[index], strategy_)){
                bestEntries[index] = routes[index];
                bestEntryGates[index] = gate;
            }
        }
    }
    const std::vector<Point> &exits = layout_->exits();
    for (std::size_t gate = 0; gate < exits.size(); ++gate){
        const std::vector<Route> routes =
            planner_->planFromToTargets(exits[gate], targets, occupancyPtr);
        for (std::size_t index = 0; index < candidates.size(); ++index){
            if (routes[index].points.empty()){
                continue;
            }
            const Route exitRoute = reversedRoute(routes[index]);
            if (isBetterRoute(exitRoute, bestExits[index], strategy_)){
                bestExits[index] = exitRoute;
                bestExitGates[index] = gate;
            }
        }
    }
    std::optional<AllocationProposal> bestProposal;
    double bestScore = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < candidates.size(); ++index){
        if (bestEntries[index].points.empty() || bestExits[index].points.empty()){
            continue;
        }
        const ParkingSpot &spot = spots[candidates[index].spotIndex];
        AllocationProposal proposal;
        proposal.plateNumber = vehicle.plateNumber();
        proposal.spotId = spot.identifier();
        proposal.entryRoute = bestEntries[index];
        proposal.exitRoute = bestExits[index];
        proposal.nearbyOccupiedSpots = nearbyOccupiedSpots(spot, spots);
        proposal.entranceIndex = bestEntryGates[index];
        proposal.exitIndex = bestExitGates[index];
        proposal.strategy = strategy_;
        const auto pressureIt = zonePressures.find(spot.zone());
        const double zonePressure = pressureIt != zonePressures.end() ? pressureIt->second : 0.0;
        proposal.score = makeScore(vehicle, spot, proposal.entryRoute, proposal.exitRoute,
                                   proposal.nearbyOccupiedSpots, zonePressure);
        if (proposal.score.total < bestScore){
            bestScore = proposal.score.total;
            bestProposal = std::move(proposal);
        }
    }
    return bestProposal;
}
ScoreBreakdown SpotAllocator::makeScore(const Vehicle &vehicle, const ParkingSpot &spot,
                                        const Route &entryRoute, const Route &exitRoute,
                                        int nearbyOccupied, double zonePressure) const{
    ScoreBreakdown breakdown;
    if (strategy_ == AllocationStrategy::Nearest){
        breakdown.entryPathCost = entryRoute.distance;
        breakdown.total = entryRoute.distance;
        return breakdown;
    }
    breakdown.entryPathCost = weights_.entryPath * entryRoute.distance;
    breakdown.exitPathCost = weights_.exitPath * exitRoute.distance;
    breakdown.laneCongestionCost = weights_.laneCongestion * static_cast<double>(nearbyOccupied);
    breakdown.turnCountCost =
        weights_.turnCount * static_cast<double>(entryRoute.turnCount + exitRoute.turnCount);
    breakdown.typePenalty = weights_.typePenalty * typePenaltyFor(vehicle, spot.type());
    breakdown.zonePressureCost = weights_.zonePressure * zonePressure;
    breakdown.total = breakdown.entryPathCost + breakdown.exitPathCost
        + breakdown.laneCongestionCost + breakdown.turnCountCost + breakdown.typePenalty
        + breakdown.zonePressureCost;
    return breakdown;
}
double SpotAllocator::typePenaltyFor(const Vehicle &vehicle, SpotType type) const{
    switch (type){
    case SpotType::Charging:
        return vehicle.type() == VehicleType::Electric ? -25.0 : 50.0;
    case SpotType::Accessible:
        return 25.0;
    case SpotType::Vip:
        return 40.0;
    case SpotType::Normal:
    default:
        return 0.0;
    }
}
int SpotAllocator::nearbyOccupiedSpots(const ParkingSpot &candidate,
                                       const std::vector<ParkingSpot> &spots) const{
    return static_cast<int>(std::count_if(
        spots.begin(), spots.end(), [this, &candidate](const ParkingSpot &spot){
            return spot.identifier() != candidate.identifier()
                && !spot.isAvailable()
                && distance(spot.accessPoint(), candidate.accessPoint()) <= weights_.occupancyRadius;
        }));
}
} // namespace smartpark
