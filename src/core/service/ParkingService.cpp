#include "core/service/ParkingService.h"
#include "core/util/TimeUtil.h"
#include "core/service/Billing.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace smartpark{

    namespace{
    bool hasSamePlate(const std::optional<Vehicle> &vehicle, const std::string &plateNumber){
        return vehicle && vehicle->plateNumber() == plateNumber;
    }
    } // namespace

    ParkingService::ParkingService(ParkingLayout layout, AllocationStrategy strategy,
                                   ParkingRepository *repository, BillingRule billingRule,
                                   BookingPolicy bookingPolicy)
        : layout_(std::move(layout))
        , spots_(layout_.spots())
        , planner_(layout_.siteWidth(), layout_.siteHeight(), spots_)
        , allocator_(layout_, planner_)
        , billing_(billingRule)
        , bookingPolicy_(bookingPolicy){

        if (bookingPolicy_.advanceDays < 1){
            throw std::invalid_argument("booking advance days must be at least 1");
        }
        if (!std::isfinite(bookingPolicy_.deposit) || bookingPolicy_.deposit < 0.0){
            throw std::invalid_argument("booking deposit must be a non-negative number");
        }
        if (bookingPolicy_.gracePeriod < std::chrono::minutes::zero()){
            throw std::invalid_argument("booking grace period must be non-negative");
        }
        allocator_.setStrategy(strategy);
        ensureReachable();
        if (repository != nullptr){
            repository_ = repository;
            if (!repository_->saveLayout(layout_)){
                throw std::runtime_error("cannot persist parking layout: "
                                        + repository_->lastError());
            }
            restore(*repository_);
        }
    }

    const ParkingLayout &ParkingService::layout() const noexcept{
        return layout_;
    }

    const std::vector<ParkingSpot> &ParkingService::spots() const noexcept{
        return spots_;
    }

const BillingService &ParkingService::billing() const noexcept{
    return billing_;
}

