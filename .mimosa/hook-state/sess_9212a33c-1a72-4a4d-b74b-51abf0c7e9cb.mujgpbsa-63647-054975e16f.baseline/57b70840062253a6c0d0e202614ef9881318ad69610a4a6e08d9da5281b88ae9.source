#pragma once
#include "core/model/Vehicle.h"
#include "core/service/GridPlanner.h"
#include <chrono>
#include <optional>
#include <string>
namespace smartpark{
    // 远程时间段预约状态机：
    // PendingPayment -> Confirmed -> CheckedIn -> Completed
    //     |              |  |-> Cancelled / NoShow
    //     |-> Expired
    enum class ReservationStatus{
        PendingPayment, // 已创建订单，等待定金支付
        Confirmed,      // 定金已支付，等待到场
        CheckedIn,      // 已到场，定金转为停车预付款
        Completed,      // 已离场，定金抵扣停车费
        Cancelled,      // 已取消，定金退回
        NoShow,         // 爽约，定金没收
        Expired         // 支付超时关闭
    };
    inline int reservationStatusToInt(ReservationStatus status) noexcept{
        switch (status){
        case ReservationStatus::PendingPayment:
            return 0;
        case ReservationStatus::Confirmed:
            return 1;
        case ReservationStatus::CheckedIn:
            return 2;
        case ReservationStatus::Completed:
            return 3;
        case ReservationStatus::Cancelled:
            return 4;
        case ReservationStatus::NoShow:
            return 5;
        case ReservationStatus::Expired:
            return 6;
        }
        return -1;
    }
    inline std::optional<ReservationStatus> reservationStatusFromInt(int value) noexcept{
        switch (value){
        case 0:
            return ReservationStatus::PendingPayment;
        case 1:
            return ReservationStatus::Confirmed;
        case 2:
            return ReservationStatus::CheckedIn;
        case 3:
            return ReservationStatus::Completed;
        case 4:
            return ReservationStatus::Cancelled;
        case 5:
            return ReservationStatus::NoShow;
        case 6:
            return ReservationStatus::Expired;
        }
        return std::nullopt;
    }
    // 定金结算状态：Pending=已收取待结算；Refunded=取消退回；
    // Forfeited=爽约没收；Applied=离场时抵扣停车费。
    enum class DepositState{
        Pending,
        Refunded,
        Forfeited,
        Applied
    };
    inline int depositStateToInt(DepositState state) noexcept{
        switch (state){
        case DepositState::Pending:
            return 0;
        case DepositState::Refunded:
            return 1;
        case DepositState::Forfeited:
            return 2;
        case DepositState::Applied:
            return 3;
        }
        return -1;
    }
    inline std::optional<DepositState> depositStateFromInt(int value) noexcept{
        switch (value){
        case 0:
            return DepositState::Pending;
        case 1:
            return DepositState::Refunded;
        case 2:
            return DepositState::Forfeited;
        case 3:
            return DepositState::Applied;
        }
        return std::nullopt;
    }
    // 远程时间段预约业务规则。
    struct ReservationRule{
        int maxAdvanceDays{7};                    // 只能预约未来 7 天内
        std::chrono::minutes minLeadTime{30};     // 最短提前时间
        std::chrono::minutes minDuration{30};     // 最短预约时长
        std::chrono::minutes gracePeriod{30};     // 到场宽限期
        std::chrono::minutes lockLeadTime{30};    // 延迟锁位：开始前多久创建短时锁
        double deposit{20.0};                     // 定金
    };
    // 定金流水，持久化到 deposit_payments 表。
    struct DepositPayment{
        enum class Kind{
            Charge,   // 收取定金
            Refund,   // 取消退回
            Forfeit,  // 爽约没收
            Apply     // 离场抵扣停车费
        };
        enum class Status{
            Succeeded,
            Failed
        };
        std::string transactionId;
        std::string reservationId;
        std::string plateNumber;
        Kind kind{Kind::Charge};
        double amount{0.0};
        Status status{Status::Succeeded};
        std::chrono::system_clock::time_point createdAt{};
    };
    inline int depositPaymentKindToInt(DepositPayment::Kind kind) noexcept{
        switch (kind){
        case DepositPayment::Kind::Charge:
            return 0;
        case DepositPayment::Kind::Refund:
            return 1;
        case DepositPayment::Kind::Forfeit:
            return 2;
        case DepositPayment::Kind::Apply:
            return 3;
        }
        return -1;
    }
    inline std::optional<DepositPayment::Kind>
    depositPaymentKindFromInt(int value) noexcept{
        switch (value){
        case 0:
            return DepositPayment::Kind::Charge;
        case 1:
            return DepositPayment::Kind::Refund;
        case 2:
            return DepositPayment::Kind::Forfeit;
        case 3:
            return DepositPayment::Kind::Apply;
        }
        return std::nullopt;
    }
    // 下单时的预期路线快照。
    struct ExpectedRoute{
        std::size_t entranceIndex{0};
        std::size_t exitIndex{0};
        Route entryRoute;
        Route exitRoute;
        bool empty() const noexcept;
        // 文本序列化（空格分隔，max_digits10 精度），供 SQLite route 列使用。
        std::string encode() const;
        static std::optional<ExpectedRoute> decode(const std::string &text);
    };
    class Reservation{
    public:
        using Clock = std::chrono::system_clock;
        using TimePoint = Clock::time_point;
        // status 等参数供持久化层还原历史订单。
        Reservation(std::string id, std::string plateNumber, VehicleType vehicleType,
                    std::string spotId, TimePoint createdAt, TimePoint startTime,
                    TimePoint endTime, TimePoint graceDeadline, double deposit,
                    std::string chargeTransactionId,
                    ReservationStatus status = ReservationStatus::PendingPayment,
                    DepositState depositState = DepositState::Pending,
                    ExpectedRoute expectedRoute = {},
                    bool accessible = false);
        const std::string &id() const noexcept;
        const std::string &plateNumber() const noexcept;
        VehicleType vehicleType() const noexcept;
        const std::string &spotId() const noexcept;
        TimePoint createdAt() const noexcept;
        TimePoint startTime() const noexcept;
        TimePoint endTime() const noexcept;
        TimePoint graceDeadline() const noexcept;
        double deposit() const noexcept;
        const std::string &chargeTransactionId() const noexcept;
        ReservationStatus status() const noexcept;
        DepositState depositState() const noexcept;
        const ExpectedRoute &expectedRoute() const noexcept;
        // 无障碍关怀订单：免定金、宽限翻倍、仅限无障碍车位。
        bool isAccessible() const noexcept;
        // 未结束订单：PendingPayment / Confirmed / CheckedIn。
        bool isOpen() const noexcept;
        bool checkIn() noexcept;
        bool complete() noexcept;
        bool cancel() noexcept;
        bool markNoShow() noexcept;
        bool expire() noexcept;
        // 由 ReservationService 依据流水写入。
        void setDepositState(DepositState state) noexcept;
    private:
        std::string id_;
        std::string plateNumber_;
        VehicleType vehicleType_{VehicleType::Car};
        std::string spotId_;
        TimePoint createdAt_;
        TimePoint startTime_;
        TimePoint endTime_;
        TimePoint graceDeadline_;
        double deposit_;
        std::string chargeTransactionId_;
        ReservationStatus status_{ReservationStatus::PendingPayment};
        DepositState depositState_{DepositState::Pending};
        ExpectedRoute expectedRoute_;
        bool accessible_{false};
    };
} // namespace smartpark
