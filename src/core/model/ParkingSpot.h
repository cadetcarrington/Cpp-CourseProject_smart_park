#pragma once

#include "core/model/Geometry.h"
#include "core/model/Vehicle.h"

#include <chrono>
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

enum class SpotType
{
    Normal,
    Accessible,
    Charging,
    Vip
};

inline const char *toString(SpotType type) noexcept
{
    switch (type) {
    case SpotType::Accessible:
        return "accessible";
    case SpotType::Charging:
        return "charging";
    case SpotType::Vip:
        return "vip";
    case SpotType::Normal:
    default:
        return "normal";
    }
}

inline std::optional<SpotType> spotTypeFromString(const std::string &text)
{
    if (text == "normal") {
        return SpotType::Normal;
    }
    if (text == "accessible") {
        return SpotType::Accessible;
    }
    if (text == "charging") {
        return SpotType::Charging;
    }
    if (text == "vip") {
        return SpotType::Vip;
    }
    return std::nullopt;
}

class ParkingSpot
{
public:
    using Clock = std::chrono::system_clock;
    using TimePoint = Clock::time_point;

    struct Geometry
    {
        std::string zone;
        int row{-1};
        int column{-1};
        Rectangle bounds;
        Point accessPoint;
        SpotType type{SpotType::Normal};
    };

    explicit ParkingSpot(std::string identifier);
    ParkingSpot(std::string identifier, Geometry geometry);

    const std::string &identifier() const noexcept;
    SpotStatus status() const noexcept;
    SpotType type() const noexcept;
    bool isAvailable() const noexcept;
    const std::optional<Vehicle> &parkedVehicle() const noexcept;
    const Geometry &geometry() const noexcept;
    const Rectangle &bounds() const noexcept;
    const Point &accessPoint() const noexcept;
    const std::string &zone() const noexcept;
    int row() const noexcept;
    int column() const noexcept;
    const std::optional<TimePoint> &reservationExpiresAt() const noexcept;

    bool occupy(const Vehicle &vehicle);
    bool reserve(const Vehicle &vehicle, TimePoint expiresAt);
    bool expireReservation(TimePoint now);
    bool release() noexcept;

private:
    void clearOccupancy() noexcept;

    std::string identifier_;
    Geometry geometry_;
    SpotStatus status_{SpotStatus::Available};
    std::optional<Vehicle> parkedVehicle_;
    std::optional<TimePoint> reservationExpiresAt_;
};

} // namespace smartpark
