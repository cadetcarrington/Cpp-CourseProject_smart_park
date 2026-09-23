#pragma once

#include "core/model/Booking.h"
#include "core/model/ParkingRecord.h"
#include "core/model/ParkingSpot.h"

#include <string>
#include <vector>

namespace smartpark{

// 单个分区的运营洞察。压力是“占用 + 0.7×预留 + 0.5×停用”折算负载后
// 除以该分区总车位数，值域 [0,1]。
struct ZoneInsight{
    std::string zone;
    int total{0};
    int available{0};
    int occupied{0};
    int reserved{0};
    int disabled{0};
    double load{0.0};
    double pressure{0.0};
};

// 一条占用率预测：基于最近入场/离场速率的指数平滑与未来有效预约数量。
struct OccupancyForecast{
    int minutes{0};
    double predictedOccupied{0.0};
    double predictedRate{0.0};   // 百分比 [0,100]
    double confidence{0.0};      // 样本充足度启发值 [0,1]
    int upcomingBookings{0};
    int sampleEvents{0};
};

struct RiskAlert{
    enum class Severity{
        Info,
        Warning,
        Critical
    };

    Severity severity{Severity::Info};
    std::string title;
    std::string reason;
    std::string action;
};

// 一次完整运营洞察。无风险时 alerts 会包含一条 Info。
struct ParkingInsights{
    std::vector<ZoneInsight> zones;
    std::vector<OccupancyForecast> forecasts;
    std::vector<RiskAlert> alerts;

    int effectiveCapacity{0};
    int currentOccupied{0};
    int currentReserved{0};
    int currentDisabled{0};

    double currentRate{0.0};
    double confidence{0.0};

    int arrivals60{0};
    int arrivals180{0};
    int departures60{0};
    int departures180{0};
};

// 透明规则预测 / 运营洞察层。
// 注意：这不是大模型预测，也不替代未来 `src/lpr` 与远程预约中的真实
// 学习模型；只基于当前车位、停车记录与预约数据给出可解释的本地推演。
class ParkingInsightEngine{
public:
    static ParkingInsights analyze(
        const std::vector<ParkingSpot> &spots,
        const std::vector<ParkingRecord> &records,
        const std::vector<Booking> &bookings,
        ParkingRecord::TimePoint now = ParkingRecord::Clock::now());
};

} // namespace smartpark
