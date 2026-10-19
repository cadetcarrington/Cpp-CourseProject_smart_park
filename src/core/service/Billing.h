#pragma once

#include <chrono>

namespace smartpark {

struct BillingRule {
    std::chrono::minutes freeDuration{30};
    std::chrono::minutes billingUnit{30};
    double minimumFee{2.5};
    double unitFee{2.5};
    double dailyCap{100.0};
};

class BillingService {
public:
    explicit BillingService(BillingRule rule = BillingRule{});

    const BillingRule &rule() const noexcept;
    double calculateFee(std::chrono::seconds duration) const;

private:
    BillingRule rule_;
};

} // namespace smartpark
