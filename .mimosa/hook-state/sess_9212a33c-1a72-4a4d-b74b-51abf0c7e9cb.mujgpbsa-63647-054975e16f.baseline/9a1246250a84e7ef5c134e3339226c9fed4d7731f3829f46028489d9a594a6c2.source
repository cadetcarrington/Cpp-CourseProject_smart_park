#pragma once
#include <string>
namespace smartpark{
    enum class VehicleType{
        Car,
        Motorcycle,
        Truck,
        Electric
    };
    class Vehicle{
        public:
            Vehicle(std::string plateNumber, VehicleType type);
            const std::string &plateNumber() const noexcept;
            VehicleType type() const noexcept;
        private:
            std::string plateNumber_;
            VehicleType type_;
    };
} // namespace smartpark
