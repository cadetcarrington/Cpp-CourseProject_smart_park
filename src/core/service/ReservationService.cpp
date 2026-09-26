#include "core/service/ReservationService.h"
#include "core/service/ParkingService.h"
#include "core/service/SpotAllocator.h"
#include "core/util/TimeUtil.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace smartpark{
namespace{
bool blocksTimeWindow(ReservationStatus status) noexcept{
    return status == ReservationStatus::PendingPayment
        || status == ReservationStatus::Confirmed
        || status == ReservationStatus::CheckedIn;
}
} // namespace

ReservationService::ReservationService(ParkingService &parking, ReservationRule rule)
    : park_(parking)
    , rule_(rule){
    if (rule_.maxAdvanceDays < 1){
        throw std::invalid_argument("reservation advance days must be at least 1");
    }
    if (rule_.minLeadTime < std::chrono::minutes::zero()){
        throw std::invalid_argument("reservation lead time must be non-negative");
    }
    if (rule_.minDuration <= std::chrono::minutes::zero()){
        throw std::invalid_argument("reservation duration must be positive");
    }
    if (rule_.gracePeriod < std::chrono::minutes::zero()){
        throw std::invalid_argument("reservation grace period must be non-negative");
    }
    if (rule_.lockLeadTime < std::chrono::minutes::zero()){
        throw std::invalid_argument("reservation lock lead time must be non-negative");
    }
    if (!std::isfinite(rule_.deposit) || rule_.deposit < 0.0){
        throw std::invalid_argument("reservation deposit must be a non-negative number");
    }
}

const ReservationRule &ReservationService::rule() const noexcept{
    return rule_;
}

