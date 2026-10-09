#pragma once
#include "core/model/Vehicle.h"
#include <chrono>
#include <optional>
#include <string>
namespace smartpark{
class ParkingRecord{
    public:
        using Clock = std::chrono::system_clock;
        using TimePoint = Clock::time_point;
        ParkingRecord(std::string plateNumber, std::string spotId, TimePoint entryTime,
                      VehicleType vehicleType = VehicleType::Car);
        const std::string &plateNumber() const noexcept;
        const std::string &spotId() const noexcept;
        TimePoint entryTime() const noexcept;
        const std::optional<TimePoint> &exitTime() const noexcept;
        VehicleType vehicleType() const noexcept;
        bool isClosed() const noexcept;
        std::chrono::seconds duration() const noexcept;
        double fee() const noexcept;
        bool close(TimePoint exitTime, double recordFee = 0.0);
    private:
        std::string plateNumber_;
        std::string spotId_;
        TimePoint entryTime_;
        std::optional<TimePoint> exitTime_;
        VehicleType vehicleType_{VehicleType::Car};
        double fee_{0.0};
    };
} // namespace smartpark
