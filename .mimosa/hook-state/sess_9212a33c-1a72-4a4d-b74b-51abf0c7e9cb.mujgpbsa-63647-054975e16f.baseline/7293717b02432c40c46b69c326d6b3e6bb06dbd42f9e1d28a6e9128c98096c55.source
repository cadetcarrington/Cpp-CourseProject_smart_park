#include "core/service/DemoDirector.h"
#include "core/service/AnalyticsEngine.h"
#include "core/service/ReservationService.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace smartpark{
DemoDirector::DemoDirector(ParkingService &service)
    : service_(&service)
    , base_(ParkingRecord::Clock::now()){
    buildScript();
}

void DemoDirector::buildScript(){
    // 每次演示生成唯一车牌后缀，保证同一数据库上可重复运行。
    const auto suffix = std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            base_.time_since_epoch()).count() % 100000);
    const VehicleType types[] = {
        VehicleType::Car, VehicleType::Electric, VehicleType::Truck,
        VehicleType::Motorcycle};
    for (int index = 0; index < 8; ++index){
        steps_.push_back({Kind::Enter, "晋演示" + suffix + std::to_string(index),
                          types[index % 4], 0, 0, false});
    }
    // 布局没有无障碍车位时回退为普通预约。
    const bool hasAccessible = std::any_of(
        service_->spots().begin(), service_->spots().end(),
        [](const ParkingSpot &spot){
            return spot.type() == SpotType::Accessible;
        });
    steps_.push_back({Kind::CreateReservation, "晋演示预约" + suffix,
                      VehicleType::Car, 120, 180, hasAccessible});
    steps_.push_back({Kind::NoShowSweep, "晋演示爽约" + suffix,
                      VehicleType::Electric, 240, 0, false});
    for (int index = 0; index < 3; ++index){
        steps_.push_back({Kind::Leave, "晋演示" + suffix + std::to_string(index),
                          types[index % 4], 60 + index * 15, 0, false});
    }
    steps_.push_back({Kind::CheckIn, "晋演示预约" + suffix, VehicleType::Car,
                      240, 0, true});
    steps_.push_back({Kind::Leave, "晋演示预约" + suffix, VehicleType::Car,
                      300, 0, true});
    steps_.push_back({Kind::Analyze, {}, VehicleType::Car, 300, 0, false});
}

bool DemoDirector::step(){
    if (index_ >= steps_.size()){
        return false;
    }
    const Step &step = steps_[index_];
    const auto now = base_ + std::chrono::minutes(step.offsetMinutes);
    std::ostringstream description;
    bool ok = true;
    switch (step.kind){
    case Kind::Enter:{
        const auto result = service_->enter({step.plate, step.type}, now);
        ok = result.has_value();
        description << "入场 " << step.plate << " -> "
                    << (ok ? result->spotId : std::string("失败"));
        if (ok){
            entered_.push_back(*result);
        }
        break;
    }
    case Kind::Leave:{
        const auto closed = service_->leave(step.plate, now);
        ok = closed.has_value();
        description << "离场 " << step.plate;
        if (ok){
            std::ostringstream fee;
            fee << std::fixed << std::setprecision(2) << closed->fee();
            description << "，费用 " << fee.str() << " 元";
        } else{
            description << "失败";
        }
        break;
    }
    case Kind::CreateReservation:{
        const auto start = now + std::chrono::minutes(120);
        const auto created = service_->reservations().create(
            {step.plate, step.type}, start, start + std::chrono::minutes(180),
            now, step.accessible);
        ok = created.has_value();
        description << (step.accessible ? "无障碍关怀时段预约 " : "时段预约 ")
                    << step.plate;
        if (ok){
            description << (step.accessible ? "（免定金、宽限翻倍）成功" : "成功");
        } else{
            description << "失败";
        }
        if (ok){
            description << "，车位 " << created->reservation.spotId();
        }
        break;
    }
    case Kind::NoShowSweep:{
        // 先给另一辆车创建一笔普通预约，再推进时间越过宽限期演示爽约。
        const auto start = now - std::chrono::hours(4);
        (void)service_->reservations().create({step.plate, step.type}, start,
                                              start + std::chrono::hours(2),
                                              start - std::chrono::hours(1));
        service_->reservations().sweep(now);
        description << "时间推进：超宽限期预约判定爽约并没收定金";
        ok = true;
        break;
    }
    case Kind::CheckIn:{
        const auto arrived = service_->reservations().checkIn(step.plate, now);
        ok = arrived.has_value();
        description << "预约到场 " << step.plate;
        if (ok){
            description << "，占用 " << arrived->spotId << "，定金转预付";
        } else{
            description << "失败";
        }
        break;
    }
    case Kind::Analyze:{
        AnalyticsEngine engine(*service_);
        const auto report = engine.analyze(now);
        lastAnalysisSummary_ = report.summary;
        description << "生成数据分析报告";
        break;
    }
    }
    lastDescription_ = description.str();
    ++index_;
    return true;
}

const std::string &DemoDirector::lastDescription() const noexcept{
    return lastDescription_;
}

const std::string &DemoDirector::lastAnalysisSummary() const noexcept{
    return lastAnalysisSummary_;
}

int DemoDirector::stepsDone() const noexcept{
    return static_cast<int>(index_);
}

int DemoDirector::totalSteps() const noexcept{
    return static_cast<int>(steps_.size());
}

} // namespace smartpark