std::optional<ReservationResult> ReservationService::create(
    const Vehicle &vehicle, ParkingRecord::TimePoint startTime,
    ParkingRecord::TimePoint endTime, ParkingRecord::TimePoint now,
    bool accessible){
    lastError_.clear();
    sweep(now);
    if (!timeutil::isValid(now) || !timeutil::isValid(startTime)
        || !timeutil::isValid(endTime)){
        lastError_ = "预约时间超出支持范围";
        return std::nullopt;
    }
    if (endTime <= startTime || endTime - startTime < rule_.minDuration){
        lastError_ = "预约时长不足，最短 "
            + std::to_string(rule_.minDuration.count()) + " 分钟";
        return std::nullopt;
    }
    if (startTime < now + rule_.minLeadTime){
        lastError_ = "预约需至少提前 "
            + std::to_string(rule_.minLeadTime.count()) + " 分钟";
        return std::nullopt;
    }
    const auto maxAdvance = std::chrono::hours(24 * rule_.maxAdvanceDays);
    if (startTime - now > maxAdvance || endTime - now > maxAdvance){
        lastError_ = "只能预约未来 "
            + std::to_string(rule_.maxAdvanceDays) + " 天内的时间段";
        return std::nullopt;
    }
    if (park_.activeRecord(vehicle.plateNumber())){
        lastError_ = "车辆已在场内，不能预约";
        return std::nullopt;
    }
    if (findOpenPtr(vehicle.plateNumber()) != nullptr){
        lastError_ = "该车牌已有未结束的时段预约";
        return std::nullopt;
    }
    if (park_.findActiveBooking(vehicle.plateNumber()) != nullptr){
        lastError_ = "该车牌已有生效的预约单";
        return std::nullopt;
    }
    // 临时副本上选位：窗口开始前到期的短时锁视为空闲，窗口重叠的预约视为占用。
    std::vector<ParkingSpot> candidates = park_.spots_;
    if (accessible){
        // 无障碍关怀：只在无障碍车位中挑选。
        candidates.erase(std::remove_if(
            candidates.begin(), candidates.end(),
            [](const ParkingSpot &spot){
                return spot.type() != SpotType::Accessible;
            }), candidates.end());
    }
    for (ParkingSpot &spot : candidates){
        if (overlapsOpenReservation(spot.identifier(), startTime, endTime)){
            if (spot.isAvailable()){
                spot.occupy(vehicle);
            }
            continue;
        }
        if (spot.status() == SpotStatus::Reserved && spot.reservationExpiresAt()
            && *spot.reservationExpiresAt() <= startTime){
            spot.release();
        }
    }
    // 无障碍路线：提高转向权重，优先少转弯的直达路径。
    const AllocationWeights savedWeights = park_.allocator_.weights();
    if (accessible){
        AllocationWeights weights = savedWeights;
        weights.turnCount = weights.turnCount * 4.0 + 1.0;
        park_.allocator_.setWeights(weights);
    }
    const std::optional<AllocationProposal> proposal =
        park_.allocator_.propose(vehicle, candidates);
    park_.allocator_.setWeights(savedWeights);
    if (!proposal){
        lastError_ = accessible ? "暂无空闲无障碍车位" : "没有可预约的车位";
        return std::nullopt;
    }
    ParkingSpot *spot = findSpot(proposal->spotId);
    if (spot == nullptr
        || !(spot->status() == SpotStatus::Available
             || (spot->status() == SpotStatus::Reserved
                 && spot->reservationExpiresAt()
                 && *spot->reservationExpiresAt() <= startTime))){
        lastError_ = "没有可预约的车位";
        return std::nullopt;
    }
    const std::chrono::minutes grace =
        accessible ? rule_.gracePeriod * 2 : rule_.gracePeriod;
    if (!timeutil::canAdd(startTime, grace)){
        lastError_ = "预约时间超出支持范围";
        return std::nullopt;
    }
    const ParkingRecord::TimePoint graceDeadline = startTime + grace;
    const std::string reservationId = newReservationId();
    const double deposit = accessible ? 0.0 : rule_.deposit;
    std::optional<std::string> chargeTransaction;
    if (deposit > 0.0){
        chargeTransaction = gateway_.charge(reservationId, vehicle.plateNumber(),
                                            deposit, now);
        if (!chargeTransaction){
            lastError_ = "定金支付失败";
            return std::nullopt;
        }
    }
    ExpectedRoute expectedRoute;
    expectedRoute.entranceIndex = proposal->entranceIndex;
    expectedRoute.exitIndex = proposal->exitIndex;
    expectedRoute.entryRoute = proposal->entryRoute;
    expectedRoute.exitRoute = proposal->exitRoute;
    Reservation reservation(reservationId, vehicle.plateNumber(), vehicle.type(),
                            proposal->spotId, now, startTime, endTime,
                            graceDeadline, deposit,
                            chargeTransaction.value_or(std::string()),
                            ReservationStatus::Confirmed, DepositState::Pending,
                            std::move(expectedRoute), accessible);
    if (park_.repository_ != nullptr){
        std::optional<DepositPayment> chargePayment;
        if (chargeTransaction){
            chargePayment = DepositPayment{*chargeTransaction, reservationId,
                vehicle.plateNumber(), DepositPayment::Kind::Charge, deposit,
                DepositPayment::Status::Succeeded, now};
        }
        if (!park_.repository_->saveReservationOrder(reservation,
                chargePayment ? &*chargePayment : nullptr)){
            lastError_ = "预约写入数据库失败: " + park_.repository_->lastError();
            return std::nullopt;
        }
        if (chargePayment){
            payments_.push_back(*chargePayment);
        }
    } else if (chargeTransaction){
        payments_.push_back(DepositPayment{*chargeTransaction, reservationId,
            vehicle.plateNumber(), DepositPayment::Kind::Charge, deposit,
            DepositPayment::Status::Succeeded, now});
    }
    reservations_.push_back(std::move(reservation));
    park_.audit(accessible ? "reservation_create_accessible" : "reservation_create",
                vehicle.plateNumber() + "@" + proposal->spotId);
    ReservationResult result{reservations_.back(), park_.toResult(*proposal)};
    return result;
}