const BookingPolicy &ParkingService::bookingPolicy() const noexcept{
    return bookingPolicy_;
}

    AllocationStrategy ParkingService::strategy() const noexcept{
        return allocator_.strategy();
    }

    void ParkingService::setStrategy(AllocationStrategy strategy) noexcept{
        allocator_.setStrategy(strategy);
    }

    void ParkingService::setWeights(AllocationWeights weights) noexcept{
        allocator_.setWeights(weights);
    }

    void ParkingService::expireReservations(ParkingRecord::TimePoint now){
        for (ParkingSpot &spot : spots_){
            if (spot.status() != SpotStatus::Reserved){
                continue;
            }
            // 预约产生的 Reserved 由 expireBookings 统一处理（含爽约扣定金），这里跳过。
            if (hasActiveBookingForSpot(spot.identifier())){
                continue;
            }
            const std::optional<Vehicle> previousVehicle = spot.parkedVehicle();
            const std::optional<ParkingSpot::TimePoint> previousExpiry =
                spot.reservationExpiresAt();
            if (!spot.expireReservation(now)){
                continue;
            }
            if (repository_ != nullptr && !repository_->saveSpotState(spot)
                && previousVehicle && previousExpiry){
                spot.reserve(*previousVehicle, *previousExpiry);
            }
        }
    }

    std::optional<AllocationResult> ParkingService::allocate(const Vehicle &vehicle){
        return enter(vehicle);
    }

    std::optional<AllocationResult> ParkingService::reserve(
        const Vehicle &vehicle, ParkingRecord::TimePoint now, std::chrono::seconds ttl){
        expireReservations(now);
        expireBookings(now);
        if (ttl <= std::chrono::seconds::zero() || !timeutil::isValid(now)
            || !timeutil::canAdd(now, ttl) || activeRecord(vehicle.plateNumber())
            || findReservedSpot(vehicle.plateNumber()) != nullptr){
            return std::nullopt;
        }
        const std::optional<AllocationProposal> proposal = allocator_.propose(vehicle, spots_);
        if (!proposal){
            return std::nullopt;
        }
        ParkingSpot *spot = findSpot(proposal->spotId);
        if (spot == nullptr || !spot->reserve(vehicle, now + ttl)){
            return std::nullopt;
        }
        if (repository_ != nullptr && !repository_->saveReservation(*spot)){
            spot->release();
            return std::nullopt;
        }
        return toResult(*proposal);
    }

    std::optional<AllocationResult> ParkingService::enter(
        const Vehicle &vehicle, ParkingRecord::TimePoint entryTime){
        if (!timeutil::isValid(entryTime)){
            return std::nullopt;
        }
        expireReservations(entryTime);
        expireBookings(entryTime);
        if (activeRecord(vehicle.plateNumber())){
            return std::nullopt;
        }
        std::string requiredSpotId;
        if (const ParkingSpot *reserved = findReservedSpot(vehicle.plateNumber())){
            requiredSpotId = reserved->identifier();
        }
        const std::optional<AllocationProposal> proposal =
            allocator_.propose(vehicle, spots_, requiredSpotId);
        if (!proposal){
            return std::nullopt;
        }
        ParkingSpot *spot = findSpot(proposal->spotId);
        if (spot == nullptr || !spot->occupy(vehicle)){
            return std::nullopt;
        }
        records_.emplace_back(vehicle.plateNumber(), proposal->spotId, entryTime);
        if (repository_ != nullptr && !repository_->saveEntry(records_.back(), *spot)){
            records_.pop_back();
            spot->release();
            return std::nullopt;
        }
        return toResult(*proposal);
    }

    std::optional<ParkingRecord> ParkingService::leave(
        const std::string &plateNumber, ParkingRecord::TimePoint exitTime){
        if (!timeutil::isValid(exitTime)){
            return std::nullopt;
        }
        expireReservations(exitTime);
        expireBookings(exitTime);
        const auto record = std::find_if(
            records_.begin(), records_.end(), [&plateNumber](const ParkingRecord &item){
                return item.plateNumber() == plateNumber && !item.isClosed();
            });
        if (record == records_.end() || exitTime < record->entryTime()){
            return std::nullopt;
        }
        ParkingSpot *spot = findSpot(record->spotId());
        if (spot == nullptr || spot->status() != SpotStatus::Occupied){
            return std::nullopt;
        }
        const auto duration = std::chrono::duration_cast<std::chrono::seconds>(
            exitTime - record->entryTime());
        const double fee = billing_.calculateFee(duration);

        if (repository_ != nullptr && !repository_->saveExit(*record, exitTime, fee)) {
            return std::nullopt;
        }
        if (!spot->release()) {
            return std::nullopt;
        }

        record->close(exitTime, fee);
        return *record;
    }

    bool ParkingService::cancelReservation(const std::string &plateNumber){
        ParkingSpot *spot = findReservedSpot(plateNumber);
        if (spot == nullptr){
            return false;
        }
        const auto previousExpiry = spot->reservationExpiresAt();
        const Vehicle previousVehicle = *spot->parkedVehicle();
        if (!spot->release()){
            return false;
        }
        if (repository_ != nullptr && !repository_->saveSpotState(*spot)){
            spot->reserve(previousVehicle, *previousExpiry);
            return false;
        }
        return true;
    }

    void ParkingService::expireBookings(ParkingRecord::TimePoint now){
        if (!timeutil::isValid(now)){
            return;
        }
        for (Booking &booking : bookings_){
            if (booking.status() != BookingStatus::Booked
                || booking.arrivalDeadline() > now){
                continue;
            }
            // 爽约：释放预留车位，随后没收定金。
            if (ParkingSpot *spot = findSpot(booking.spotId())){
                if (spot->status() == SpotStatus::Reserved && spot->parkedVehicle()
                    && spot->parkedVehicle()->plateNumber() == booking.plateNumber()){
                    const Vehicle previousVehicle = *spot->parkedVehicle();
                    const std::optional<ParkingSpot::TimePoint> previousExpiry =
                        spot->reservationExpiresAt();
                    if (spot->release()){
                        if (repository_ != nullptr && !repository_->saveSpotState(*spot)
                            && previousExpiry){
                            spot->reserve(previousVehicle, *previousExpiry);
                        }
                    }
                }
            }
            booking.markNoShow();
            if (repository_ != nullptr){
                repository_->saveBookingStatus(booking);
            }
        }
    }

    std::optional<BookingResult> ParkingService::createBooking(
        const Vehicle &vehicle, ParkingRecord::TimePoint arrivalTime,
        ParkingRecord::TimePoint now){
        expireBookings(now);
        if (!timeutil::isValid(now) || !timeutil::isValid(arrivalTime)
            || arrivalTime <= now
            || !timeutil::canAdd(arrivalTime, bookingPolicy_.gracePeriod)
            || activeRecord(vehicle.plateNumber())
            || findReservedSpot(vehicle.plateNumber()) != nullptr
            || findActiveBooking(vehicle.plateNumber()) != nullptr){
            return std::nullopt;
        }
        const auto maxAdvance =
            std::chrono::hours(24 * bookingPolicy_.advanceDays);
        if (arrivalTime - now > maxAdvance){
            return std::nullopt;
        }
        const std::optional<AllocationProposal> proposal =
            allocator_.propose(vehicle, spots_);
        if (!proposal){
            return std::nullopt;
        }
        ParkingSpot *spot = findSpot(proposal->spotId);
        if (spot == nullptr){
            return std::nullopt;
        }
        const ParkingRecord::TimePoint deadline =
            arrivalTime + bookingPolicy_.gracePeriod;
        if (!spot->reserve(vehicle, deadline)){
            return std::nullopt;
        }
        bookings_.emplace_back(newBookingId(), vehicle.plateNumber(),
                               proposal->spotId, now, arrivalTime, deadline,
                               bookingPolicy_.deposit);
        if (repository_ != nullptr){
            if (!repository_->saveReservation(*spot)){
                spot->release();
                bookings_.pop_back();
                return std::nullopt;
            }
            if (!repository_->saveBooking(bookings_.back())){
                spot->release();
                bookings_.pop_back();
                repository_->saveSpotState(*spot);
                return std::nullopt;
            }
        }
        BookingResult result{bookings_.back(), toResult(*proposal)};
        return result;
    }

    std::optional<AllocationResult> ParkingService::confirmBooking(
        const std::string &plateNumber, ParkingRecord::TimePoint now){
        expireBookings(now);
        if (!timeutil::isValid(now)){
            return std::nullopt;
        }
        Booking *booking = findActiveBooking(plateNumber);
        if (booking == nullptr){
            return std::nullopt;
        }
        if (now < booking->arrivalTime() || now > booking->arrivalDeadline()){
            return std::nullopt;
        }
        ParkingSpot *spot = findReservedSpot(plateNumber);
        if (spot == nullptr || !spot->parkedVehicle()){
            return std::nullopt;
        }
        const Vehicle vehicle = *spot->parkedVehicle();
        const std::optional<AllocationProposal> proposal =
            allocator_.propose(vehicle, spots_, spot->identifier());
        if (!proposal){
            return std::nullopt;
        }
        const ParkingRecord::TimePoint deadline = booking->arrivalDeadline();
        if (!spot->occupy(vehicle)){
            return std::nullopt;
        }
        records_.emplace_back(vehicle.plateNumber(), booking->spotId(), now);
        if (!booking->checkIn()){
            records_.pop_back();
            spot->release();
            spot->reserve(vehicle, deadline);
            return std::nullopt;
        }
        if (repository_ != nullptr
            && !repository_->saveBookingCheckIn(*booking, records_.back(), *spot)){
            records_.pop_back();
            spot->release();
            spot->reserve(vehicle, deadline);
            *booking = Booking(booking->id(), vehicle.plateNumber(),
                               booking->spotId(), booking->createdAt(),
                               booking->arrivalTime(), deadline, booking->deposit());
            return std::nullopt;
        }
        return toResult(*proposal);
    }

    bool ParkingService::cancelBooking(const std::string &plateNumber,
                                       ParkingRecord::TimePoint now){
        expireBookings(now);
        if (!timeutil::isValid(now)){
            return false;
        }
        Booking *booking = findActiveBooking(plateNumber);
        if (booking == nullptr){
            return false;
        }
        if (now >= booking->arrivalTime()){
            return false;
        }
        const std::string spotId = booking->spotId();
        const ParkingRecord::TimePoint deadline = booking->arrivalDeadline();
        ParkingSpot *spot = findSpot(spotId);
        std::optional<Vehicle> releasedVehicle;
        if (spot != nullptr && spot->status() == SpotStatus::Reserved
            && spot->parkedVehicle()
            && spot->parkedVehicle()->plateNumber() == plateNumber){
            releasedVehicle = *spot->parkedVehicle();
            if (!spot->release()){
                return false;
            }
            if (repository_ != nullptr && !repository_->saveSpotState(*spot)){
                spot->reserve(*releasedVehicle, deadline);
                return false;
            }
        }
        if (!booking->cancel()){
            return false;
        }
        if (repository_ != nullptr && !repository_->saveBookingStatus(*booking)){
            *booking = Booking(booking->id(), plateNumber, spotId,
                               booking->createdAt(), booking->arrivalTime(),
                               deadline, booking->deposit());
            if (releasedVehicle && spot != nullptr){
                spot->reserve(*releasedVehicle, deadline);
                repository_->saveSpotState(*spot);
            }
            return false;
        }
        return true;
    }

    const std::vector<Booking> &ParkingService::bookings() const noexcept{
        return bookings_;
    }

    std::optional<Booking> ParkingService::activeBooking(
        const std::string &plateNumber) const{
        const Booking *booking = findActiveBooking(plateNumber);
        if (booking == nullptr){
            return std::nullopt;
        }
        return *booking;
    }

    double ParkingService::pendingDeposits() const noexcept{
        double total = 0.0;
        for (const Booking &booking : bookings_){
            if (booking.status() == BookingStatus::Booked){
                total += booking.deposit();
            }
        }
        return total;
    }

    double ParkingService::forfeitedDeposits() const noexcept{
        double total = 0.0;
        for (const Booking &booking : bookings_){
            if (booking.status() == BookingStatus::NoShow){
                total += booking.deposit();
            }
        }
        return total;
    }

    bool ParkingService::release(const std::string &spotId){
        ParkingSpot *spot = findSpot(spotId);
        if (spot == nullptr || spot->status() == SpotStatus::Available){
            return false;
        }
        if (spot->status() == SpotStatus::Occupied){
            const auto record = std::find_if(
                records_.begin(), records_.end(), [&spotId](const ParkingRecord &item){
                    return item.spotId() == spotId && !item.isClosed();
                });
            if (record == records_.end()){
                // 防御路径：占用但无活跃记录，只写一次车位状态。
                const std::optional<Vehicle> previousVehicle = spot->parkedVehicle();
                if (!spot->release()){
                    return false;
                }
                if (repository_ != nullptr && !repository_->saveSpotState(*spot)){
                    if (previousVehicle){
                        spot->occupy(*previousVehicle);
                    }
                    return false;
                }
                return true;
            }
            // 正常离场：saveExit 在单个事务里同时关闭记录并置空车位，避免双写。
            const ParkingRecord::TimePoint exitTime = ParkingRecord::Clock::now();
            const auto duration = std::chrono::duration_cast<std::chrono::seconds>(
                exitTime - record->entryTime());
            const double fee = billing_.calculateFee(duration);
            if (repository_ != nullptr && !repository_->saveExit(*record, exitTime, fee)){
                return false;
            }
            record->close(exitTime, fee);
            if (!spot->release()){
                return false;
            }
            return true;
        }

        // 预留释放：单次写入车位状态，失败时回滚内存，保证与数据库一致。
        const std::optional<Vehicle> previousVehicle = spot->parkedVehicle();
        const std::optional<ParkingSpot::TimePoint> previousExpiry =
            spot->reservationExpiresAt();
        if (!spot->release()){
            return false;
        }
        if (repository_ != nullptr && !repository_->saveSpotState(*spot)){
            if (previousVehicle && previousExpiry){
                spot->reserve(*previousVehicle, *previousExpiry);
            }
            return false;
        }
        return true;
    }

    int ParkingService::remainingSpots() const noexcept{
        return static_cast<int>(std::count_if(
            spots_.begin(), spots_.end(),
            [](const ParkingSpot &spot) { return spot.isAvailable(); }));
    }
    int ParkingService::occupiedSpots() const noexcept{
        return static_cast<int>(std::count_if(
            spots_.begin(), spots_.end(),
            [](const ParkingSpot &spot) { return spot.status() == SpotStatus::Occupied; }));
    }
    int ParkingService::reservedSpots() const noexcept{
        return static_cast<int>(std::count_if(
            spots_.begin(), spots_.end(),
            [](const ParkingSpot &spot) { return spot.status() == SpotStatus::Reserved; }));
    }
    const std::vector<ParkingRecord> &ParkingService::records() const noexcept{
        return records_;
    }
    std::optional<ParkingRecord> ParkingService::activeRecord(
        const std::string &plateNumber) const{
        const auto record = std::find_if(
            records_.begin(), records_.end(), [&plateNumber](const ParkingRecord &item){
                return item.plateNumber() == plateNumber && !item.isClosed();
            });
        if (record == records_.end()){
            return std::nullopt;
        }
        return *record;
    }
    double ParkingService::totalRevenue() const noexcept{
        double total = 0.0;
        for (const ParkingRecord &record : records_){
            if (record.isClosed()){
                total += record.fee();
            }
        }
        return total;
    }
    AllocationResult ParkingService::toResult(const AllocationProposal &proposal) const{
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
    ParkingSpot *ParkingService::findSpot(const std::string &spotId){
        const auto spot = std::find_if(
            spots_.begin(), spots_.end(),
            [&spotId](const ParkingSpot &item) { return item.identifier() == spotId; });
        if (spot == spots_.end()){
            return nullptr;
        }
        return &(*spot);
    }
    const ParkingSpot *ParkingService::findReservedSpot(const std::string &plateNumber) const{
        const auto spot = std::find_if(
            spots_.begin(), spots_.end(), [&plateNumber](const ParkingSpot &item){
                return item.status() == SpotStatus::Reserved && hasSamePlate(item.parkedVehicle(), plateNumber);
            });
        if (spot == spots_.end()){
            return nullptr;
        }
        return &(*spot);
    }
    ParkingSpot *ParkingService::findReservedSpot(const std::string &plateNumber){
        return const_cast<ParkingSpot *>(
            static_cast<const ParkingService *>(this)->findReservedSpot(plateNumber));
    }
    const Booking *ParkingService::findActiveBooking(const std::string &plateNumber) const{
        const auto booking = std::find_if(
            bookings_.begin(), bookings_.end(), [&plateNumber](const Booking &item){
                return item.status() == BookingStatus::Booked
                    && item.plateNumber() == plateNumber;
            });
        if (booking == bookings_.end()){
            return nullptr;
        }
        return &(*booking);
    }
    Booking *ParkingService::findActiveBooking(const std::string &plateNumber){
        return const_cast<Booking *>(
            static_cast<const ParkingService *>(this)->findActiveBooking(plateNumber));
    }
    bool ParkingService::hasActiveBookingForSpot(const std::string &spotId) const{
        return std::any_of(
            bookings_.begin(), bookings_.end(), [&spotId](const Booking &item){
                return item.status() == BookingStatus::Booked
                    && item.spotId() == spotId;
            });
    }
    std::string ParkingService::newBookingId(){
        const auto now = Booking::Clock::now().time_since_epoch();
        const auto millis =
            std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        std::ostringstream identifier;
        identifier << 'B' << millis << '-' << (bookingSequence_++);
        return identifier.str();
    }
    void ParkingService::ensureReachable() const{
        std::vector<Point> accessPoints;
        accessPoints.reserve(spots_.size());
        for (const ParkingSpot &spot : spots_){
            accessPoints.push_back(spot.accessPoint());
        }
        std::vector<char> reachableFromEntrance(spots_.size(), 0);
        for (const Point &entrance : layout_.entrances()){
            const std::vector<Route> routes = planner_.planFromToTargets(entrance, accessPoints);
            for (std::size_t index = 0; index < routes.size(); ++index){
                if (!routes[index].points.empty()){
                    reachableFromEntrance[index] = 1;
                }
            }
        }
        std::vector<char> reachableToExit(spots_.size(), 0);
        for (const Point &exit : layout_.exits()){
            const std::vector<Route> routes = planner_.planFromToTargets(exit, accessPoints);
            for (std::size_t index = 0; index < routes.size(); ++index){
                if (!routes[index].points.empty()){
                    reachableToExit[index] = 1;
                }
            }
        }
        for (std::size_t index = 0; index < spots_.size(); ++index){
            if (!reachableFromEntrance[index]){
                throw std::invalid_argument("spot " + spots_[index].identifier() + " is unreachable from any entrance");
            }
            if (!reachableToExit[index]){
                throw std::invalid_argument("spot " + spots_[index].identifier() + " cannot reach any exit");
            }
        }
    }
    void ParkingService::restore(ParkingRepository &repository){
        const std::vector<PersistedRecord> persistedRecords = repository.loadRecords();
        const std::vector<PersistedSpotState> persistedSpots = repository.loadSpotStates();
        if (!repository.lastError().empty()){
            throw std::runtime_error("cannot restore parking data: " + repository.lastError());
        }
        for (const PersistedRecord &item : persistedRecords){
            if (findSpot(item.spotId) == nullptr){
                throw std::runtime_error("persisted record refers to an unknown spot: " + item.spotId);
            }
            ParkingRecord record(item.plateNumber, item.spotId, item.entryTime);
            if (item.exitTime){
                if (!record.close(*item.exitTime, item.fee)){
                    throw std::runtime_error("invalid persisted parking record time");
                }
            } else if (std::any_of(records_.begin(), records_.end(), [&item](const ParkingRecord &existing){
                        return existing.plateNumber() == item.plateNumber && !existing.isClosed();
                    })){
                throw std::runtime_error("duplicate active persisted parking record: " + item.plateNumber);
            }
            records_.push_back(std::move(record));
        }
        for (const PersistedSpotState &state : persistedSpots){
            ParkingSpot *spot = findSpot(state.spotId);
            if (spot == nullptr){
                throw std::runtime_error("persisted state refers to an unknown spot: " + state.spotId);
            }
            if (state.status == SpotStatus::Occupied){
                if (!state.vehicle || !activeRecord(state.vehicle->plateNumber())
                    || activeRecord(state.vehicle->plateNumber())->spotId() != state.spotId
                    || !spot->occupy(*state.vehicle)){
                    throw std::runtime_error("inconsistent occupied parking spot: " + state.spotId);
                }
            } else if (state.status == SpotStatus::Reserved){
                if (!state.vehicle || !state.reservationExpiresAt
                    || !spot->reserve(*state.vehicle, *state.reservationExpiresAt)){
                    throw std::runtime_error("inconsistent reserved parking spot: " + state.spotId);
                }
            } else if (state.status == SpotStatus::Disabled){
                if (!spot->disable()){
                    throw std::runtime_error("inconsistent disabled parking spot: " + state.spotId);
                }
            } else if (state.status == SpotStatus::Available){
                if (spot->status() != SpotStatus::Available){
                    throw std::runtime_error("inconsistent available parking spot: " + state.spotId);
                }
            }
        }
        for (const ParkingRecord &record : records_){
            if (record.isClosed()){
                continue;
            }
            const ParkingSpot *spot = findSpot(record.spotId());
            if (spot == nullptr || spot->status() != SpotStatus::Occupied
                || !spot->parkedVehicle()
                || spot->parkedVehicle()->plateNumber() != record.plateNumber()){
                throw std::runtime_error("active parking record does not match its spot: "
                                        + record.plateNumber());
            }
        }
        for (const ParkingRecord &record : records_){
            if (record.isClosed()){
                continue;
            }
            const ParkingSpot *spot = findSpot(record.spotId());
            if (spot == nullptr || spot->status() != SpotStatus::Occupied
                || !spot->parkedVehicle()
                || spot->parkedVehicle()->plateNumber() != record.plateNumber()){
                throw std::runtime_error("active persisted record has inconsistent spot state: "
                                        + record.spotId());
            }
        }
        const std::vector<Booking> persistedBookings = repository.loadBookings();
        if (!repository.lastError().empty()){
            throw std::runtime_error("cannot restore booking data: " + repository.lastError());
        }
        for (const Booking &booking : persistedBookings){
            const ParkingSpot *spot = findSpot(booking.spotId());
            if (spot == nullptr){
                throw std::runtime_error("persisted booking refers to an unknown spot: "
                                        + booking.spotId());
            }
            if (findActiveBooking(booking.plateNumber()) != nullptr){
                throw std::runtime_error("duplicate active persisted booking: "
                                        + booking.plateNumber());
            }
            if (booking.status() == BookingStatus::Booked){
                if (spot->status() != SpotStatus::Reserved || !spot->parkedVehicle()
                    || spot->parkedVehicle()->plateNumber() != booking.plateNumber()){
                    throw std::runtime_error("active booking does not match its reserved spot: "
                                            + booking.plateNumber());
                }
            } else if (booking.status() == BookingStatus::CheckedIn){
                const std::optional<ParkingRecord> record = activeRecord(booking.plateNumber());
                if (!record || record->spotId() != booking.spotId()
                    || spot->status() != SpotStatus::Occupied
                    || !spot->parkedVehicle()
                    || spot->parkedVehicle()->plateNumber() != booking.plateNumber()){
                    throw std::runtime_error("checked-in booking does not match its parking record: "
                                            + booking.plateNumber());
                }
            } else if (spot->status() == SpotStatus::Reserved && spot->parkedVehicle()
                       && spot->parkedVehicle()->plateNumber() == booking.plateNumber()){
                throw std::runtime_error("resolved booking still holds its reserved spot: "
                                        + booking.plateNumber());
            }
            bookings_.push_back(booking);
        }
    }

} // namespace smartpark
