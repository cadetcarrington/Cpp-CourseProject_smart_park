#include "core/service/ParkingService.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace smartpark {

ParkingService::ParkingService(ParkingLayout layout)
    : layout_(std::move(layout))
    , spots_(layout_.spots())
    , planner_(layout_.siteWidth(), layout_.siteHeight(), spots_)
{
    const auto unreachable = std::find_if(
        spots_.begin(), spots_.end(), [this](const ParkingSpot &spot) {
            return planner_.plan(layout_.entrance(), spot.accessPoint()).points.empty();
        });
    if (unreachable != spots_.end()) {
        throw std::invalid_argument("spot " + unreachable->identifier() + " is unreachable");
    }
}

const ParkingLayout &ParkingService::layout() const noexcept
{
    return layout_;
}

const std::vector<ParkingSpot> &ParkingService::spots() const noexcept
{
    return spots_;
}

std::optional<AllocationResult> ParkingService::allocate(const Vehicle &vehicle)
{
    return enter(vehicle);
}

std::optional<AllocationResult> ParkingService::enter(
    const Vehicle &vehicle, ParkingRecord::TimePoint entryTime)
{
    if (activeRecord(vehicle.plateNumber())) {
        return std::nullopt;
    }

    std::optional<AllocationResult> bestResult;
    double bestScore = std::numeric_limits<double>::infinity();

    for (const ParkingSpot &candidate : spots_) {
        if (!candidate.isAvailable()) {
            continue;
        }

        const Route entryRoute = planner_.plan(layout_.entrance(), candidate.accessPoint());
        const Route exitRoute = planner_.plan(candidate.accessPoint(), layout_.exit());
        if (entryRoute.points.empty() || exitRoute.points.empty()) {
            continue;
        }

        const int congestion = nearbyOccupiedSpots(candidate);
        const double score = entryRoute.distance + 0.35 * exitRoute.distance
            + 2.5 * static_cast<double>(congestion);
        if (score < bestScore) {
            bestResult = AllocationResult{vehicle.plateNumber(), candidate.identifier(),
                                          entryRoute, exitRoute,
                                          score, congestion};
            bestScore = score;
        }
    }

    if (!bestResult) {
        return std::nullopt;
    }

    const auto spot = std::find_if(
        spots_.begin(), spots_.end(),
        [&bestResult](const ParkingSpot &item) { return item.identifier() == bestResult->spotId; });
    if (spot == spots_.end() || !spot->occupy(vehicle)) {
        return std::nullopt;
    }
    records_.emplace_back(vehicle.plateNumber(), bestResult->spotId, entryTime);
    return bestResult;
}

std::optional<ParkingRecord> ParkingService::leave(
    const std::string &plateNumber, ParkingRecord::TimePoint exitTime)
{
    const auto record = std::find_if(
        records_.begin(), records_.end(), [&plateNumber](const ParkingRecord &item) {
            return item.plateNumber() == plateNumber && !item.isClosed();
        });
    if (record == records_.end() || record->isClosed() || exitTime < record->entryTime()) {
        return std::nullopt;
    }

    const auto spot = std::find_if(
        spots_.begin(), spots_.end(),
        [&record](const ParkingSpot &item) { return item.identifier() == record->spotId(); });
    if (spot == spots_.end() || !spot->release()) {
        return std::nullopt;
    }
    record->close(exitTime);
    return *record;
}

bool ParkingService::release(const std::string &spotId)
{
    const auto spot = std::find_if(
        spots_.begin(), spots_.end(),
        [&spotId](const ParkingSpot &item) { return item.identifier() == spotId; });
    if (spot == spots_.end() || !spot->release()) {
        return false;
    }
    const auto record = std::find_if(
        records_.begin(), records_.end(), [&spotId](const ParkingRecord &item) {
            return item.spotId() == spotId && !item.isClosed();
        });
    if (record != records_.end()) {
        record->close(ParkingRecord::Clock::now());
    }
    return true;
}

int ParkingService::remainingSpots() const noexcept
{
    return static_cast<int>(std::count_if(
        spots_.begin(), spots_.end(),
        [](const ParkingSpot &spot) { return spot.isAvailable(); }));
}

int ParkingService::occupiedSpots() const noexcept
{
    return static_cast<int>(spots_.size()) - remainingSpots();
}

const std::vector<ParkingRecord> &ParkingService::records() const noexcept
{
    return records_;
}

std::optional<ParkingRecord> ParkingService::activeRecord(
    const std::string &plateNumber) const
{
    const auto record = std::find_if(
        records_.begin(), records_.end(), [&plateNumber](const ParkingRecord &item) {
            return item.plateNumber() == plateNumber && !item.isClosed();
        });
    if (record == records_.end()) {
        return std::nullopt;
    }
    return *record;
}

int ParkingService::nearbyOccupiedSpots(const ParkingSpot &candidate) const
{
    return static_cast<int>(std::count_if(
        spots_.begin(), spots_.end(), [&candidate](const ParkingSpot &spot) {
            return !spot.isAvailable()
                && distance(spot.accessPoint(), candidate.accessPoint()) <= 12.0;
        }));
}

} // namespace smartpark