bool ReservationService::cancel(const std::string &plateNumber,
                                ParkingRecord::TimePoint now){
    lastError_.clear();
    sweep(now);
    Reservation *reservation = findOpenPtr(plateNumber);
    if (reservation == nullptr
        || (reservation->status() != ReservationStatus::Confirmed
            && reservation->status() != ReservationStatus::PendingPayment)){
        lastError_ = "该车牌没有可取消的时段预约";
        return false;
    }
    if (now >= reservation->startTime()){
        lastError_ = "预约已开始，不能取消（未到场将按爽约处理）";
        return false;
    }
    if (!releaseLockIfHeld(*reservation)){
        lastError_ = "释放预约车位失败";
        return false;
    }
    const Reservation rollback = *reservation;
    const bool charged = reservation->status() == ReservationStatus::Confirmed
        && reservation->depositState() == DepositState::Pending
        && !reservation->chargeTransactionId().empty();
    if (charged){
        const auto refundTransaction = gateway_.refund(
            reservation->chargeTransactionId(), reservation->deposit(), now);
        if (!refundTransaction){
            lastError_ = "定金退回失败";
            return false;
        }
        reservation->cancel();
        reservation->setDepositState(DepositState::Refunded);
        DepositPayment receipt{*refundTransaction, reservation->id(), plateNumber,
                               DepositPayment::Kind::Refund, reservation->deposit(),
                               DepositPayment::Status::Succeeded, now};
        if (park_.repository_ != nullptr
            && !park_.repository_->saveReservationPayment(*reservation, receipt)){
            *reservation = rollback;
            releaseLockIfHeld(*reservation);
            lastError_ = "取消写入数据库失败: " + park_.repository_->lastError();
            return false;
        }
        payments_.push_back(std::move(receipt));
    } else{
        reservation->cancel();
        if (park_.repository_ != nullptr
            && !park_.repository_->saveReservationStatus(*reservation)){
            *reservation = rollback;
            releaseLockIfHeld(*reservation);
            lastError_ = "取消写入数据库失败: " + park_.repository_->lastError();
            return false;
        }
    }
    park_.audit("reservation_cancel", plateNumber);
    return true;
}

std::optional<AllocationResult> ReservationService::checkIn(
    const std::string &plateNumber, ParkingRecord::TimePoint now){
    lastError_.clear();
    sweep(now);
    Reservation *reservation = findStatusPtr(plateNumber, ReservationStatus::Confirmed);
    if (reservation == nullptr){
        lastError_ = "该车牌没有待到场的时段预约";
        return std::nullopt;
    }
    if (now < reservation->startTime() - rule_.lockLeadTime
        || now > reservation->graceDeadline()){
        lastError_ = "不在到场时间窗口内（开始前 "
            + std::to_string(rule_.lockLeadTime.count())
            + " 分钟起至宽限期截止）";
        return std::nullopt;
    }
    ParkingSpot *spot = findSpot(reservation->spotId());
    if (spot == nullptr){
        lastError_ = "预约车位不存在";
        return std::nullopt;
    }
    const Vehicle vehicle(reservation->plateNumber(), reservation->vehicleType());
    if (spot->status() == SpotStatus::Available){
        // sweep 未及锁位时兜底。
        if (!spot->reserve(vehicle, reservation->graceDeadline())){
            lastError_ = "锁定预约车位失败";
            return std::nullopt;
        }
        if (park_.repository_ != nullptr
            && !park_.repository_->saveReservation(*spot)){
            spot->release();
            lastError_ = "锁位写入数据库失败: " + park_.repository_->lastError();
            return std::nullopt;
        }
    }
    const std::optional<AllocationProposal> proposal = park_.allocator_.propose(
        vehicle, park_.spots_, spot->identifier());
    if (!proposal
        || spot->status() != SpotStatus::Reserved
        || !spot->parkedVehicle()
        || spot->parkedVehicle()->plateNumber() != plateNumber
        || !spot->occupy(vehicle)){
        lastError_ = "预约车位当前不可用（可能被其他车辆占用）";
        return std::nullopt;
    }
    park_.records_.emplace_back(plateNumber, reservation->spotId(), now);
    const Reservation rollback = *reservation;
    if (!reservation->checkIn()){
        park_.records_.pop_back();
        spot->release();
        spot->reserve(vehicle, reservation->graceDeadline());
        lastError_ = "预约状态不允许到场确认";
        return std::nullopt;
    }
    if (park_.repository_ != nullptr
        && !park_.repository_->saveReservationCheckIn(*reservation,
                                                       park_.records_.back(),
                                                       *spot)){
        park_.records_.pop_back();
        spot->release();
        spot->reserve(vehicle, reservation->graceDeadline());
        *reservation = rollback;
        lastError_ = "到场确认写入数据库失败: " + park_.repository_->lastError();
        return std::nullopt;
    }
    park_.audit("reservation_checkin", plateNumber + "@" + reservation->spotId());
    return park_.toResult(*proposal);
}

