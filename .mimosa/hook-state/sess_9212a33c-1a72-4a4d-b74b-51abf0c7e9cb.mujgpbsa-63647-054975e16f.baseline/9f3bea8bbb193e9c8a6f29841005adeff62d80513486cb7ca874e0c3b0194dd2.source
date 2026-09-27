#pragma once
#include "core/model/Booking.h"
#include "core/model/ParkingRecord.h"
#include "core/service/ParkingService.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace smartpark{

// 一个时间点的占用率样本，值域 [0,1]。
struct OccupancySample{
    ParkingRecord::TimePoint time{};
    double occupancyRate{0.0};
};

// 分区负载统计。
struct ZoneLoadStat{
    std::string zone;
    int total{0};
    int occupied{0};
    double load{0.0}; // [0,1]
};

// 给远程分析接口的聚合运营快照：只含统计指标，不含车牌等隐私数据。
struct OperationalSnapshot{
    int capacity{0};
    int occupied{0};
    double occupancyRate{0.0};
    int activeRecords{0};
    int closedRecords{0};
    double totalRevenue{0.0};
    double averageDurationHours{0.0};
    int peakEntryHour{-1};          // UTC 小时，无数据为 -1
    int openBookings{0};
    int noShowBookings{0};
    double reservationForfeited{0.0};
    std::vector<ZoneLoadStat> zones;
    std::vector<OccupancySample> occupancySeries; // 近 72 小时逐时占用率
};

// 普通最小二乘拟合 y = a + b * x。
struct LinearTrend{
    double slope{0.0};
    double intercept{0.0};
    double r2{0.0};
    int samples{0};
    bool valid() const noexcept { return samples >= 3; }
};

struct AnalysisFinding{
    enum class Category{
        Forecast,
        Peak,
        Revenue,
        Zone,
        Booking,
        DataQuality
    };
    Category category{Category::Forecast};
    std::string title;
    std::string detail;
};

struct AnalysisReport{
    std::string model;                // 产生结论的模型标识
    ParkingRecord::TimePoint generatedAt{};
    std::string summary;              // 2-3 句中文结论
    std::vector<AnalysisFinding> findings;
    std::vector<std::string> recommendations;
    double currentOccupancyRate{0.0};
    LinearTrend trend;
    std::vector<std::pair<int, double>> forecast; // (小时偏移, 占用率%)，长度 kForecastHorizonHours
    int peakEntryHour{-1};
    double averageDurationHours{0.0};
    double totalRevenue{0.0};
    static const char *categoryText(AnalysisFinding::Category category);
};

// 本地数据分析引擎：小模型（OLS 线性回归）拟合近 72 小时占用率时间序列，
// 预测未来 6 小时；配合规则生成中文结论与建议。数据由 ParkingService 提供。
// 远程模型（LLM API）见 RemoteAnalystClient，输出结构与此层一致。
class AnalyticsEngine{
public:
    static constexpr int kForecastHorizonHours = 6;
    static constexpr int kTrendWindowHours = 72;

    explicit AnalyticsEngine(const ParkingService &service);

    AnalysisReport analyze(ParkingRecord::TimePoint now = ParkingRecord::Clock::now()) const;
    OperationalSnapshot snapshot(ParkingRecord::TimePoint now) const;

    // 普通最小二乘拟合；样本少于 3 时返回 invalid。
    static LinearTrend fitLinear(const std::vector<OccupancySample> &samples);

    static const char *modelName(){ return "local-ols-v1"; }

private:
    static int hourOfDay(ParkingRecord::TimePoint time);
    const ParkingService *service_;
};

} // namespace smartpark
