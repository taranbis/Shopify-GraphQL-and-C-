/// @file test_throttle.cpp
/// Unit tests for throttle.hpp — cost-based rate-limit controller.

#include "throttle.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <chrono>

using namespace graphql_sync;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Helper: build a Shopify-style response with extensions.cost
// ---------------------------------------------------------------------------

static json makeThrottleResponse(double requestedCost,
                                 double maxAvailable,
                                 double currentlyAvailable,
                                 double restoreRate) {
    return {
        {"data", {{"products", {}}}},
        {"extensions", {
            {"cost", {
                {"requestedQueryCost", requestedCost},
                {"throttleStatus", {
                    {"maximumAvailable", maxAvailable},
                    {"currentlyAvailable", currentlyAvailable},
                    {"restoreRate", restoreRate}
                }}
            }}
        }}
    };
}

// ============================================================================
// Construction and defaults
// ============================================================================

TEST(ThrottleController, FreshControllerHasZeroStats) {
    ThrottleController tc;
    EXPECT_DOUBLE_EQ(tc.totalSleepSeconds(), 0.0);
    EXPECT_DOUBLE_EQ(tc.avgQueryCost(), 0.0);
    EXPECT_EQ(tc.totalObservations(), 0);
}

// ============================================================================
// observeResponse — tracking costs
// ============================================================================

TEST(ThrottleController, SingleObservationTracksCost) {
    ThrottleController tc;
    tc.observeResponse(makeThrottleResponse(52.0, 200.0, 148.0, 50.0));

    EXPECT_EQ(tc.totalObservations(), 1);
    EXPECT_DOUBLE_EQ(tc.avgQueryCost(), 52.0);
}

TEST(ThrottleController, MultipleObservationsAverageCorrectly) {
    ThrottleController tc;
    tc.observeResponse(makeThrottleResponse(50.0, 200.0, 150.0, 50.0));
    tc.observeResponse(makeThrottleResponse(100.0, 200.0, 50.0, 50.0));

    EXPECT_EQ(tc.totalObservations(), 2);
    EXPECT_DOUBLE_EQ(tc.avgQueryCost(), 75.0);  // (50 + 100) / 2
}

TEST(ThrottleController, ThreeObservationsAverage) {
    ThrottleController tc;
    tc.observeResponse(makeThrottleResponse(10.0, 200.0, 190.0, 50.0));
    tc.observeResponse(makeThrottleResponse(20.0, 200.0, 170.0, 50.0));
    tc.observeResponse(makeThrottleResponse(30.0, 200.0, 140.0, 50.0));

    EXPECT_EQ(tc.totalObservations(), 3);
    EXPECT_DOUBLE_EQ(tc.avgQueryCost(), 20.0);  // (10 + 20 + 30) / 3
}

// ============================================================================
// observeResponse — edge cases
// ============================================================================

TEST(ThrottleController, ResponseWithoutExtensionsIsIgnored) {
    ThrottleController tc;
    json resp = {{"data", {{"products", {}}}}};
    tc.observeResponse(resp);

    EXPECT_EQ(tc.totalObservations(), 0);
    EXPECT_DOUBLE_EQ(tc.avgQueryCost(), 0.0);
}

TEST(ThrottleController, ResponseWithPartialCostInfoIsHandled) {
    ThrottleController tc;
    // Has extensions.cost.requestedQueryCost but no throttleStatus.
    json resp = {
        {"data", {{"products", {}}}},
        {"extensions", {{"cost", {{"requestedQueryCost", 30.0}}}}}
    };
    tc.observeResponse(resp);

    EXPECT_EQ(tc.totalObservations(), 1);
    EXPECT_DOUBLE_EQ(tc.avgQueryCost(), 30.0);
}

TEST(ThrottleController, MalformedExtensionsDoNotCrash) {
    ThrottleController tc;
    json resp = {
        {"data", nullptr},
        {"extensions", "not-an-object"}
    };
    // Should not throw; the controller catches json exceptions.
    EXPECT_NO_THROW(tc.observeResponse(resp));
    EXPECT_EQ(tc.totalObservations(), 0);
}

// ============================================================================
// maybeSleepBeforeNextRequest — no-sleep path
// ============================================================================

TEST(ThrottleController, NoSleepBeforeFirstObservation) {
    ThrottleController tc;
    tc.maybeSleepBeforeNextRequest();

    EXPECT_DOUBLE_EQ(tc.totalSleepSeconds(), 0.0);
}

TEST(ThrottleController, NoSleepWhenBudgetIsHigh) {
    ThrottleController tc(20.0);  // safetyMargin = 20
    // requestedCost=52, available=200 => needed = 52 + 20 = 72 < 200 => no sleep
    tc.observeResponse(makeThrottleResponse(52.0, 200.0, 200.0, 50.0));
    tc.maybeSleepBeforeNextRequest();

    EXPECT_DOUBLE_EQ(tc.totalSleepSeconds(), 0.0);
}