void ReservationService::sweep(ParkingRecord::TimePoint now){
    if (!timeutil::isValid(now)){
        return;
    }
    // 爽约结算：超过宽限期仍未到场的预约没收定金并释放短时锁。
    for (Reservation &reservation : reservations_){
        if (reservation.status() != ReservationStatus::Confirmed
            || now <= reservation.graceDeadline()){
            continue;
        }
        releaseLockIfHeld(reservation);
        const Reservation rollback = reservation;
        const auto forfeitTransaction = gateway_.forfeit(
            reservation.chargeTransactionId(), reservation.deposit(), now);
        reservation.markNoShow();
        reservation.setDepositState(DepositState::Forfeited);
        if (park_.repository_ != nullptr){
            bool persisted = false;
            if (forfeitTransaction){
                DepositPayment receipt{*forfeitTransaction, reservation.id(),
                                       reservation.plateNumber(),
                                       DepositPayment::Kind::Forfeit,
                                       reservation.deposit(),
                                       DepositPayment::Status::Succeeded, now};
                persisted = park_.repository_->saveReservationPayment(reservation,
                                                                       receipt);
                if (persisted){
                    payments_.push_back(std::move(receipt));
                }
            } else{
                persisted = park_.repository_->saveReservationStatus(reservation);
            }
            if (!persisted){
                reservation = rollback;
            }
        }
        park_.audit("reservation_no_show",
                    reservation.plateNumber() + "@" + reservation.spotId());
    }
    // 延迟锁位：进入到场窗口的预约把物理车位短时锁定到宽限期截止。
    for (Reservation &reservation : reservations_){
        if (reservation.status() != ReservationStatus::Confirmed
            || now < reservation.startTime() - rule_.lockLeadTime){
            continue;
        }
        ParkingSpot *spot = findSpot(reservation.spotId());
        if (spot == nullptr || spot->status() != SpotStatus::Available){
            continue;
        }
        const Vehicle vehicle(reservation.plateNumber(), reservation.vehicleType());
        if (spot->reserve(vehicle, reservation.graceDeadline())){
            if (park_.repository_ != nullptr
                && !park_.repository_->saveReservation(*spot)){
                spot->release();
            }
        }
    }
}

const std::vector<Reservation> &ReservationService::reservations() const noexcept{
    return reservations_;
}

const std::vector<DepositPayment> &ReservationService::payments() const noexcept{
    return payments_;
}

std::optional<Reservation> ReservationService::findOpen(
    const std::string &plateNumber) const{
    const Reservation *reservation = findOpenPtr(plateNumber);
    if (reservation == nullptr){
        return std::nullopt;
    }
    return *reservation;
}

std::optional<Reservation> ReservationService::findCheckedIn(
    const std::string &plateNumber) const{
    const Reservation *reservation = const_cast<ReservationService *>(this)
                                          ->findStatusPtr(plateNumber,
                                                          ReservationStatus::CheckedIn);
    if (reservation == nullptr){
        return std::nullopt;
    }
    return *reservation;
}

double ReservationService::heldDeposits() const noexcept{
    double total = 0.0;
    for (const Reservation &reservation : reservations_){
        if (reservation.depositState() == DepositState::Pending
            && (reservation.status() == ReservationStatus::Confirmed
                || reservation.status() == ReservationStatus::CheckedIn)){
            total += reservation.deposit();
        }
    }
    return total;
}

