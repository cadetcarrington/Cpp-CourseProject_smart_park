#pragma once

#include "core/model/Vehicle.h"

#include <optional>
#include <string>

namespace smartpark {

enum class SpotStatus
{
    Available,
    Occupied,
    Reserved,
    Disabled
};

class ParkingSpot
{
public:
    explicit ParkingSpot(std::string identifier);

    const std::string &identifier() const noexcept;
    SpotStatus status() const noexcept;
    bool isAvailable() const noexcept;
    const std::optional<Vehicle> &parkedVehicle() const noexcept;

    bool occupy(const Vehicle &vehicle);
    bool release() noexcept;

private:
    std::string identifier_;
    SpotStatus status_{SpotStatus::Available};
    std::optional<Vehicle> parkedVehicle_;
};

} // namespace smartpark
