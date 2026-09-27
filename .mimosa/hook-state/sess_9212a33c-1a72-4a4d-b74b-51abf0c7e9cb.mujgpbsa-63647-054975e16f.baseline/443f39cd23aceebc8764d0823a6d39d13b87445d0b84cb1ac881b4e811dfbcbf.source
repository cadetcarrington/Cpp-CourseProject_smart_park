#include "core/service/ParkingInsightEngine.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace smartpark{
namespace{

using TimePoint = ParkingRecord::TimePoint;

constexpr std::chrono::minutes kMinute60{60};
constexpr std::chrono::minutes kMinute180{180};
constexpr std::chrono::minutes kMinute10{10};
constexpr std::chrono::hours kLongStayThreshold{8};

bool withinRecent(TimePoint time, TimePoint now, std::chrono::minutes window){
    return time <= now && time > now - window;
}

int countArrivals(const std::vector<ParkingRecord> &records, TimePoint now,
                  std::chrono::minutes window){
    int count = 0;
    for (const ParkingRecord &record : records){
        if (withinRecent(record.entryTime(), now, window)){
            ++count;
        }
    }
    return count;
}

int countDepartures(const std::vector<ParkingRecord> &records, TimePoint now,
                    std::chrono::minutes window){
    int count = 0;
    for (const ParkingRecord &record : records){
        const std::optional<TimePoint> exit = record.exitTime();
        if (exit && withinRecent(*exit, now, window)){
            ++count;
        }
    }
    return count;
}

int upcomingBookings(const std::vector<Booking> &bookings, TimePoint now,
                     std::chrono::minutes window){
    const TimePoint end = now + window;
    int count = 0;
    for (const Booking &booking : bookings){
        if (booking.status() != BookingStatus::Booked){
            continue;
        }
        if (booking.arrivalTime() > now && booking.arrivalTime() <= end){
            ++count;
        }
    }
    return count;
}

double minutesBetween(TimePoint from, TimePoint to){
    return std::chrono::duration_cast<std::chrono::duration<double, std::ratio<60>>>(
               to - from)
        .count();
}

const OccupancyForecast *forecastAt(const std::vector<OccupancyForecast> &forecasts,
                                    int minutes){
    const auto found = std::find_if(
        forecasts.begin(), forecasts.end(),
        [minutes](const OccupancyForecast &forecast){ return forecast.minutes == minutes; });
    return found == forecasts.end() ? nullptr : &(*found);
}

bool isNonElectricCharging(const ParkingSpot &spot, const std::vector<ParkingRecord> &records,
                           TimePoint now){
    if (spot.type() != SpotType::Charging || spot.status() != SpotStatus::Occupied){
        return false;
    }
    const std::optional<Vehicle> vehicle = spot.parkedVehicle();
    if (!vehicle || vehicle->type() == VehicleType::Electric){
        return false;
    }
    const auto record = std::find_if(
        records.begin(), records.end(), [&spot](const ParkingRecord &item){
            return item.spotId() == spot.identifier() && !item.isClosed();
        });
    if (record == records.end()){
        return false;
    }
    return now - record->entryTime() > kMinute10;
}

} // namespace