double ReservationService::forfeitedDeposits() const noexcept{
    double total = 0.0;
    for (const Reservation &reservation : reservations_){
        if (reservation.status() == ReservationStatus::NoShow){
            total += reservation.deposit();
        }
    }
    return total;
}

double ReservationService::refundedDeposits() const noexcept{
    double total = 0.0;
    for (const Reservation &reservation : reservations_){
        if (reservation.status() == ReservationStatus::Cancelled
            && reservation.depositState() == DepositState::Refunded){
            total += reservation.deposit();
        }
    }
    return total;
}

double ReservationService::appliedDeposits() const noexcept{
    double total = 0.0;
    for (const Reservation &reservation : reservations_){
        if (reservation.status() == ReservationStatus::Completed
            && reservation.depositState() == DepositState::Applied){
            total += reservation.deposit();
        }
    }
    return total;
}

bool ReservationService::hasLockedSpot(const std::string &spotId) const noexcept{
    return std::any_of(
        reservations_.begin(), reservations_.end(),
        [&spotId](const Reservation &reservation){
            return reservation.status() == ReservationStatus::Confirmed
                && reservation.spotId() == spotId;
        });
}

const std::string &ReservationService::lastError() const noexcept{
    return lastError_;
}

void ReservationService::failNextDepositCharge() noexcept{
    gateway_.failNextCharge();
}

ReservationService::ExitSettlement ReservationService::prepareExitSettlement(
    const std::string &plateNumber, double parkingFee,
    ParkingRecord::TimePoint exitTime){
    ExitSettlement settlement;
    if (!std::isfinite(parkingFee) || parkingFee < 0.0){
        return settlement;
    }
    Reservation *reservation = findStatusPtr(plateNumber,
                                             ReservationStatus::CheckedIn);
    if (reservation == nullptr){
        return settlement;
    }
    settlement.reservation = reservation;
    settlement.credit = std::min(reservation->deposit(), parkingFee);
    settlement.receipt = DepositPayment{
        newLedgerId("APL", exitTime), reservation->id(),
        reservation->plateNumber(), DepositPayment::Kind::Apply,
        settlement.credit, DepositPayment::Status::Succeeded, exitTime};
    return settlement;
}

void ReservationService::applyExitSettlement(ExitSettlement &settlement){
    if (settlement.reservation == nullptr){
        return;
    }
    settlement.previousState = *settlement.reservation;
    settlement.reservation->complete();
    settlement.reservation->setDepositState(DepositState::Applied);
    payments_.push_back(settlement.receipt);
}

void ReservationService::rollbackExitSettlement(ExitSettlement &settlement){
    if (settlement.reservation == nullptr){
        return;
    }
    if (settlement.previousState.has_value()){
        *settlement.reservation = *settlement.previousState;
        settlement.previousState.reset();
    }
    const auto receipt = std::find_if(
        payments_.begin(), payments_.end(),
        [&settlement](const DepositPayment &payment){
            return payment.transactionId == settlement.receipt.transactionId;
        });
    if (receipt != payments_.end()){
        payments_.erase(receipt);
    }
}

