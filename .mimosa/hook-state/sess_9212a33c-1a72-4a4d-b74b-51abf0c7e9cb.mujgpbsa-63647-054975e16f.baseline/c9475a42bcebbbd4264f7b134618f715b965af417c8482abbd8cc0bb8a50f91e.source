#include "core/service/AnalyticsEngine.h"
#include "core/service/ReservationService.h"
#include "core/util/TimeUtil.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace smartpark{
namespace{
constexpr int kSampleStepHours = 1;

std::string formatRate(double rate){
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1) << rate * 100.0 << "%";
    return stream.str();
}
std::string formatMoney(double value){
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << value;
    return stream.str();
}
} // namespace

const char *AnalysisReport::categoryText(AnalysisFinding::Category category){
    switch (category){
    case AnalysisFinding::Category::Forecast:
        return "预测";
    case AnalysisFinding::Category::Peak:
        return "高峰";
    case AnalysisFinding::Category::Revenue:
        return "收入";
    case AnalysisFinding::Category::Zone:
        return "分区";
    case AnalysisFinding::Category::Booking:
        return "预约";
    case AnalysisFinding::Category::DataQuality:
        return "数据";
    }
    return "其他";
}

AnalyticsEngine::AnalyticsEngine(const ParkingService &service)
    : service_(&service){
}

int AnalyticsEngine::hourOfDay(ParkingRecord::TimePoint time){
    const std::time_t epoch = ParkingRecord::Clock::to_time_t(time);
    std::tm parts{};
    gmtime_r(&epoch, &parts);
    return parts.tm_hour;
}

OperationalSnapshot AnalyticsEngine::snapshot(ParkingRecord::TimePoint now) const{
    OperationalSnapshot snapshot;
    const auto &spots = service_->spots();
    snapshot.capacity = static_cast<int>(spots.size());
    snapshot.occupied = service_->occupiedSpots();
    snapshot.occupancyRate = snapshot.capacity > 0
        ? static_cast<double>(snapshot.occupied) / snapshot.capacity
        : 0.0;
    snapshot.activeRecords = static_cast<int>(std::count_if(
        service_->records().begin(), service_->records().end(),
        [](const ParkingRecord &record) { return !record.isClosed(); }));
    snapshot.closedRecords = static_cast<int>(service_->records().size())
        - snapshot.activeRecords;
    snapshot.totalRevenue = service_->totalRevenue();

    double durationTotal = 0.0;
    int durationSamples = 0;
    std::map<int, int> entryHours;
    for (int offset = kTrendWindowHours; offset >= 0; offset -= kSampleStepHours){
        const auto t = now - std::chrono::hours(offset);
        int occupiedAt = 0;
        for (const ParkingRecord &record : service_->records()){
            if (record.entryTime() > t){
                continue;
            }
            if (record.isClosed() && record.exitTime() <= t){
                continue;
            }
            ++occupiedAt;
        }
        const double rate = snapshot.capacity > 0
            ? std::min(1.0, static_cast<double>(occupiedAt) / snapshot.capacity)
            : 0.0;
        snapshot.occupancySeries.push_back({t, rate});
    }

    for (const ParkingRecord &record : service_->records()){
        if (record.isClosed() && record.exitTime()){
            const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                *record.exitTime() - record.entryTime());
            durationTotal += std::chrono::duration_cast<std::chrono::duration<double>>(
                seconds).count() / 3600.0;
            ++durationSamples;
        }
        ++entryHours[hourOfDay(record.entryTime())];
    }
    snapshot.averageDurationHours = durationSamples > 0
        ? durationTotal / durationSamples
        : 0.0;
    int peakCount = 0;
    for (const auto &entry : entryHours){
        if (entry.second > peakCount){
            peakCount = entry.second;
            snapshot.peakEntryHour = entry.first;
        }
    }

    std::map<std::string, ZoneLoadStat> zoneMap;
    for (const ParkingSpot &spot : spots){
        auto &stat = zoneMap[spot.zone()];
        stat.zone = spot.zone();
        stat.total += 1;
        if (spot.status() == SpotStatus::Occupied){
            stat.occupied += 1;
        }
    }
    for (auto &entry : zoneMap){
        entry.second.load = entry.second.total > 0
            ? static_cast<double>(entry.second.occupied) / entry.second.total
            : 0.0;
        snapshot.zones.push_back(entry.second);
    }
    std::sort(snapshot.zones.begin(), snapshot.zones.end(),
              [](const ZoneLoadStat &a, const ZoneLoadStat &b){
                  return a.zone < b.zone;
              });

    for (const Booking &booking : service_->bookings()){
        if (booking.status() == BookingStatus::Booked){
            ++snapshot.openBookings;
        } else if (booking.status() == BookingStatus::NoShow){
            ++snapshot.noShowBookings;
        }
    }
    snapshot.reservationForfeited = service_->reservations().forfeitedDeposits();
    return snapshot;
}

