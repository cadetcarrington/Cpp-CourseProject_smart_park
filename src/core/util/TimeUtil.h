#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>

namespace smartpark {
namespace timeutil {

using TimePoint = std::chrono::system_clock::time_point;

// 业务时间范围：2000-01-01T00:00:00Z 至 2200-01-01T00:00:00Z。
// 该范围同时保证“毫秒 ↔ 时间点”转换不会溢出常见的纳秒级系统时钟。
constexpr std::int64_t minValidSeconds = 946684800;
constexpr std::int64_t maxValidSeconds = 7258118400;

inline bool isValid(const TimePoint &time) noexcept
{
    const auto sinceEpoch = time.time_since_epoch();
    return sinceEpoch >= std::chrono::milliseconds(minValidSeconds * 1000)
        && sinceEpoch <= std::chrono::milliseconds(maxValidSeconds * 1000);
}

inline bool canAdd(const TimePoint &time, std::chrono::seconds delta) noexcept
{
    if (delta.count() < 0 || delta.count() > maxValidSeconds || !isValid(time)) {
        return false;
    }
    const auto sinceEpoch = time.time_since_epoch();
    const auto maxDuration = std::chrono::milliseconds(maxValidSeconds * 1000);
    const auto deltaDuration =
        std::chrono::duration_cast<std::chrono::milliseconds>(delta);
    return sinceEpoch <= maxDuration - deltaDuration;
}

template<typename Integer>
bool toMilliseconds(const TimePoint &time, Integer &milliseconds) noexcept
{
    const auto sinceEpoch = time.time_since_epoch();
    if (!std::isfinite(std::chrono::duration<double>(sinceEpoch).count())
        || !isValid(time)) {
        return false;
    }
    milliseconds = static_cast<Integer>(
        std::chrono::duration_cast<std::chrono::milliseconds>(sinceEpoch).count());
    return true;
}

template<typename Integer>
bool fromMilliseconds(Integer milliseconds, TimePoint &time) noexcept
{
    constexpr Integer millisecondsPerSecond = 1000;
    if (milliseconds < minValidSeconds * millisecondsPerSecond
        || milliseconds > maxValidSeconds * millisecondsPerSecond) {
        return false;
    }
    time = TimePoint{} + std::chrono::milliseconds(milliseconds);
    return true;
}

} // namespace timeutil
} // namespace smartpark
