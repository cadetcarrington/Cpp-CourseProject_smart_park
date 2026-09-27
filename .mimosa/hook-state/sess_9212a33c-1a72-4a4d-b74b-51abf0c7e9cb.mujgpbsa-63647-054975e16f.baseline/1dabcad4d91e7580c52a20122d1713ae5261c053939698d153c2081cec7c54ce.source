#include "core/service/Billing.h"

#include <algorithm>

namespace smartpark {

BillingService::BillingService(BillingRule rule)
    : rule_(rule){
}

const BillingRule &BillingService::rule() const noexcept{
    return rule_;
}

double BillingService::calculateFee(std::chrono::seconds duration) const{
    if (duration <= std::chrono::seconds::zero()
        || rule_.billingUnit <= std::chrono::minutes::zero()){
        return 0.0;
    }

    if (duration <= rule_.freeDuration){
        return 0.0;
    }

    const auto billable = duration - rule_.freeDuration;
    const auto billableSeconds = std::chrono::duration_cast<std::chrono::seconds>(billable);
    const long unitSeconds = std::chrono::duration_cast<std::chrono::seconds>(
        rule_.billingUnit).count();
    if (unitSeconds <= 0){
        return 0.0;
    }
    const long units = (billableSeconds.count() + unitSeconds - 1) / unitSeconds;

    if (units <= 1) {
        return std::min(rule_.minimumFee, rule_.dailyCap);
    }

    const double fee = rule_.minimumFee
        + static_cast<double>(units - 1) * rule_.unitFee;

    return std::min(fee, rule_.dailyCap);
}

} // namespace smartpark