LinearTrend AnalyticsEngine::fitLinear(const std::vector<OccupancySample> &samples){
    LinearTrend trend;
    trend.samples = static_cast<int>(samples.size());
    if (samples.size() < 3){
        return trend;
    }
    const double n = static_cast<double>(samples.size());
    double meanX = 0.0;
    double meanY = 0.0;
    for (std::size_t index = 0; index < samples.size(); ++index){
        meanX += static_cast<double>(index);
        meanY += samples[index].occupancyRate;
    }
    meanX /= n;
    meanY /= n;
    double covariance = 0.0;
    double variance = 0.0;
    for (std::size_t index = 0; index < samples.size(); ++index){
        const double dx = static_cast<double>(index) - meanX;
        covariance += dx * (samples[index].occupancyRate - meanY);
        variance += dx * dx;
    }
    if (variance <= 0.0){
        return trend;
    }
    trend.slope = covariance / variance;
    trend.intercept = meanY - trend.slope * meanX;
    double residualSum = 0.0;
    double totalSum = 0.0;
    for (std::size_t index = 0; index < samples.size(); ++index){
        const double predicted = trend.intercept + trend.slope * static_cast<double>(index);
        residualSum += std::pow(samples[index].occupancyRate - predicted, 2);
        totalSum += std::pow(samples[index].occupancyRate - meanY, 2);
    }
    trend.r2 = totalSum > 0.0 ? 1.0 - residualSum / totalSum : 0.0;
    return trend;
}

