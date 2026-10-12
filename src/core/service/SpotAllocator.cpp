#include "core/service/SpotAllocator.h"
#include <algorithm>
#include <limits>
#include <map>
#include <utility>
namespace smartpark{
namespace{
// 分区并列（车辆数相同）时比较"哪个分区离入口更近"用的直线八向距离，
// 只是排序键，不做路径搜索。
double octileDistance(Point from, Point to){
    const double dx = std::abs(to.x - from.x);
    const double dy = std::abs(to.y - from.y);
    return std::max(dx, dy) + (std::sqrt(2.0) - 1.0) * std::min(dx, dy);
}
} // namespace
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
// 分区负载：占用 +0.7 预留 +0.5 停用。压力在评分时按放置后负载占比的平方计算。
std::map<std::string, std::pair<int, double>> zoneLoadMap(
    const std::vector<ParkingSpot> &spots){
    std::map<std::string, std::pair<int, double>> loads;
    for (const ParkingSpot &spot : spots){
        auto &entry = loads[spot.zone()];
        entry.first += 1;
        switch (spot.status()){
        case SpotStatus::Occupied:
            entry.second += 1.0;
            break;
        case SpotStatus::Reserved:
            entry.second += 0.7;
            break;
        case SpotStatus::Disabled:
            entry.second += 0.5;
            break;
        case SpotStatus::Available:
        default:
            break;
        }
    }
    return loads;
}
} // namespace
SpotAllocator::SpotAllocator(const ParkingLayout &layout, const GridPlanner &planner)
    : layout_(&layout)
    , planner_(&planner)
    , siteDiagonal_(std::hypot(layout.siteWidth(), layout.siteHeight())){
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
    // 候选：可用车位（或本车自己锁定的预约车位）。requiredSpotId 非空时只有一个候选。
    std::vector<std::size_t> candidateIndices;
    candidateIndices.reserve(spots.size());
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
        candidateIndices.push_back(index);
    }
    if (candidateIndices.empty()){
        return std::nullopt;
    }

    // 指定车位（预约锁位、到场确认）没有挑选余地：直接评估，不做分区筛选。
    // Nearest 策略的语义就是"最近的空位"，同样不做分区筛选。
    if (!requiredSpotId.empty() || strategy_ != AllocationStrategy::WeightedCost){
        return evaluateCandidates(vehicle, spots, candidateIndices);
    }

    // 分区优先：每次只对「本车对应类型里车辆最少的那一个分区」做路径搜索，
    // 不再对全场每个空闲车位各搜一遍。只有该分区给不出可行方案时才看下一个
    // （正常情况一次就命中），因此搜索量取决于单个分区而不是整个车场。
    const std::map<std::string, std::pair<int, double>> zoneLoads = zoneLoadMap(spots);
    std::map<std::string, std::vector<std::size_t>> candidatesByZone;
    for (std::size_t index : candidateIndices){
        candidatesByZone[spots[index].zone()].push_back(index);
    }
    struct ZoneChoice{
        std::string name;
        double vehicles{0.0};     // 分区内车辆数（预约按 0.7、停用按 0.5 折算）
        double loadRatio{0.0};
        double typePenalty{0.0};  // 类型代价最低 = 本车的对应类型
        double entranceDistance{0.0};  // 该分区最近车位到最近入口的直线距离
    };
    std::vector<ZoneChoice> zones;
    zones.reserve(candidatesByZone.size());
    for (const auto &entry : candidatesByZone){
        const auto loadIt = zoneLoads.find(entry.first);
        const double capacity = loadIt != zoneLoads.end()
            ? static_cast<double>(loadIt->second.first)
            : 0.0;
        const double vehicles = loadIt != zoneLoads.end() ? loadIt->second.second : 0.0;
        double entranceDistance = std::numeric_limits<double>::infinity();
        for (std::size_t index : entry.second){
            const Point access = spots[index].accessPoint();
            for (const Point &entrance : layout_->entrances()){
                entranceDistance = std::min(entranceDistance, octileDistance(entrance, access));
            }
        }
        zones.push_back({entry.first, vehicles,
                         capacity > 0.0 ? vehicles / capacity : 0.0,
                         typePenaltyFor(vehicle, spots[entry.second.front()].type()),
                         entranceDistance});
    }
    std::sort(zones.begin(), zones.end(), [](const ZoneChoice &lhs, const ZoneChoice &rhs){
        if (lhs.typePenalty != rhs.typePenalty){
            return lhs.typePenalty < rhs.typePenalty;   // 对应类型分区优先
        }
        if (lhs.vehicles != rhs.vehicles){
            return lhs.vehicles < rhs.vehicles;         // 同类型里车辆最少
        }
        if (lhs.entranceDistance != rhs.entranceDistance){
            return lhs.entranceDistance < rhs.entranceDistance;   // 并列时近的优先
        }
        if (lhs.loadRatio != rhs.loadRatio){
            return lhs.loadRatio < rhs.loadRatio;
        }
        return lhs.name < rhs.name;                     // 并列时按名称，结果可复现
    });
    for (const ZoneChoice &zone : zones){
        const auto subset = candidatesByZone.find(zone.name);
        if (subset == candidatesByZone.end()){
            continue;
        }
        if (std::optional<AllocationProposal> proposal =
                evaluateCandidates(vehicle, spots, subset->second)){
            return proposal;
        }
    }
    return std::nullopt;
}

double SpotAllocator::exitCoefficient(std::size_t exitIndex) const noexcept{
    if (exitIndex < weights_.exitCoefficients.size()){
        const double coefficient = weights_.exitCoefficients[exitIndex];
        if (coefficient > 0.0){
            return coefficient;
        }
    }
    return 1.0;   // 未配置：两个出口等权
}