TEST(ThrottleController, NoSleepWhenBudgetExactlyMeetsNeed) {
    ThrottleController tc(0.0);  // safetyMargin = 0
    // needed = 52 + 0 = 52, available = 52 => exactly enough => no sleep
    tc.observeResponse(makeThrottleResponse(52.0, 200.0, 52.0, 50.0));
    tc.maybeSleepBeforeNextRequest();

    EXPECT_DOUBLE_EQ(tc.totalSleepSeconds(), 0.0);
}

// ============================================================================
// maybeSleepBeforeNextRequest — sleep path
// NOTE: These tests actually sleep (~1 second) because ThrottleController
//       calls std::this_thread::sleep_for internally.
// ============================================================================

TEST(ThrottleController, SleepsWhenBudgetIsLow) {
    ThrottleController tc(0.0);  // safetyMargin = 0
    // requestedCost=100, available=50, restoreRate=100
    // needed = 100, deficit = 100 - 50 = 50
    // sleepSeconds = ceil(50 / 100) = ceil(0.5) = 1
    tc.observeResponse(makeThrottleResponse(100.0, 200.0, 50.0, 100.0));

    auto start = std::chrono::steady_clock::now();
    tc.maybeSleepBeforeNextRequest();
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_GT(tc.totalSleepSeconds(), 0.0);

    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    EXPECT_GE(elapsedMs, 900);   // at least ~900 ms (1s minus scheduling tolerance)
    EXPECT_LE(elapsedMs, 1500);  // at most ~1.5s
}

TEST(ThrottleController, SleepsTwoSecondsWithLargerDeficit) {
    ThrottleController tc(0.0);  // safetyMargin = 0
    // requestedCost=200, available=50, restoreRate=100
    // needed = 200, deficit = 200 - 50 = 150
    // sleepSeconds = ceil(150 / 100) = ceil(1.5) = 2
    tc.observeResponse(makeThrottleResponse(200.0, 400.0, 50.0, 100.0));

    auto start = std::chrono::steady_clock::now();
    tc.maybeSleepBeforeNextRequest();
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_DOUBLE_EQ(tc.totalSleepSeconds(), 2.0);

    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    EXPECT_GE(elapsedMs, 1900);  // ~2 seconds
    EXPECT_LE(elapsedMs, 2500);
}

TEST(ThrottleController, CumulativeSleepAcrossMultipleCalls) {
    ThrottleController tc(0.0);  // safetyMargin = 0

    // First cycle: cost=100, available=50, rate=100 => 1s sleep.
    tc.observeResponse(makeThrottleResponse(100.0, 200.0, 50.0, 100.0));
    tc.maybeSleepBeforeNextRequest();
    EXPECT_DOUBLE_EQ(tc.totalSleepSeconds(), 1.0);

    // Simulate server response after restoration: budget is healthy again.
    tc.observeResponse(makeThrottleResponse(100.0, 200.0, 200.0, 100.0));
    tc.maybeSleepBeforeNextRequest();  // no sleep needed
    EXPECT_DOUBLE_EQ(tc.totalSleepSeconds(), 1.0);  // unchanged

    // Second cycle: budget is low again => another 1s sleep.
    tc.observeResponse(makeThrottleResponse(100.0, 200.0, 50.0, 100.0));
    tc.maybeSleepBeforeNextRequest();
    EXPECT_DOUBLE_EQ(tc.totalSleepSeconds(), 2.0);  // cumulative
}

TEST(ThrottleController, AfterBudgetRestoresNoMoreSleepNeeded) {
    ThrottleController tc(20.0);  // safetyMargin = 20

    // Low budget: cost=52, available=30, needed=72 => sleep.
    // deficit = 72 - 30 = 42, sleepSeconds = ceil(42/50) = 1
    tc.observeResponse(makeThrottleResponse(52.0, 200.0, 30.0, 50.0));
    tc.maybeSleepBeforeNextRequest();
    EXPECT_DOUBLE_EQ(tc.totalSleepSeconds(), 1.0);

    // Server responds after sleep with restored budget.
    tc.observeResponse(makeThrottleResponse(52.0, 200.0, 148.0, 50.0));
    // needed = 52 + 20 = 72, available = 148 >= 72 => no sleep.
    tc.maybeSleepBeforeNextRequest();
    EXPECT_DOUBLE_EQ(tc.totalSleepSeconds(), 1.0);  // unchanged — no extra sleep
}

TEST(ThrottleController, SafetyMarginAffectsSleepDecision) {
    // With safetyMargin=0: available=55, cost=52 => needed=52, 55 >= 52 => no sleep.
    ThrottleController tcNoMargin(0.0);
    tcNoMargin.observeResponse(makeThrottleResponse(52.0, 200.0, 55.0, 50.0));
    tcNoMargin.maybeSleepBeforeNextRequest();
    EXPECT_DOUBLE_EQ(tcNoMargin.totalSleepSeconds(), 0.0);

    // With safetyMargin=20: available=55, cost=52 => needed=72, 55 < 72 => SLEEP.
    ThrottleController tcWithMargin(20.0);
    tcWithMargin.observeResponse(makeThrottleResponse(52.0, 200.0, 55.0, 50.0));
    tcWithMargin.maybeSleepBeforeNextRequest();
    EXPECT_GT(tcWithMargin.totalSleepSeconds(), 0.0);
}