void ReservationService::restore(ParkingRepository &repository){
    reservations_ = repository.loadReservations();
    if (!repository.lastError().empty()){
        throw std::runtime_error("cannot restore reservations: "
                                + repository.lastError());
    }
    payments_ = repository.loadDepositPayments();
    if (!repository.lastError().empty()){
        throw std::runtime_error("cannot restore deposit payments: "
                                + repository.lastError());
    }
    std::set<std::string> openPlates;
    for (const Reservation &reservation : reservations_){
        const ParkingSpot *spot = findSpot(reservation.spotId());
        if (spot == nullptr){
            throw std::runtime_error("persisted reservation refers to an unknown spot: "
                                    + reservation.spotId());
        }
        if (reservation.isOpen() && !openPlates.insert(reservation.plateNumber()).second){
            throw std::runtime_error("duplicate open persisted reservation: "
                                    + reservation.plateNumber());
        }
        if (reservation.status() == ReservationStatus::Confirmed){
            if (spot->status() == SpotStatus::Reserved && spot->parkedVehicle()
                && spot->parkedVehicle()->plateNumber() != reservation.plateNumber()){
                throw std::runtime_error(
                    "reserved spot belongs to another vehicle for reservation: "
                    + reservation.id());
            }
            // 停机期间进入锁位窗口且车位空闲时，由下一次 sweep 补锁。
        } else if (reservation.status() == ReservationStatus::CheckedIn){
            const std::optional<ParkingRecord> record =
                park_.activeRecord(reservation.plateNumber());
            if (!record || record->spotId() != reservation.spotId()
                || spot->status() != SpotStatus::Occupied || !spot->parkedVehicle()
                || spot->parkedVehicle()->plateNumber() != reservation.plateNumber()){
                throw std::runtime_error(
                    "checked-in reservation does not match its parking record: "
                    + reservation.plateNumber());
            }
        } else if (spot->status() == SpotStatus::Reserved && spot->parkedVehicle()
                   && spot->parkedVehicle()->plateNumber() == reservation.plateNumber()){
            throw std::runtime_error("resolved reservation still holds its spot: "
                                    + reservation.id());
        }
    }
}

Reservation *ReservationService::findOpenPtr(const std::string &plateNumber){
    const auto reservation = std::find_if(
        reservations_.begin(), reservations_.end(),
        [&plateNumber](const Reservation &item){
            return item.isOpen() && item.plateNumber() == plateNumber;
        });
    return reservation == reservations_.end() ? nullptr : &(*reservation);
}

const Reservation *ReservationService::findOpenPtr(
    const std::string &plateNumber) const{
    return const_cast<ReservationService *>(this)->findOpenPtr(plateNumber);
}

Reservation *ReservationService::findStatusPtr(const std::string &plateNumber,
                                               ReservationStatus status){
    const auto reservation = std::find_if(
        reservations_.begin(), reservations_.end(),
        [&plateNumber, status](const Reservation &item){
            return item.status() == status && item.plateNumber() == plateNumber;
        });
    return reservation == reservations_.end() ? nullptr : &(*reservation);
}

bool ReservationService::overlapsOpenReservation(
    const std::string &spotId, ParkingRecord::TimePoint startTime,
    ParkingRecord::TimePoint endTime) const{
    return std::any_of(
        reservations_.begin(), reservations_.end(),
        [&spotId, startTime, endTime](const Reservation &reservation){
            return blocksTimeWindow(reservation.status())
                && reservation.spotId() == spotId
                && reservation.startTime() < endTime
                && reservation.endTime() > startTime;
        });
}

bool ReservationService::releaseLockIfHeld(const Reservation &reservation){
    ParkingSpot *spot = findSpot(reservation.spotId());
    if (spot == nullptr || spot->status() != SpotStatus::Reserved
        || !spot->parkedVehicle()
        || spot->parkedVehicle()->plateNumber() != reservation.plateNumber()){
        return true;
    }
    const Vehicle previousVehicle = *spot->parkedVehicle();
    const std::optional<ParkingSpot::TimePoint> previousExpiry =
        spot->reservationExpiresAt();
    if (!spot->release()){
        return false;
    }
    if (park_.repository_ != nullptr && !park_.repository_->saveSpotState(*spot)){
        if (previousExpiry){
            spot->reserve(previousVehicle, *previousExpiry);
        }
        return false;
    }
    return true;
}

std::string ReservationService::newReservationId(){
    return newLedgerId("R", Reservation::Clock::now());
}

std::string ReservationService::newLedgerId(const char *prefix,
                                            ParkingRecord::TimePoint time){
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            time.time_since_epoch())
            .count();
    std::ostringstream stream;
    stream << prefix << '-' << millis << '-' << (sequence_++);
    return stream.str();
}

ParkingSpot *ReservationService::findSpot(const std::string &spotId){
    const auto spot = std::find_if(
        park_.spots_.begin(), park_.spots_.end(),
        [&spotId](const ParkingSpot &item) { return item.identifier() == spotId; });
    return spot == park_.spots_.end() ? nullptr : &(*spot);
}
} // namespace smartpark
