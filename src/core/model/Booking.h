#pragma once
#include <chrono>
#include <optional>
#include <string>
namespace smartpark{
    // 预约状态
    enum class BookingStatus{
        Booked,     // 已预约，等待到场
        CheckedIn,  // 已到场，转为停车
        NoShow,     // 未到场，定金没收
        Cancelled   // 已取消
    };
    inline int bookingStatusToInt(BookingStatus status) noexcept{
        switch (status){
        case BookingStatus::Booked:
            return 0;
        case BookingStatus::CheckedIn:
            return 1;
        case BookingStatus::NoShow:
            return 2;
        case BookingStatus::Cancelled:
            return 3;
        }
        return -1;
    }
    inline std::optional<BookingStatus> bookingStatusFromInt(int value) noexcept{
        switch (value){
        case 0:
            return BookingStatus::Booked;
        case 1:
            return BookingStatus::CheckedIn;
        case 2:
            return BookingStatus::NoShow;
        case 3:
            return BookingStatus::Cancelled;
        }
        return std::nullopt;
    }
    // 预约策略：定金、最多提前天数与到场宽限期。
    struct BookingPolicy{
        double deposit{20.0};
        int advanceDays{7};
        std::chrono::minutes gracePeriod{30};
    };
    class Booking{
    public:
        using Clock = std::chrono::system_clock;
        using TimePoint = Clock::time_point;
        // status 供持久化层在重启恢复时直接还原历史状态，业务新建时默认 Booked。
        Booking(std::string id, std::string plateNumber, std::string spotId,
                TimePoint createdAt, TimePoint arrivalTime, TimePoint arrivalDeadline,
                double deposit, BookingStatus status = BookingStatus::Booked);
        const std::string &id() const noexcept;
        const std::string &plateNumber() const noexcept;
        const std::string &spotId() const noexcept;
        TimePoint createdAt() const noexcept;
        TimePoint arrivalTime() const noexcept;
        TimePoint arrivalDeadline() const noexcept;
        double deposit() const noexcept;
        BookingStatus status() const noexcept;
        bool isActive() const noexcept;
        bool checkIn() noexcept;
        bool markNoShow() noexcept;
        bool cancel() noexcept;
    private:
        std::string id_;
        std::string plateNumber_;
        std::string spotId_;
        TimePoint createdAt_;
        TimePoint arrivalTime_;
        TimePoint arrivalDeadline_;
        double deposit_;
        BookingStatus status_{BookingStatus::Booked};
    };
} // namespace smartpark