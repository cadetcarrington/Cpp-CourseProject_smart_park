#include "core/model/Reservation.h"
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>
namespace smartpark{
namespace{
bool validVehicleType(VehicleType type) noexcept{
    return type >= VehicleType::Car && type <= VehicleType::Electric;
}
} // namespace
bool ExpectedRoute::empty() const noexcept{
    return entryRoute.points.empty() && exitRoute.points.empty();
}
std::string ExpectedRoute::encode() const{
    if (empty()){
        return {};
    }
    std::ostringstream stream;
    stream.precision(std::numeric_limits<double>::max_digits10);
    stream << "v1 " << entranceIndex << ' ' << exitIndex << '\n'
           << entryRoute.distance << ' ' << entryRoute.cost << ' '
           << entryRoute.turnCount << '\n'
           << exitRoute.distance << ' ' << exitRoute.cost << ' '
           << exitRoute.turnCount << '\n';
    stream << entryRoute.points.size();
    for (const Point &point : entryRoute.points){
        stream << ' ' << point.x << ' ' << point.y;
    }
    stream << '\n' << exitRoute.points.size();
    for (const Point &point : exitRoute.points){
        stream << ' ' << point.x << ' ' << point.y;
    }
    return stream.str();
}
std::optional<ExpectedRoute> ExpectedRoute::decode(const std::string &text){
    if (text.empty()){
        return ExpectedRoute{};
    }
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    std::string version;
    std::size_t entranceIndex = 0;
    std::size_t exitIndex = 0;
    if (!(stream >> version) || version != "v1"
        || !(stream >> entranceIndex >> exitIndex)){
        return std::nullopt;
    }
    ExpectedRoute route;
    route.entranceIndex = entranceIndex;
    route.exitIndex = exitIndex;
    if (!(stream >> route.entryRoute.distance >> route.entryRoute.cost
          >> route.entryRoute.turnCount)
        || !(stream >> route.exitRoute.distance >> route.exitRoute.cost
             >> route.exitRoute.turnCount)){
        return std::nullopt;
    }
    std::size_t entryCount = 0;
    if (!(stream >> entryCount) || entryCount > 100000){
        return std::nullopt;
    }
    route.entryRoute.points.resize(entryCount);
    for (Point &point : route.entryRoute.points){
        if (!(stream >> point.x >> point.y)){
            return std::nullopt;
        }
    }
    std::size_t exitCount = 0;
    if (!(stream >> exitCount) || exitCount > 100000){
        return std::nullopt;
    }
    route.exitRoute.points.resize(exitCount);
    for (Point &point : route.exitRoute.points){
        if (!(stream >> point.x >> point.y)){
            return std::nullopt;
        }
    }
    return route;
}
Reservation::Reservation(std::string id, std::string plateNumber,
                         VehicleType vehicleType, std::string spotId,
                         TimePoint createdAt, TimePoint startTime,
                         TimePoint endTime, TimePoint graceDeadline,
                         double deposit, std::string chargeTransactionId,
                         ReservationStatus status, DepositState depositState,
                         ExpectedRoute expectedRoute,
                         bool accessible)
    : id_(std::move(id))
    , plateNumber_(std::move(plateNumber))
    , vehicleType_(vehicleType)
    , spotId_(std::move(spotId))
    , createdAt_(createdAt)
    , startTime_(startTime)
    , endTime_(endTime)
    , graceDeadline_(graceDeadline)
    , deposit_(deposit)
    , chargeTransactionId_(std::move(chargeTransactionId))
    , status_(status)
    , depositState_(depositState)
    , expectedRoute_(std::move(expectedRoute))
    , accessible_(accessible){
    if (id_.empty()){
        throw std::invalid_argument("reservation id cannot be empty");
    }
    if (plateNumber_.empty()){
        throw std::invalid_argument("reservation plate number cannot be empty");
    }
    if (!validVehicleType(vehicleType_)){
        throw std::invalid_argument("reservation vehicle type is invalid");
    }
    if (spotId_.empty()){
        throw std::invalid_argument("reservation spot id cannot be empty");
    }
    if (!std::isfinite(deposit_) || deposit_ < 0.0){
        throw std::invalid_argument("reservation deposit must be a non-negative number");
    }
    if (startTime_ < createdAt_){
        throw std::invalid_argument("reservation start time must not precede its creation time");
    }
    if (endTime_ < startTime_){
        throw std::invalid_argument("reservation end time must not precede its start time");
    }
    if (graceDeadline_ < startTime_){
        throw std::invalid_argument("reservation grace deadline must not precede its start time");
    }
}
const std::string &Reservation::id() const noexcept{
    return id_;
}
const std::string &Reservation::plateNumber() const noexcept{
    return plateNumber_;
}
VehicleType Reservation::vehicleType() const noexcept{
    return vehicleType_;
}
const std::string &Reservation::spotId() const noexcept{
    return spotId_;
}
Reservation::TimePoint Reservation::createdAt() const noexcept{
    return createdAt_;
}
Reservation::TimePoint Reservation::startTime() const noexcept{
    return startTime_;
}
Reservation::TimePoint Reservation::endTime() const noexcept{
    return endTime_;
}
Reservation::TimePoint Reservation::graceDeadline() const noexcept{
    return graceDeadline_;
}
double Reservation::deposit() const noexcept{
    return deposit_;
}
const std::string &Reservation::chargeTransactionId() const noexcept{
    return chargeTransactionId_;
}
ReservationStatus Reservation::status() const noexcept{
    return status_;
}
DepositState Reservation::depositState() const noexcept{
    return depositState_;
}
const ExpectedRoute &Reservation::expectedRoute() const noexcept{
    return expectedRoute_;
}
bool Reservation::isAccessible() const noexcept{
    return accessible_;
}
bool Reservation::isOpen() const noexcept{
    return status_ == ReservationStatus::PendingPayment
        || status_ == ReservationStatus::Confirmed
        || status_ == ReservationStatus::CheckedIn;
}
bool Reservation::checkIn() noexcept{
    if (status_ != ReservationStatus::Confirmed){
        return false;
    }
    status_ = ReservationStatus::CheckedIn;
    return true;
}
bool Reservation::complete() noexcept{
    if (status_ != ReservationStatus::CheckedIn){
        return false;
    }
    status_ = ReservationStatus::Completed;
    return true;
}
bool Reservation::cancel() noexcept{
    if (status_ != ReservationStatus::Confirmed
        && status_ != ReservationStatus::PendingPayment){
        return false;
    }
    status_ = ReservationStatus::Cancelled;
    return true;
}
bool Reservation::markNoShow() noexcept{
    if (status_ != ReservationStatus::Confirmed){
        return false;
    }
    status_ = ReservationStatus::NoShow;
    return true;
}
bool Reservation::expire() noexcept{
    if (status_ != ReservationStatus::PendingPayment){
        return false;
    }
    status_ = ReservationStatus::Expired;
    return true;
}
void Reservation::setDepositState(DepositState state) noexcept{
    depositState_ = state;
}
} // namespace smartpark
