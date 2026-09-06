#include "core/model/ParkingRecord.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace smartpark {

ParkingRecord::ParkingRecord(std::string plateNumber, std::string spotId,
                             TimePoint entryTime)
    : plateNumber_(std::move(plateNumber))
    , spotId_(std::move(spotId))
    , entryTime_(entryTime)
{
    if (plateNumber_.empty()) {
        throw std::invalid_argument("plate number cannot be empty");
    }
    if (spotId_.empty()) {
        throw std::invalid_argument("spot id cannot be empty");
    }
}

const std::string &ParkingRecord::plateNumber() const noexcept
{
    return plateNumber_;
}

const std::string &ParkingRecord::spotId() const noexcept
{
    return spotId_;
}

ParkingRecord::TimePoint ParkingRecord::entryTime() const noexcept
{
    return entryTime_;
}

const std::optional<ParkingRecord::TimePoint> &ParkingRecord::exitTime() const noexcept
{
    return exitTime_;
}

bool ParkingRecord::isClosed() const noexcept
{
    return exitTime_.has_value();
}

std::chrono::seconds ParkingRecord::duration() const noexcept
{
    const auto endPoint = exitTime_.value_or(Clock::now());
    return std::chrono::duration_cast<std::chrono::seconds>(endPoint - entryTime_);
}

double ParkingRecord::fee() const noexcept
{
    return fee_;
}

bool ParkingRecord::close(TimePoint exitTime, double recordFee)
{
    if (isClosed() || exitTime < entryTime_ || !std::isfinite(recordFee)
        || recordFee < 0.0) {
        return false;
    }
    exitTime_ = exitTime;
    fee_ = recordFee;
    return true;
}

} // namespace smartpark
