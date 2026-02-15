#pragma once

#include <nlohmann/json.hpp>

namespace graphql_sync {

/// Observes Shopify-style cost / throttle-status extensions and sleeps
/// when the available query-cost budget is too low for the next request.
class ThrottleController {
public:
    /// @param safetyMargin  Extra budget headroom before triggering a sleep.
    explicit ThrottleController(double safetyMargin = 20.0);

    /// Extract cost fields from a GraphQL response JSON.
    void observeResponse(const nlohmann::json& response);

    /// If the remaining budget is too low, sleep until enough points restore.
    void maybeSleepBeforeNextRequest();

    // ---- accessors for summary report ----
    double totalSleepSeconds() const { return mTotalSleep; }
    double avgQueryCost()      const;
    int    totalObservations()  const { return mObservationCount; }

private:
    double mSafetyMargin;

    double mLastRequestedCost  = 0.0;
    double mMaximumAvailable   = 1000.0;
    double mCurrentlyAvailable = 1000.0;
    double mRestoreRate        = 50.0;

    double mTotalSleep         = 0.0;
    double mTotalCost          = 0.0;
    int    mObservationCount   = 0;

    bool   mHasObserved        = false;
};

} // namespace graphql_sync
