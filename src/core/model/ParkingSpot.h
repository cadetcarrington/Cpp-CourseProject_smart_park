#pragma once

#include "core/model/Vehicle.h"

#include <string>
#include <optional>

#include "core/model/Geometry.h"

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
    struct Geometry
    {
        std::string zone;
        int row{-1};
        int column{-1};
        Rectangle bounds;
        Point accessPoint;
    };

    explicit ParkingSpot(std::string identifier);
    ParkingSpot(std::string identifier, Geometry geometry);

    const std::string &identifier() const noexcept;
    SpotStatus status() const noexcept;
    bool isAvailable() const noexcept;
    const std::optional<Vehicle> &parkedVehicle() const noexcept;
    const Geometry &geometry() const noexcept;
    const Rectangle &bounds() const noexcept;
    const Point &accessPoint() const noexcept;
    const std::string &zone() const noexcept;
    int row() const noexcept;
    int column() const noexcept;

    bool occupy(const Vehicle &vehicle);
    bool release() noexcept;

private:
    std::string identifier_;
    Geometry geometry_;
    SpotStatus status_{SpotStatus::Available};
    std::optional<Vehicle> parkedVehicle_;
};

} // namespace smartpark
