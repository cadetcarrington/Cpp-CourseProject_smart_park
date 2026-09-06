#include "core/model/Vehicle.h"

#include <stdexcept>
#include <utility>

namespace smartpark {

Vehicle::Vehicle(std::string plateNumber, VehicleType type)
    : plateNumber_(std::move(plateNumber))
    , type_(type)
{
    if (plateNumber_.empty()) {
        throw std::invalid_argument("plate number cannot be empty");
    }
}

const std::string &Vehicle::plateNumber() const noexcept
{
    return plateNumber_;
}

VehicleType Vehicle::type() const noexcept
{
    return type_;
}

} // namespace smartpark
