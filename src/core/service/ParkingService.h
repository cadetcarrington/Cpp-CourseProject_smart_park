#pragma once

#include "core/model/ParkingLayout.h"
#include "core/model/ParkingRecord.h"
#include "core/model/Vehicle.h"
#include "core/persistence/ParkingRepository.h"
#include "core/service/GridPlanner.h"
#include "core/service/SpotAllocator.h"

#include <chrono>
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
    ScoreBreakdown breakdown;
    std::size_t entranceIndex{0};
    std::size_t exitIndex{0};
    AllocationStrategy strategy{AllocationStrategy::WeightedCost};
};

class ParkingService
{
public:
    explicit ParkingService(ParkingLayout layout,
                            AllocationStrategy strategy = AllocationStrategy::WeightedCost,
                            ParkingRepository *repository = nullptr);

    const ParkingLayout &layout() const noexcept;
    const std::vector<ParkingSpot> &spots() const noexcept;
    AllocationStrategy strategy() const noexcept;
    void setStrategy(AllocationStrategy strategy) noexcept;
    void setWeights(AllocationWeights weights) noexcept;
    void expireReservations(ParkingRecord::TimePoint now);

    std::optional<AllocationResult> reserve(
        const Vehicle &vehicle,
        ParkingRecord::TimePoint now,
        std::chrono::seconds ttl);
    std::optional<AllocationResult> enter(
        const Vehicle &vehicle,
        ParkingRecord::TimePoint entryTime = ParkingRecord::Clock::now());
    std::optional<ParkingRecord> leave(
        const std::string &plateNumber,
        ParkingRecord::TimePoint exitTime = ParkingRecord::Clock::now());
    std::optional<AllocationResult> allocate(const Vehicle &vehicle);
    bool cancelReservation(const std::string &plateNumber);
    bool release(const std::string &spotId);
    int remainingSpots() const noexcept;
    int occupiedSpots() const noexcept;
    int reservedSpots() const noexcept;
    const std::vector<ParkingRecord> &records() const noexcept;
    std::optional<ParkingRecord> activeRecord(const std::string &plateNumber) const;

private:
    AllocationResult toResult(const AllocationProposal &proposal) const;
    void restore(ParkingRepository &repository);
    ParkingSpot *findSpot(const std::string &spotId);
    const ParkingSpot *findReservedSpot(const std::string &plateNumber) const;
    ParkingSpot *findReservedSpot(const std::string &plateNumber);
    void ensureReachable() const;

    ParkingLayout layout_;
    std::vector<ParkingSpot> spots_;
    GridPlanner planner_;
    SpotAllocator allocator_;
    std::vector<ParkingRecord> records_;
    ParkingRepository *repository_{nullptr};
};

} // namespace smartpark
