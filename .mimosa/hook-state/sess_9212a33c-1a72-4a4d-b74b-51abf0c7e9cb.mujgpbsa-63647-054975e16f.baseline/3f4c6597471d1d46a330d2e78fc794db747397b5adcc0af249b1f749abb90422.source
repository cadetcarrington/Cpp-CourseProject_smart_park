#pragma once
#include <chrono>
#include <optional>
#include <string>
namespace smartpark{
// 定金模拟支付网关：生成模拟交易号，失败路径由 failNextCharge 注入。
class FakePaymentGateway{
public:
    using TimePoint = std::chrono::system_clock::time_point;
    // 收取定金：成功返回交易号，失败返回空。
    std::optional<std::string> charge(const std::string &reservationId,
                                      const std::string &plateNumber,
                                      double amount, TimePoint now);
    // 退回 / 没收：针对原定金交易发起，成功返回新交易号。
    std::optional<std::string> refund(const std::string &chargeTransactionId,
                                      double amount, TimePoint now);
    std::optional<std::string> forfeit(const std::string &chargeTransactionId,
                                       double amount, TimePoint now);
    // 测试钩子：使下一次 charge 失败，模拟支付失败路径。
    void failNextCharge() noexcept;
private:
    std::optional<std::string> settle(const std::string &prefix,
                                      const std::string &referenceId,
                                      double amount, TimePoint now);
    bool nextChargeFails_{false};
    unsigned long long sequence_{0};
};
} // namespace smartpark