std::optional<AllocationProposal> SpotAllocator::evaluateCandidates(
    const Vehicle &vehicle,
    const std::vector<ParkingSpot> &spots,
    const std::vector<std::size_t> &candidateIndices) const{
    if (candidateIndices.empty()){
        return std::nullopt;
    }
    const std::map<std::string, std::pair<int, double>> zoneLoads = zoneLoadMap(spots);
    OccupancyField occupancy;
    const OccupancyField *occupancyPtr = nullptr;
    if (strategy_ == AllocationStrategy::WeightedCost){
        occupancy = planner_->buildOccupancy(spots, weights_.occupancyRadius, weights_.occupancyK);
        occupancyPtr = &occupancy;
    }
    std::vector<Point> targets;
    targets.reserve(candidateIndices.size());
    for (std::size_t index : candidateIndices){
        targets.push_back(spots[index].accessPoint());
    }
    const std::size_t candidateCount = candidateIndices.size();
    std::vector<Route> bestEntries(candidateCount);
    std::vector<std::size_t> bestEntryGates(candidateCount, 0);
    std::vector<Route> bestExits(candidateCount);
    std::vector<std::size_t> bestExitGates(candidateCount, 0);
    // 出口评分：距离用「各出口系数加权的平均」——两个出口都顺路的车位更优，
    // 而不是只看最近的那个出口；实际指派走的出口取「系数 × 距离」最小者。
    std::vector<double> exitWeightedDistance(candidateCount, 0.0);
    std::vector<double> exitCoefficientSum(candidateCount, 0.0);
    std::vector<double> exitWeightedBest(candidateCount,
                                         std::numeric_limits<double>::infinity());

    const std::vector<Point> &entrances = layout_->entrances();
    for (std::size_t gate = 0; gate < entrances.size(); ++gate){
        const std::vector<Route> routes =
            planner_->planFromToTargets(entrances[gate], targets, occupancyPtr);
        for (std::size_t index = 0; index < candidateCount; ++index){
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
        const double coefficient = exitCoefficient(gate);
        for (std::size_t index = 0; index < candidateCount; ++index){
            const Route exitRoute = reversedRoute(routes[index]);
            if (exitRoute.points.empty()){
                continue;
            }
            exitWeightedDistance[index] += coefficient * exitRoute.distance;
            exitCoefficientSum[index] += coefficient;
            const double weightedCost = coefficient * exitRoute.distance;
            if (bestExits[index].points.empty() || weightedCost < exitWeightedBest[index]){
                exitWeightedBest[index] = weightedCost;
                bestExits[index] = exitRoute;
                bestExitGates[index] = gate;
            }
        }
    }
    // 分区均衡（水位填充，仅 WeightedCost 且 zonePressure 权重 > 0 时启用）：
    // 跨分区时“计入本车后负载占比”更低的分区无条件优先，
    // 同水位档内再由加权分（距离/拥堵/类型）决定。
    const bool zoneBalance = strategy_ == AllocationStrategy::WeightedCost
        && weights_.zonePressure > 0.0;
    std::optional<AllocationProposal> bestProposal;
    double bestScore = std::numeric_limits<double>::infinity();
    double bestLoadAfter = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < candidateCount; ++index){
        if (bestEntries[index].points.empty() || bestExits[index].points.empty()){
            continue;
        }
        const ParkingSpot &spot = spots[candidateIndices[index]];
        AllocationProposal proposal;
        proposal.plateNumber = vehicle.plateNumber();
        proposal.spotId = spot.identifier();
        proposal.entryRoute = bestEntries[index];
        proposal.exitRoute = bestExits[index];
        proposal.nearbyOccupiedSpots = nearbyOccupiedSpots(spot, spots);
        proposal.entranceIndex = bestEntryGates[index];
        proposal.exitIndex = bestExitGates[index];
        proposal.strategy = strategy_;
        double zoneLoadAfter = 1.0;
        const auto loadIt = zoneLoads.find(spot.zone());
        if (loadIt != zoneLoads.end() && loadIt->second.first > 0){
            zoneLoadAfter = std::clamp(
                (loadIt->second.second + 1.0) / loadIt->second.first, 0.0, 1.0);
        }
        // 评分用的出口距离换成加权平均；proposal.exitRoute 仍是实际要走的那个出口。
        Route scoringExit = proposal.exitRoute;
        if (exitCoefficientSum[index] > 0.0){
            scoringExit.distance = exitWeightedDistance[index] / exitCoefficientSum[index];
        }
        proposal.score = makeScore(vehicle, spot, proposal.entryRoute, scoringExit,
                                   proposal.nearbyOccupiedSpots, zoneLoadAfter);
        constexpr double kLoadEpsilon = 1e-9;
        const bool lowerLoad = zoneBalance
            && zoneLoadAfter < bestLoadAfter - kLoadEpsilon;
        const bool sameTier = !zoneBalance
            || zoneLoadAfter <= bestLoadAfter + kLoadEpsilon;
        if (!bestProposal.has_value()
            || (lowerLoad || (sameTier && proposal.score.total < bestScore))){
            bestLoadAfter = zoneLoadAfter;
            bestScore = proposal.score.total;
            bestProposal = std::move(proposal);
        }
    }
    return bestProposal;
}

ScoreBreakdown SpotAllocator::makeScore(const Vehicle &vehicle, const ParkingSpot &spot,
                                        const Route &entryRoute, const Route &exitRoute,
                                        int nearbyOccupied, double zoneLoadAfter) const{
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
    // 分区压力：计入本车后的负载占比取平方，再乘权重和场地对角线，
    // 换算成等效步行米数，与路径项同一量纲直接比较。
    breakdown.zonePressureCost =
        weights_.zonePressure * siteDiagonal_ * zoneLoadAfter * zoneLoadAfter;
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