ParkingInsights ParkingInsightEngine::analyze(
    const std::vector<ParkingSpot> &spots,
    const std::vector<ParkingRecord> &records,
    const std::vector<Booking> &bookings,
    TimePoint now){

    ParkingInsights result;

    int available = 0;
    int occupied = 0;
    int reserved = 0;
    int disabled = 0;
    std::map<std::string, ZoneInsight> zoneMap;

    for (const ParkingSpot &spot : spots){
        switch (spot.status()){
        case SpotStatus::Occupied:
            ++occupied;
            break;
        case SpotStatus::Reserved:
            ++reserved;
            break;
        case SpotStatus::Disabled:
            ++disabled;
            break;
        case SpotStatus::Available:
        default:
            ++available;
            break;
        }

        ZoneInsight &zone = zoneMap[spot.zone()];
        zone.zone = spot.zone();
        ++zone.total;
        switch (spot.status()){
        case SpotStatus::Occupied:
            ++zone.occupied;
            break;
        case SpotStatus::Reserved:
            ++zone.reserved;
            break;
        case SpotStatus::Disabled:
            ++zone.disabled;
            break;
        case SpotStatus::Available:
        default:
            ++zone.available;
            break;
        }
    }

    const int totalSpots = static_cast<int>(spots.size());
    result.effectiveCapacity = totalSpots - disabled;
    result.currentOccupied = occupied;
    result.currentReserved = reserved;
    result.currentDisabled = disabled;
    result.currentRate = result.effectiveCapacity > 0
        ? occupied * 100.0 / static_cast<double>(result.effectiveCapacity)
        : 0.0;

    result.zones.reserve(zoneMap.size());
    for (const auto &entry : zoneMap){
        ZoneInsight zone = entry.second;
        zone.load = static_cast<double>(zone.occupied)
                    + 0.7 * static_cast<double>(zone.reserved)
                    + 0.5 * static_cast<double>(zone.disabled);
        zone.pressure = zone.total > 0 ? zone.load / static_cast<double>(zone.total) : 0.0;
        result.zones.push_back(zone);
    }
    std::sort(result.zones.begin(), result.zones.end(),
              [](const ZoneInsight &left, const ZoneInsight &right){
                  if (std::abs(left.pressure - right.pressure) > 1e-9){
                      return left.pressure > right.pressure;
                  }
                  return left.zone < right.zone;
              });

    result.arrivals60 = countArrivals(records, now, kMinute60);
    result.arrivals180 = countArrivals(records, now, kMinute180);
    result.departures60 = countDepartures(records, now, kMinute60);
    result.departures180 = countDepartures(records, now, kMinute180);

    const double arrivalRate60 = static_cast<double>(result.arrivals60) / 1.0;   // 每小时
    const double arrivalRate180 = static_cast<double>(result.arrivals180) / 3.0; // 每小时
    const double departureRate60 = static_cast<double>(result.departures60) / 1.0;
    const double departureRate180 = static_cast<double>(result.departures180) / 3.0;

    const double smoothedArrivalRate = 0.65 * arrivalRate60 + 0.35 * arrivalRate180;
    const double smoothedDepartureRate = 0.65 * departureRate60 + 0.35 * departureRate180;

    const int sampleEvents = result.arrivals180 + result.departures180;
    // 样本越多，启发式置信度越高；无样本时为 0，上限 1。
    const double confidence = std::min(1.0, static_cast<double>(sampleEvents) / 10.0);
    result.confidence = confidence;

    for (const int minutes : {30, 60, 120}){
        OccupancyForecast forecast;
        forecast.minutes = minutes;
        forecast.upcomingBookings = upcomingBookings(bookings, now, std::chrono::minutes(minutes));
        const double hours = minutes / 60.0;
        double predicted = static_cast<double>(occupied)
                           + (smoothedArrivalRate - smoothedDepartureRate) * hours
                           + static_cast<double>(forecast.upcomingBookings);
        predicted = std::max(0.0, std::min(predicted,
                                           static_cast<double>(result.effectiveCapacity)));
        forecast.predictedOccupied = predicted;
        forecast.predictedRate = result.effectiveCapacity > 0
            ? predicted * 100.0 / static_cast<double>(result.effectiveCapacity)
            : 0.0;
        forecast.confidence = confidence;
        forecast.sampleEvents = sampleEvents;
        result.forecasts.push_back(forecast);
    }

    std::vector<RiskAlert> alerts;
    auto addAlert = [&alerts](RiskAlert::Severity severity, const std::string &title,
                              const std::string &reason, const std::string &action){
        RiskAlert alert;
        alert.severity = severity;
        alert.title = title;
        alert.reason = reason;
        alert.action = action;
        alerts.push_back(alert);
    };

    if (const OccupancyForecast *forecast = forecastAt(result.forecasts, 60)){
        if (forecast->predictedRate >= 95.0){
            addAlert(RiskAlert::Severity::Critical, "60 分钟预测占用率过高",
                     "预测占用率达到 " + std::to_string(static_cast<int>(forecast->predictedRate))
                         + "%，接近容量上限。",
                     "建议暂停新预约，引导车辆前往其他区域或开启排队。");
        } else if (forecast->predictedRate >= 85.0){
            addAlert(RiskAlert::Severity::Warning, "60 分钟预测占用率偏高",
                     "预测占用率达到 " + std::to_string(static_cast<int>(forecast->predictedRate))
                         + "%。",
                     "建议控制入场节奏并提示现场管理员关注分区压力。");
        }
    }

    int zoneAlertCount = 0;
    for (const ZoneInsight &zone : result.zones){
        if (zone.pressure >= 0.90){
            addAlert(RiskAlert::Severity::Critical, "分区压力达到临界",
                     "分区 " + zone.zone + " 压力为 "
                         + std::to_string(static_cast<int>(zone.pressure * 100.0)) + "%。",
                     "建议立即把新入场车辆引导到压力更低的分区。");
            ++zoneAlertCount;
        } else if (zone.pressure >= 0.80){
            addAlert(RiskAlert::Severity::Warning, "分区压力偏高",
                     "分区 " + zone.zone + " 压力为 "
                         + std::to_string(static_cast<int>(zone.pressure * 100.0)) + "%。",
                     "建议优先引导新入场车辆到压力更低的分区。");
            ++zoneAlertCount;
        }
        if (zoneAlertCount >= 3){
            break;
        }
    }

    if (result.effectiveCapacity > 0
        && disabled >= static_cast<int>(result.effectiveCapacity * 0.10)){
        addAlert(RiskAlert::Severity::Warning, "停用车位占比较高",
                 "停用车位 " + std::to_string(disabled) + " 个，达到有效容量的 10% 及以上。",
                 "建议检查停用车位原因并尽快恢复可用车位。");
    }

    const int upcoming30 = upcomingBookings(bookings, now, std::chrono::minutes(30));
    const double expectedDepartures30 = smoothedDepartureRate * 0.5;
    if (upcoming30 > static_cast<double>(available) + expectedDepartures30){
        addAlert(RiskAlert::Severity::Warning, "未来 30 分钟预约需求超载",
                 "未来 30 分钟有 " + std::to_string(upcoming30)
                     + " 个预约到场，当前空闲 " + std::to_string(available)
                     + " 个、预计离场 " + std::to_string(static_cast<int>(expectedDepartures30))
                     + " 个。",
                 "建议暂停或错峰新增预约，避免到场后无车位可用。");
    }

    int longStayCount = 0;
    for (const ParkingRecord &record : records){
        if (!record.isClosed() && now - record.entryTime() > kLongStayThreshold){
            ++longStayCount;
        }
    }
    if (longStayCount > 0){
        addAlert(RiskAlert::Severity::Warning, "存在长时间停放车辆",
                 "当前有 " + std::to_string(longStayCount) + " 辆在场车辆停放超过 8 小时。",
                 "建议联系车主确认离场安排，或按规则启动长停提醒。");
    }

    int misusedChargingCount = 0;
    for (const ParkingSpot &spot : spots){
        if (isNonElectricCharging(spot, records, now)){
            ++misusedChargingCount;
        }
    }
    if (misusedChargingCount > 0){
        addAlert(RiskAlert::Severity::Warning, "充电车位被非电动车占用",
                 std::to_string(misusedChargingCount)
                     + " 个充电车位被非电动车占用超过 10 分钟。",
                 "建议提示相关车辆挪位，把充电车位留给电动车。");
    }

    const bool hasRisk = std::any_of(alerts.begin(), alerts.end(), [](const RiskAlert &alert){
        return alert.severity != RiskAlert::Severity::Info;
    });
    if (!hasRisk){
        addAlert(RiskAlert::Severity::Info, "运行平稳",
                 "当前未发现需要人工处理的运营风险。",
                 "保持现有入场节奏并持续监控分区压力。");
    }
    result.alerts = std::move(alerts);

    return result;
}

} // namespace smartpark
