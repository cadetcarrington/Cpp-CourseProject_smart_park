#pragma once
#include "core/model/ParkingLayout.h"
#include "core/model/Vehicle.h"
#include "core/service/GridPlanner.h"
#include <optional>
#include <string>
#include <vector>
namespace smartpark{
enum class AllocationStrategy{
    WeightedCost,
    Nearest
};
struct AllocationWeights{
    double entryPath{1.0};
    double exitPath{0.35};
    double laneCongestion{2.5};
    double turnCount{0.4};
    double typePenalty{1.0};
    double occupancyRadius{12.0};
    double occupancyK{0.35};
    // 分区压力权重：乘以场地对角线得到“满区等效步行米数”。
    // 0.8 表示压满一个分区约等于多走 0.8 倍对角线的距离。
    double zonePressure{0.8};
    // 出口评分系数：每个出口一个权重（缺失或 <=0 的按 1.0 处理）。
    // 评分用「按系数加权的平均出口距离」，而不是只取最近出口——两个出口都顺路的
    // 车位更优；实际指派走的出口仍取「系数 × 距离」最小的那个。默认全 1.0=等权。
    std::vector<double> exitCoefficients;
};
struct ScoreBreakdown{
    double entryPathCost{0.0};
    double exitPathCost{0.0};
    double laneCongestionCost{0.0};
    double turnCountCost{0.0};
    double typePenalty{0.0};
    double zonePressureCost{0.0};
    double total{0.0};
};
struct AllocationProposal{
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
class SpotAllocator{
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
    // 对给定候选车位集合做一次完整评估：入口/出口搜索 + 评分 + 选最优。
    // 分区优先策略下每次只传一个分区的候选，搜索量取决于该分区而不是全场。
    std::optional<AllocationProposal> evaluateCandidates(
        const Vehicle &vehicle,
        const std::vector<ParkingSpot> &spots,
        const std::vector<std::size_t> &candidateIndices) const;
    double exitCoefficient(std::size_t exitIndex) const noexcept;
    ScoreBreakdown makeScore(const Vehicle &vehicle, const ParkingSpot &spot,
                             const Route &entryRoute, const Route &exitRoute,
                             int nearbyOccupied, double zoneLoadAfter) const;
    double typePenaltyFor(const Vehicle &vehicle, SpotType type) const;
    int nearbyOccupiedSpots(const ParkingSpot &candidate,
                            const std::vector<ParkingSpot> &spots) const;
    const ParkingLayout *layout_;
    const GridPlanner *planner_;
    AllocationStrategy strategy_{AllocationStrategy::WeightedCost};
    AllocationWeights weights_;
    double siteDiagonal_{0.0};
};
} // namespace smartpark
