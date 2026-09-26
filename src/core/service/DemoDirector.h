#pragma once
#include "core/model/Vehicle.h"
#include "core/service/ParkingService.h"

#include <string>
#include <vector>

namespace smartpark{

// 剧本式一键演示：按预置脚本以虚拟时钟驱动现有业务流
// （批量入场 → 分区再平衡 → 时段预约 → 爽约 → 到场 → 离场计费 → 分析结论），
// 每步返回一句人类可读的说明，供 CLI/GUI 展示。
class DemoDirector{
public:
    explicit DemoDirector(ParkingService &service);

    // 执行下一步；返回 false 表示剧本结束。
    bool step();
    const std::string &lastDescription() const noexcept;
    // 结尾分析步骤的结论摘要（其他步骤为空）。
    const std::string &lastAnalysisSummary() const noexcept;
    int stepsDone() const noexcept;
    int totalSteps() const noexcept;

private:
    enum class Kind{
        Enter,
        Leave,
        CreateReservation,
        NoShowSweep,
        CheckIn,
        Analyze
    };
    struct Step{
        Kind kind;
        std::string plate;
        VehicleType type{VehicleType::Car};
        int offsetMinutes{0};   // 相对基准时间
        int stayMinutes{0};
        bool accessible{false};
    };

    void buildScript();

    ParkingService *service_;
    std::vector<Step> steps_;
    std::size_t index_{0};
    std::string lastDescription_;
    std::string lastAnalysisSummary_;
    std::vector<AllocationResult> entered_;
    ParkingRecord::TimePoint base_;
};

} // namespace smartpark
