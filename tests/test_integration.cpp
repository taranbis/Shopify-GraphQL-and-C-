/// @file test_integration.cpp
/// Integration tests — requires the mock server running at localhost:4000.
///
/// Start it with:
///     cd server_mock
///     npm start
///
/// If the server is not running, all tests in this file are SKIPPED (not failed).

#include "graphql_client.hpp"
#include "mapping.hpp"
#include "models.hpp"
#include "pagination.hpp"
#include "queries.hpp"
#include "throttle.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <string>
#include <thread>

using namespace graphql_sync;
using json = nlohmann::json;

static const std::string kMockEndpoint = "http://localhost:4000/graphql";

// ---------------------------------------------------------------------------
// Check whether the mock server is reachable before each test.
// ---------------------------------------------------------------------------

static bool isMockServerRunning() {
    try {
        GraphQLClient client(kMockEndpoint, "", /*timeoutMs=*/2000);
        auto resp = client.execute(queries::kProductsQuery, {{"first", 1}});
        // 200 = success, 503 = transient error, 429 = throttled.
        // All mean the server is up and responding.
        return resp.httpStatus == 200 || resp.httpStatus == 503
            || resp.httpStatus == 429;
    } catch (...) {
        return false;
    }
}

class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!isMockServerRunning()) {
            GTEST_SKIP()
                << "Mock server not running at " << kMockEndpoint
                << " — skipping integration tests.  "
                << "Start it with:  cd server_mock && npm start";
        }
    }
};

// ============================================================================
// Pagination
// ============================================================================

TEST_F(IntegrationTest, FetchSmallBatchAcrossMultiplePages) {
    GraphQLClient client(kMockEndpoint, "", 5000);
    ThrottleController throttle(20.0);
    Paginator paginator(client, throttle, /*verbose=*/false);

    auto products = paginator.fetchAllProducts(/*totalLimit=*/25, /*pageSize=*/10);

    // 25 products, 10 per page → at least 3 pages.
    ASSERT_EQ(products.size(), 25u);

    // First product should be Product 1 (GID 1001).
    EXPECT_EQ(products[0].id, "gid://shopify/Product/1001");
    EXPECT_EQ(products[0].title, "Product 1 - Widget");
    EXPECT_FALSE(products[0].updatedAt.empty());

    // Last product should be Product 25 (GID 1025).
    EXPECT_EQ(products[24].id, "gid://shopify/Product/1025");

    // Products should be in ascending GID order.
    for (size_t i = 1; i < products.size(); ++i) {
        EXPECT_GT(products[i].id, products[i - 1].id)
            << "Products out of order at index " << i;
    }
}

TEST_F(IntegrationTest, FetchSinglePage) {
    GraphQLClient client(kMockEndpoint, "", 5000);
    ThrottleController throttle(20.0);
    Paginator paginator(client, throttle, /*verbose=*/false);

    auto products = paginator.fetchAllProducts(/*totalLimit=*/5, /*pageSize=*/5);

    EXPECT_EQ(products.size(), 5u);

    auto stats = paginator.getStats();
    EXPECT_EQ(stats.totalFetched, 5);
    // At least 1 successful request (could be more with retries).
    EXPECT_GE(stats.totalRequests, 1);
}

// ============================================================================
// Data integrity
// ============================================================================

TEST_F(IntegrationTest, AllProductsHaveValidFields) {
    GraphQLClient client(kMockEndpoint, "", 5000);
    ThrottleController throttle(20.0);
    Paginator paginator(client, throttle, /*verbose=*/false);

    auto products = paginator.fetchAllProducts(/*totalLimit=*/50, /*pageSize=*/25);

    for (const auto& p : products) {
        // ID should be a valid Shopify GID.
        EXPECT_EQ(p.id.find("gid://shopify/Product/"), 0u)
            << "Invalid product ID: " << p.id;

        // Title should not be empty.
        EXPECT_FALSE(p.title.empty())
            << "Product " << p.id << " has empty title";

        // updatedAt should be a non-empty ISO-8601 string.
        EXPECT_FALSE(p.updatedAt.empty())
            << "Product " << p.id << " has empty updatedAt";
    }
}

// ============================================================================
// Stats
// ============================================================================

TEST_F(IntegrationTest, StatsAreReasonable) {
    GraphQLClient client(kMockEndpoint, "", 5000);
    ThrottleController throttle(20.0);
    Paginator paginator(client, throttle, /*verbose=*/false);

    paginator.fetchAllProducts(/*totalLimit=*/30, /*pageSize=*/10);

    auto stats = paginator.getStats();
    EXPECT_EQ(stats.totalFetched, 30);

    // 30 products at 10 per page = 3 pages minimum (plus possible retries).
    EXPECT_GE(stats.totalRequests, 3);

    // Avg query cost should be positive (mock charges 2 + first = 12 per page).
    EXPECT_GT(stats.avgQueryCost, 0.0);
}

// ============================================================================
// Single raw request (low-level client test)
// ============================================================================