AnalysisReport AnalyticsEngine::analyze(ParkingRecord::TimePoint now) const{
    AnalysisReport report;
    report.model = modelName();
    report.generatedAt = now;
    if (!timeutil::isValid(now)){
        now = ParkingRecord::Clock::now();
    }
    const OperationalSnapshot data = snapshot(now);
    report.currentOccupancyRate = data.occupancyRate;
    report.peakEntryHour = data.peakEntryHour;
    report.averageDurationHours = data.averageDurationHours;
    report.totalRevenue = data.totalRevenue;

    // 本地小模型：对近 72 小时占用率序列做 OLS 拟合，外推未来 6 小时。
    report.trend = fitLinear(data.occupancySeries);
    const double lastX = static_cast<double>(data.occupancySeries.size() - 1);
    for (int hour = 1; hour <= kForecastHorizonHours; ++hour){
        double rate = report.trend.valid()
            ? report.trend.intercept + report.trend.slope * (lastX + hour)
            : data.occupancyRate;
        rate = std::clamp(rate, 0.0, 1.0);
        report.forecast.emplace_back(hour, rate * 100.0);
    }

    // ---- 规则结论 ----
    const bool enoughData = data.closedRecords + data.activeRecords >= 3
        && data.occupancySeries.size() >= 3;
    if (!enoughData){
        report.findings.push_back({AnalysisFinding::Category::DataQuality,
                                   "历史数据不足",
                                   "停车记录少于 3 条，趋势与预测仅供参考；"
                                   "建议积累一天以上运营数据后重新分析。"});
        std::ostringstream summary;
        summary << "当前占用率 " << formatRate(data.occupancyRate)
                << "（" << data.occupied << "/" << data.capacity << "）。"
                << "历史数据不足，暂无法给出可靠趋势结论。";
        report.summary = summary.str();
        report.recommendations.push_back("积累运营数据后重新执行分析。");
        return report;
    }

    // 趋势
    {
        const double slopePerHour = report.trend.slope;
        AnalysisFinding::Category category = AnalysisFinding::Category::Forecast;
        if (report.trend.valid() && std::abs(slopePerHour) >= 0.001){
            const bool rising = slopePerHour > 0;
            std::ostringstream detail;
            detail << (rising ? "上升" : "下降") << "约 "
                   << std::fixed << std::setprecision(1)
                   << std::abs(slopePerHour) * 100.0 << "%/小时（R²="
                   << std::setprecision(2) << report.trend.r2 << "），"
                   << "未来 " << kForecastHorizonHours
                   << " 小时预计 "
                   << formatRate(report.forecast.back().second / 100.0) << "。";
            report.findings.push_back({category,
                                       rising ? "占用率呈上升趋势" : "占用率呈下降趋势",
                                       detail.str()});
        } else{
            report.findings.push_back({category,
                                       "占用率基本平稳",
                                       "近 72 小时无显著上升或下降趋势。"});
        }
    }
    // 高峰
    if (data.peakEntryHour >= 0){
        std::ostringstream detail;
        detail << "历史入场记录集中在 " << data.peakEntryHour
               << ":00（UTC）前后，建议在该时段保持出口通道畅通。";
        report.findings.push_back({AnalysisFinding::Category::Peak,
                                   "入场高峰时段",
                                   detail.str()});
    }
    // 收入
    if (data.closedRecords > 0){
        std::ostringstream detail;
        detail << "累计结算 " << data.closedRecords << " 笔，收入 "
               << formatMoney(data.totalRevenue) << " 元，平均停车时长 "
               << std::fixed << std::setprecision(1) << data.averageDurationHours
               << " 小时。";
        report.findings.push_back({AnalysisFinding::Category::Revenue,
                                   "收入与时长",
                                   detail.str()});
    }
    // 分区
    if (!data.zones.empty()){
        const auto busiest = std::max_element(
            data.zones.begin(), data.zones.end(),
            [](const ZoneLoadStat &a, const ZoneLoadStat &b){
                return a.load < b.load;
            });
        const auto emptiest = std::min_element(
            data.zones.begin(), data.zones.end(),
            [](const ZoneLoadStat &a, const ZoneLoadStat &b){
                return a.load < b.load;
            });
        std::ostringstream detail;
        detail << "负载最高 " << busiest->zone << " 区 "
               << formatRate(busiest->load) << "，最低 " << emptiest->zone << " 区 "
               << formatRate(emptiest->load) << "。";
        report.findings.push_back({AnalysisFinding::Category::Zone,
                                   "分区负载",
                                   detail.str()});
        if (busiest->load - emptiest->load > 0.3){
            report.recommendations.push_back(
                "分区负载差超过 30%，建议检查引导标识或调整预约分配权重。");
        }
    }
    // 预约
    if (data.noShowBookings > 0 || data.reservationForfeited > 0.0){
        std::ostringstream detail;
        detail << "爽约预约 " << data.noShowBookings << " 笔（第一版预约）";
        if (data.reservationForfeited > 0.0){
            detail << "，时段预约没收定金 " << formatMoney(data.reservationForfeited)
                   << " 元";
        }
        detail << "。";
        report.findings.push_back({AnalysisFinding::Category::Booking,
                                   "预约爽约",
                                   detail.str()});
        report.recommendations.push_back("存在爽约记录，建议加强对预约用户的到场提醒。");
    }
    // 容量预警
    if (!report.forecast.empty() && report.forecast.back().second >= 85.0){
        report.recommendations.push_back(
            "预测占用率接近满容，建议限制对应时段的预约量并预留通道车位。");
    }
    if (data.averageDurationHours >= 4.0){
        report.recommendations.push_back("平均停车时长较长，可评估长时套餐或月卡定价。");
    }

    // 摘要：取前两条发现压缩成结论。
    std::ostringstream summary;
    summary << "当前占用率 " << formatRate(data.occupancyRate)
            << "（" << data.occupied << "/" << data.capacity << "）。";
    if (!report.findings.empty()){
        summary << report.findings.front().title << "。";
    }
    if (report.findings.size() > 1){
        summary << report.findings[1].title << "。";
    }
    report.summary = summary.str();
    if (report.recommendations.empty()){
        report.recommendations.push_back("运营指标正常，保持当前策略即可。");
    }
    return report;
}

} // namespace smartpark
