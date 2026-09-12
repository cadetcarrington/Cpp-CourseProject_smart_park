#include "core/model/Booking.h"
#include <cmath>
#include <stdexcept>
#include <utility>
namespace smartpark{
Booking::Booking(std::string id, std::string plateNumber, std::string spotId,
                 TimePoint createdAt, TimePoint arrivalTime, TimePoint arrivalDeadline,
                 double deposit, BookingStatus status)
    : id_(std::move(id))
    , plateNumber_(std::move(plateNumber))
    , spotId_(std::move(spotId))
    , createdAt_(createdAt)
    , arrivalTime_(arrivalTime)
    , arrivalDeadline_(arrivalDeadline)
    , deposit_(deposit)
    , status_(status){
    if (id_.empty()){
        throw std::invalid_argument("booking id cannot be empty");
    }
    if (plateNumber_.empty()){
        throw std::invalid_argument("booking plate number cannot be empty");
    }
    if (spotId_.empty()){
        throw std::invalid_argument("booking spot id cannot be empty");
    }
    if (!std::isfinite(deposit_) || deposit_ < 0.0){
        throw std::invalid_argument("booking deposit must be a non-negative number");
    }
    if (arrivalTime_ < createdAt_){
        throw std::invalid_argument("booking arrival time must not precede its creation time");
    }
    if (arrivalDeadline_ < arrivalTime_){
        throw std::invalid_argument("booking arrival deadline must not precede its arrival time");
    }
}
const std::string &Booking::id() const noexcept{
    return id_;
}
const std::string &Booking::plateNumber() const noexcept{
    return plateNumber_;
}
const std::string &Booking::spotId() const noexcept{
    return spotId_;
}
Booking::TimePoint Booking::createdAt() const noexcept{
    return createdAt_;
}
Booking::TimePoint Booking::arrivalTime() const noexcept{
    return arrivalTime_;
}
Booking::TimePoint Booking::arrivalDeadline() const noexcept{
    return arrivalDeadline_;
}
double Booking::deposit() const noexcept{
    return deposit_;
}
BookingStatus Booking::status() const noexcept{
    return status_;
}
bool Booking::isActive() const noexcept{
    return status_ == BookingStatus::Booked;
}
bool Booking::checkIn() noexcept{
    if (status_ != BookingStatus::Booked){
        return false;
    }
    status_ = BookingStatus::CheckedIn;
    return true;
}
bool Booking::markNoShow() noexcept{
    if (status_ != BookingStatus::Booked){
        return false;
    }
    status_ = BookingStatus::NoShow;
    return true;
}
bool Booking::cancel() noexcept{
    if (status_ != BookingStatus::Booked){
        return false;
    }
    status_ = BookingStatus::Cancelled;
    return true;
}
} // namespace smartpark