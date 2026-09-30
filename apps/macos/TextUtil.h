#pragma once

#include "core/model/Booking.h"
#include "core/model/ParkingRecord.h"
#include "core/model/ParkingSpot.h"
#include "core/model/Vehicle.h"

#include <ctime>
#include <string>

// AppKit 版共享的文本映射（与 Qt 版 MainWindow 中的同类函数保持一致）。
namespace smartpark_ui{

inline const char *vehicleTypeText(smartpark::VehicleType type){
    switch (type){
    case smartpark::VehicleType::Motorcycle:
        return "摩托车";
    case smartpark::VehicleType::Truck:
        return "卡车";
    case smartpark::VehicleType::Electric:
        return "电动车";
    case smartpark::VehicleType::Car:
    default:
        return "轿车";
    }
}

inline const char *spotTypeText(smartpark::SpotType type){
    switch (type){
    case smartpark::SpotType::Accessible:
        return "无障碍";
    case smartpark::SpotType::Charging:
        return "充电";
    case smartpark::SpotType::Vip:
        return "VIP";
    case smartpark::SpotType::Normal:
    default:
        return "普通";
    }
}

inline const char *statusText(smartpark::SpotStatus status){
    switch (status){
    case smartpark::SpotStatus::Reserved:
        return "预订";
    case smartpark::SpotStatus::Occupied:
        return "占用";
    case smartpark::SpotStatus::Disabled:
        return "停用";
    case smartpark::SpotStatus::Available:
    default:
        return "空闲";
    }
}

inline const char *bookingStatusText(smartpark::BookingStatus status){
    switch (status){
    case smartpark::BookingStatus::CheckedIn:
        return "已到场";
    case smartpark::BookingStatus::NoShow:
        return "爽约";
    case smartpark::BookingStatus::Cancelled:
        return "已取消";
    case smartpark::BookingStatus::Booked:
    default:
        return "已预约";
    }
}

inline std::string formatTime(smartpark::ParkingRecord::TimePoint time){
    std::time_t epoch = smartpark::ParkingRecord::Clock::to_time_t(time);
    std::tm *parts = std::localtime(&epoch);
    if (parts == nullptr){
        return "invalid-time";
    }
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", parts);
    return std::string(buffer);
}

inline int vehicleTypeIndex(smartpark::VehicleType type){
    switch (type){
    case smartpark::VehicleType::Motorcycle:
        return 1;
    case smartpark::VehicleType::Truck:
        return 2;
    case smartpark::VehicleType::Electric:
        return 3;
    case smartpark::VehicleType::Car:
    default:
        return 0;
    }
}

inline smartpark::VehicleType vehicleTypeFromIndex(int index){
    switch (index){
    case 1:
        return smartpark::VehicleType::Motorcycle;
    case 2:
        return smartpark::VehicleType::Truck;
    case 3:
        return smartpark::VehicleType::Electric;
    default:
        return smartpark::VehicleType::Car;
    }
}

} // namespace smartpark_ui
