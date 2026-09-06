#include "core/service/ParkingService.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace smartpark {
namespace {

bool hasSamePlate(const std::optional<Vehicle> &vehicle, const std::string &plateNumber)
{
    return vehicle && vehicle->plateNumber() == plateNumber;
}

} // namespace

ParkingService::ParkingService(ParkingLayout layout, AllocationStrategy strategy)
    : layout_(std::move(layout))
    , spots_(layout_.spots())
    , planner_(layout_.siteWidth(), layout_.siteHeight(), spots_)
    , allocator_(layout_, planner_)
{
    allocator_.setStrategy(strategy);
    ensureReachable();
}

const ParkingLayout &ParkingService::layout() const noexcept
{
    return layout_;
}

const std::vector<ParkingSpot> &ParkingService::spots() const noexcept
{
    return spots_;
}

AllocationStrategy ParkingService::strategy() const noexcept
{
    return allocator_.strategy();
}

void ParkingService::setStrategy(AllocationStrategy strategy) noexcept
{
    allocator_.setStrategy(strategy);
}

void ParkingService::setWeights(AllocationWeights weights) noexcept
{
    allocator_.setWeights(weights);
}

void ParkingService::expireReservations(ParkingRecord::TimePoint now)
{
    for (ParkingSpot &spot : spots_) {
        spot.expireReservation(now);
    }
}

std::optional<AllocationResult> ParkingService::allocate(const Vehicle &vehicle)
{
    return enter(vehicle);
}

std::optional<AllocationResult> ParkingService::reserve(
    const Vehicle &vehicle, ParkingRecord::TimePoint now, std::chrono::seconds ttl)
{
    expireReservations(now);
    if (ttl <= std::chrono::seconds::zero() || activeRecord(vehicle.plateNumber())
        || findReservedSpot(vehicle.plateNumber()) != nullptr) {
        return std::nullopt;
    }

    const std::optional<AllocationProposal> proposal = allocator_.propose(vehicle, spots_);
    if (!proposal) {
        return std::nullopt;
    }

    ParkingSpot *spot = findSpot(proposal->spotId);
    if (spot == nullptr || !spot->reserve(vehicle, now + ttl)) {
        return std::nullopt;
    }
    return toResult(*proposal);
}

std::optional<AllocationResult> ParkingService::enter(
    const Vehicle &vehicle, ParkingRecord::TimePoint entryTime)
{
    expireReservations(entryTime);
    if (activeRecord(vehicle.plateNumber())) {
        return std::nullopt;
    }

    std::string requiredSpotId;
    if (const ParkingSpot *reserved = findReservedSpot(vehicle.plateNumber())) {
        requiredSpotId = reserved->identifier();
    }

    const std::optional<AllocationProposal> proposal =
        allocator_.propose(vehicle, spots_, requiredSpotId);
    if (!proposal) {
        return std::nullopt;
    }

    ParkingSpot *spot = findSpot(proposal->spotId);
    if (spot == nullptr || !spot->occupy(vehicle)) {
        return std::nullopt;
    }
    records_.emplace_back(vehicle.plateNumber(), proposal->spotId, entryTime);
    return toResult(*proposal);
}

std::optional<ParkingRecord> ParkingService::leave(
    const std::string &plateNumber, ParkingRecord::TimePoint exitTime)
{
    expireReservations(exitTime);
    const auto record = std::find_if(
        records_.begin(), records_.end(), [&plateNumber](const ParkingRecord &item) {
            return item.plateNumber() == plateNumber && !item.isClosed();
        });
    if (record == records_.end() || exitTime < record->entryTime()) {
        return std::nullopt;
    }

    ParkingSpot *spot = findSpot(record->spotId());
    if (spot == nullptr || !spot->release()) {
        return std::nullopt;
    }
    record->close(exitTime);
    return *record;
}

bool ParkingService::cancelReservation(const std::string &plateNumber)
{
    ParkingSpot *spot = findReservedSpot(plateNumber);
    if (spot == nullptr) {
        return false;
    }
    return spot->release();
}

bool ParkingService::release(const std::string &spotId)
{
    ParkingSpot *spot = findSpot(spotId);
    if (spot == nullptr || !spot->release()) {
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
    return static_cast<int>(std::count_if(
        spots_.begin(), spots_.end(),
        [](const ParkingSpot &spot) { return spot.status() == SpotStatus::Occupied; }));
}

int ParkingService::reservedSpots() const noexcept
{
    return static_cast<int>(std::count_if(
        spots_.begin(), spots_.end(),
        [](const ParkingSpot &spot) { return spot.status() == SpotStatus::Reserved; }));
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

AllocationResult ParkingService::toResult(const AllocationProposal &proposal) const
{
    AllocationResult result;
    result.plateNumber = proposal.plateNumber;
    result.spotId = proposal.spotId;
    result.entryRoute = proposal.entryRoute;
    result.exitRoute = proposal.exitRoute;
    result.score = proposal.score.total;
    result.nearbyOccupiedSpots = proposal.nearbyOccupiedSpots;
    result.breakdown = proposal.score;
    result.entranceIndex = proposal.entranceIndex;
    result.exitIndex = proposal.exitIndex;
    result.strategy = proposal.strategy;
    return result;
}

ParkingSpot *ParkingService::findSpot(const std::string &spotId)
{
    const auto spot = std::find_if(
        spots_.begin(), spots_.end(),
        [&spotId](const ParkingSpot &item) { return item.identifier() == spotId; });
    if (spot == spots_.end()) {
        return nullptr;
    }
    return &(*spot);
}

const ParkingSpot *ParkingService::findReservedSpot(const std::string &plateNumber) const
{
    const auto spot = std::find_if(
        spots_.begin(), spots_.end(), [&plateNumber](const ParkingSpot &item) {
            return item.status() == SpotStatus::Reserved && hasSamePlate(item.parkedVehicle(), plateNumber);
        });
    if (spot == spots_.end()) {
        return nullptr;
    }
    return &(*spot);
}

ParkingSpot *ParkingService::findReservedSpot(const std::string &plateNumber)
{
    return const_cast<ParkingSpot *>(
        static_cast<const ParkingService *>(this)->findReservedSpot(plateNumber));
}

void ParkingService::ensureReachable() const
{
    std::vector<Point> accessPoints;
    accessPoints.reserve(spots_.size());
    for (const ParkingSpot &spot : spots_) {
        accessPoints.push_back(spot.accessPoint());
    }

    std::vector<char> reachableFromEntrance(spots_.size(), 0);
    for (const Point &entrance : layout_.entrances()) {
        const std::vector<Route> routes = planner_.planFromToTargets(entrance, accessPoints);
        for (std::size_t index = 0; index < routes.size(); ++index) {
            if (!routes[index].points.empty()) {
                reachableFromEntrance[index] = 1;
            }
        }
    }

    std::vector<char> reachableToExit(spots_.size(), 0);
    for (const Point &exit : layout_.exits()) {
        const std::vector<Route> routes = planner_.planFromToTargets(exit, accessPoints);
        for (std::size_t index = 0; index < routes.size(); ++index) {
            if (!routes[index].points.empty()) {
                reachableToExit[index] = 1;
            }
        }
    }

    for (std::size_t index = 0; index < spots_.size(); ++index) {
        if (!reachableFromEntrance[index]) {
            throw std::invalid_argument("spot " + spots_[index].identifier() + " is unreachable from any entrance");
        }
        if (!reachableToExit[index]) {
            throw std::invalid_argument("spot " + spots_[index].identifier() + " cannot reach any exit");
        }
    }
}

} // namespace smartpark