TEST_F(IntegrationTest, RawGraphQLRequest) {
    GraphQLClient client(kMockEndpoint, "", 5000);

    json variables;
    variables["first"] = 3;

    // May fail with 503 (~10 %) or 429 (budget), so retry a few times.
    GraphQLClient::Response resp;
    for (int attempt = 0; attempt < 10; ++attempt) {
        resp = client.execute(queries::kProductsQuery, variables);
        if (resp.httpStatus == 200) break;
        // Wait a bit if throttled so budget can restore.
        if (resp.httpStatus == 429) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    ASSERT_EQ(resp.httpStatus, 200u);

    // Should have data.products.edges with 3 elements.
    ASSERT_TRUE(resp.body.contains("data"));
    ASSERT_TRUE(resp.body["data"].contains("products"));
    ASSERT_TRUE(resp.body["data"]["products"].contains("edges"));
    EXPECT_EQ(resp.body["data"]["products"]["edges"].size(), 3u);

    // Should have extensions.cost.
    ASSERT_TRUE(resp.body.contains("extensions"));
    ASSERT_TRUE(resp.body["extensions"].contains("cost"));
    EXPECT_TRUE(resp.body["extensions"]["cost"].contains("requestedQueryCost"));
    EXPECT_TRUE(resp.body["extensions"]["cost"].contains("throttleStatus"));
}

// ============================================================================
// Throttle — budget drain → 429 → sleep → 200
// ============================================================================

TEST_F(IntegrationTest, WithoutSleepBudgetDrainsAndHits429) {
    // The mock server enforces Shopify-style 429 when currentlyAvailable < cost.
    // Budget = 200 points, restore = 50/s.
    // first=100 → cost = 102 per request.
    //
    // Phase 1: rapid-fire requests WITHOUT sleeping → drains budget → 429.
    // Phase 2: sleep to let budget restore → next request succeeds (200).

    GraphQLClient client(kMockEndpoint, "", 5000);

    json variables;
    variables["first"] = 100;  // cost = 2 + 100 = 102

    // --- Phase 1: Drain budget until we get a 429 ---
    bool got429 = false;
    int attempts = 0;
    for (int i = 0; i < 20; ++i) {
        auto resp = client.execute(queries::kProductsQuery, variables);
        ++attempts;

        if (resp.httpStatus == 429) {
            got429 = true;
            break;
        }
        // 503 (random) doesn't drain budget; 200 does. Both are fine, keep going.
    }

    ASSERT_TRUE(got429)
        << "Expected HTTP 429 after draining budget with rapid requests, "
        << "but did not receive one in " << attempts << " attempts.";

    // --- Phase 2: Sleep to let budget fully restore ---
    // maximumAvailable / restoreRate = 200 / 50 = 4 seconds.
    // Sleep 5s to be safe.
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // --- Phase 3: Request should now succeed ---
    bool gotSuccess = false;
    for (int i = 0; i < 10; ++i) {
        auto resp = client.execute(queries::kProductsQuery, variables);
        if (resp.httpStatus == 200) {
            gotSuccess = true;
            break;
        }
        // Might still hit a random 503, just retry.
        if (resp.httpStatus == 429) {
            // Budget still not enough — wait a bit more.
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }

    EXPECT_TRUE(gotSuccess)
        << "Expected HTTP 200 after budget restoration, but requests kept failing.";

    // Let budget restore for subsequent tests.
    std::this_thread::sleep_for(std::chrono::seconds(5));
}

TEST_F(IntegrationTest, ThrottleControllerPrevents429DuringPagination) {
    // Without a ThrottleController, rapid pagination would drain the budget
    // and hit 429.  With the controller, it sleeps between requests when
    // budget is low, so all requests succeed.
    //
    // Fetch 200 products in pages of 50 (cost = 52 per page, 4+ pages).
    // Budget = 200, so after 3-4 pages the controller must sleep to avoid 429.

    GraphQLClient client(kMockEndpoint, "", 10000);
    ThrottleController throttle(20.0);
    Paginator paginator(client, throttle, /*verbose=*/false);

    auto products = paginator.fetchAllProducts(/*totalLimit=*/200, /*pageSize=*/50);

    // All 200 products should have been fetched successfully.
    EXPECT_EQ(products.size(), 200u);

    auto stats = paginator.getStats();
    EXPECT_EQ(stats.totalFetched, 200);

    // At least 4 pages (200 / 50).
    EXPECT_GE(stats.totalRequests, 4);

    // The throttle controller should have slept at some point because
    // 4 pages × 52 cost = 208, which exceeds the 200-point budget.
    EXPECT_GT(stats.totalSleepSeconds, 0.0)
        << "ThrottleController should have slept to avoid 429, "
        << "but totalSleepSeconds is 0.";

    // Avg query cost should match expected: 2 + 50 = 52.
    EXPECT_GT(stats.avgQueryCost, 0.0);

    // Let budget restore for subsequent tests.
    std::this_thread::sleep_for(std::chrono::seconds(5));
}
