#include "core/model/ParkingSpot.h"

#include <stdexcept>
#include <utility>

namespace smartpark {

ParkingSpot::ParkingSpot(std::string identifier)
    : identifier_(std::move(identifier))
{
    if (identifier_.empty()) {
        throw std::invalid_argument("parking spot identifier cannot be empty");
    }
}

const std::string &ParkingSpot::identifier() const noexcept
{
    return identifier_;
}

SpotStatus ParkingSpot::status() const noexcept
{
    return status_;
}

bool ParkingSpot::isAvailable() const noexcept
{
    return status_ == SpotStatus::Available;
}

const std::optional<Vehicle> &ParkingSpot::parkedVehicle() const noexcept
{
    return parkedVehicle_;
}

bool ParkingSpot::occupy(const Vehicle &vehicle)
{
    if (!isAvailable()) {
        return false;
    }

    parkedVehicle_ = vehicle;
    status_ = SpotStatus::Occupied;
    return true;
}

bool ParkingSpot::release() noexcept
{
    if (status_ != SpotStatus::Occupied) {
        return false;
    }

    parkedVehicle_.reset();
    status_ = SpotStatus::Available;
    return true;
}

} // namespace smartpark
