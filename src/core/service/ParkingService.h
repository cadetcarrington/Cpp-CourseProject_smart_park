#pragma once
#include "core/model/Booking.h"
#include "core/model/ParkingLayout.h"
#include "core/model/ParkingRecord.h"
#include "core/model/Vehicle.h"
#include "core/persistence/ParkingRepository.h"
#include "core/service/GridPlanner.h"
#include "core/service/SpotAllocator.h"
#include "core/service/Billing.h"
#include <chrono>
#include <optional>
#include <string>
#include <vector>
namespace smartpark{
struct AllocationResult{
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
struct BookingResult{
    Booking booking;
    AllocationResult allocation;
};
class ParkingService{
public:
    explicit ParkingService(ParkingLayout layout,
                            AllocationStrategy strategy = AllocationStrategy::WeightedCost,
                            ParkingRepository *repository = nullptr,
                            BillingRule billingRule = BillingRule{},
                            BookingPolicy bookingPolicy = BookingPolicy{});
    const ParkingLayout &layout() const noexcept;
    const std::vector<ParkingSpot> &spots() const noexcept;
    const BillingService &billing() const noexcept;
    const BookingPolicy &bookingPolicy() const noexcept;
    AllocationStrategy strategy() const noexcept;
    void setStrategy(AllocationStrategy strategy) noexcept;
    void setWeights(AllocationWeights weights) noexcept;
    void expireReservations(ParkingRecord::TimePoint now);
    void expireBookings(ParkingRecord::TimePoint now);
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
    std::optional<BookingResult> createBooking(
        const Vehicle &vehicle,
        ParkingRecord::TimePoint arrivalTime,
        ParkingRecord::TimePoint now = ParkingRecord::Clock::now());
    std::optional<AllocationResult> confirmBooking(
        const std::string &plateNumber,
        ParkingRecord::TimePoint now = ParkingRecord::Clock::now());
    bool cancelBooking(const std::string &plateNumber,
                       ParkingRecord::TimePoint now = ParkingRecord::Clock::now());
    const std::vector<Booking> &bookings() const noexcept;
    std::optional<Booking> activeBooking(const std::string &plateNumber) const;
    double pendingDeposits() const noexcept;
    double forfeitedDeposits() const noexcept;
    int remainingSpots() const noexcept;
    int occupiedSpots() const noexcept;
    int reservedSpots() const noexcept;
    const std::vector<ParkingRecord> &records() const noexcept;
    std::optional<ParkingRecord> activeRecord(const std::string &plateNumber) const;
    double totalRevenue() const noexcept;
private:
    AllocationResult toResult(const AllocationProposal &proposal) const;
    void restore(ParkingRepository &repository);
    ParkingSpot *findSpot(const std::string &spotId);
    const ParkingSpot *findReservedSpot(const std::string &plateNumber) const;
    ParkingSpot *findReservedSpot(const std::string &plateNumber);
    bool hasActiveBookingForSpot(const std::string &spotId) const;
    Booking *findActiveBooking(const std::string &plateNumber);
    const Booking *findActiveBooking(const std::string &plateNumber) const;
    std::string newBookingId();
    void ensureReachable() const;
    ParkingLayout layout_;
    std::vector<ParkingSpot> spots_;
    GridPlanner planner_;
    SpotAllocator allocator_;
    std::vector<ParkingRecord> records_;
    std::vector<Booking> bookings_;
    ParkingRepository *repository_{nullptr};
    BillingService billing_;
    BookingPolicy bookingPolicy_;
    std::size_t bookingSequence_{0};
};
} // namespace smartpark
