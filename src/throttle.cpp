#include "throttle.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

namespace graphql_sync {

ThrottleController::ThrottleController(double safetyMargin)
    : mSafetyMargin(safetyMargin) {}

void ThrottleController::observeResponse(const nlohmann::json& response) {
    try {
        if (!response.contains("extensions") ||
            !response["extensions"].contains("cost")) {
            return;  // nothing to observe
        }

        const auto& cost = response["extensions"]["cost"];

        if (cost.contains("requestedQueryCost")) {
            mLastRequestedCost = cost["requestedQueryCost"].get<double>();
        }

        if (cost.contains("throttleStatus")) {
            const auto& ts = cost["throttleStatus"];
            if (ts.contains("maximumAvailable"))
                mMaximumAvailable = ts["maximumAvailable"].get<double>();
            if (ts.contains("currentlyAvailable"))
                mCurrentlyAvailable = ts["currentlyAvailable"].get<double>();
            if (ts.contains("restoreRate"))
                mRestoreRate = ts["restoreRate"].get<double>();
        }

        mTotalCost += mLastRequestedCost;
        ++mObservationCount;
        mHasObserved = true;

    } catch (const nlohmann::json::exception& e) {
        std::cerr << "[Throttle] Warning: failed to parse cost info: "
                  << e.what() << "\n";
    }
}

void ThrottleController::maybeSleepBeforeNextRequest() {
    if (!mHasObserved || mRestoreRate <= 0.0) return;

    double needed = mLastRequestedCost + mSafetyMargin;
    if (mCurrentlyAvailable >= needed) return;

    double deficit      = needed - mCurrentlyAvailable;
    double sleepSeconds = std::ceil(deficit / mRestoreRate);
    sleepSeconds        = std::max(0.0, sleepSeconds);

    if (sleepSeconds > 0.0) {
        std::cerr << "[Throttle] Rate-limit approaching: sleeping "
                  << sleepSeconds << "s  (available="
                  << mCurrentlyAvailable << ", needed=" << needed
                  << ", restoreRate=" << mRestoreRate << ")\n";

        mTotalSleep += sleepSeconds;
        std::this_thread::sleep_for(
            std::chrono::milliseconds(static_cast<int64_t>(sleepSeconds * 1000)));
    }
}

double ThrottleController::avgQueryCost() const {
    if (mObservationCount == 0) return 0.0;
    return mTotalCost / mObservationCount;
}

} // namespace graphql_sync
