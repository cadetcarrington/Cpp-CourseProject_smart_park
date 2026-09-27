#include "core/service/FakePaymentGateway.h"
#include <cmath>
#include <sstream>
namespace smartpark{
std::optional<std::string> FakePaymentGateway::charge(
    const std::string &reservationId, const std::string &plateNumber,
    double amount, TimePoint now){
    if (nextChargeFails_){
        nextChargeFails_ = false;
        return std::nullopt;
    }
    (void)plateNumber;
    return settle("PAY", reservationId, amount, now);
}
std::optional<std::string> FakePaymentGateway::refund(
    const std::string &chargeTransactionId, double amount, TimePoint now){
    return settle("RFD", chargeTransactionId, amount, now);
}
std::optional<std::string> FakePaymentGateway::forfeit(
    const std::string &chargeTransactionId, double amount, TimePoint now){
    return settle("FFT", chargeTransactionId, amount, now);
}
void FakePaymentGateway::failNextCharge() noexcept{
    nextChargeFails_ = true;
}
std::optional<std::string> FakePaymentGateway::settle(
    const std::string &prefix, const std::string &referenceId, double amount,
    TimePoint now){
    if (referenceId.empty() || !std::isfinite(amount) || amount < 0.0){
        return std::nullopt;
    }
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch())
                            .count();
    std::ostringstream stream;
    stream << prefix << '-' << millis << '-' << sequence_++;
    return stream.str();
}
} // namespace smartpark
