/// @file test_util.cpp
/// Unit tests for util.hpp — URL parsing and exponential-backoff computation.

#include "util.hpp"

#include <gtest/gtest.h>
#include <stdexcept>

using namespace graphql_sync;

// ============================================================================
// parseUrl
// ============================================================================

TEST(ParseUrl, HttpWithPort) {
    auto parts = parseUrl("http://localhost:4000/graphql");
    EXPECT_EQ(parts.scheme, "http");
    EXPECT_EQ(parts.host, "localhost");
    EXPECT_EQ(parts.port, "4000");
    EXPECT_EQ(parts.target, "/graphql");
}

TEST(ParseUrl, HttpWithoutPortDefaultsTo80) {
    auto parts = parseUrl("http://example.com/api");
    EXPECT_EQ(parts.scheme, "http");
    EXPECT_EQ(parts.host, "example.com");
    EXPECT_EQ(parts.port, "80");
    EXPECT_EQ(parts.target, "/api");
}

TEST(ParseUrl, HttpsWithoutPortDefaultsTo443) {
    auto parts = parseUrl("https://shop.myshopify.com/admin/api/graphql.json");
    EXPECT_EQ(parts.scheme, "https");
    EXPECT_EQ(parts.host, "shop.myshopify.com");
    EXPECT_EQ(parts.port, "443");
    EXPECT_EQ(parts.target, "/admin/api/graphql.json");
}

TEST(ParseUrl, HttpsWithExplicitPort) {
    auto parts = parseUrl("https://localhost:8443/graphql");
    EXPECT_EQ(parts.scheme, "https");
    EXPECT_EQ(parts.host, "localhost");
    EXPECT_EQ(parts.port, "8443");
    EXPECT_EQ(parts.target, "/graphql");
}

TEST(ParseUrl, UrlWithoutPathDefaultsToSlash) {
    auto parts = parseUrl("http://example.com");
    EXPECT_EQ(parts.scheme, "http");
    EXPECT_EQ(parts.host, "example.com");
    EXPECT_EQ(parts.port, "80");
    EXPECT_EQ(parts.target, "/");
}

TEST(ParseUrl, UrlWithNestedPath) {
    auto parts = parseUrl("http://api.example.com:3000/v2/admin/graphql");
    EXPECT_EQ(parts.scheme, "http");
    EXPECT_EQ(parts.host, "api.example.com");
    EXPECT_EQ(parts.port, "3000");
    EXPECT_EQ(parts.target, "/v2/admin/graphql");
}

TEST(ParseUrl, MissingSchemeThrows) {
    EXPECT_THROW(parseUrl("localhost:4000/graphql"), std::invalid_argument);
}

TEST(ParseUrl, EmptyHostThrows) {
    EXPECT_THROW(parseUrl("http:///graphql"), std::invalid_argument);
}

TEST(ParseUrl, GarbageStringThrows) {
    EXPECT_THROW(parseUrl("not-a-url"), std::invalid_argument);
}

// ============================================================================
// computeBackoffMs
// ============================================================================

TEST(ComputeBackoff, Attempt0InRange200To300) {
    // base=200, jitter in [0,100] => result in [200, 300]
    for (int i = 0; i < 50; ++i) {
        auto ms = computeBackoffMs(0).count();
        EXPECT_GE(ms, 200);
        EXPECT_LE(ms, 300);
    }
}

TEST(ComputeBackoff, Attempt1InRange400To500) {
    // 200 * 2^1 = 400, + jitter => [400, 500]
    for (int i = 0; i < 50; ++i) {
        auto ms = computeBackoffMs(1).count();
        EXPECT_GE(ms, 400);
        EXPECT_LE(ms, 500);
    }
}

TEST(ComputeBackoff, Attempt2InRange800To900) {
    // 200 * 2^2 = 800, + jitter => [800, 900]
    for (int i = 0; i < 50; ++i) {
        auto ms = computeBackoffMs(2).count();
        EXPECT_GE(ms, 800);
        EXPECT_LE(ms, 900);
    }
}

TEST(ComputeBackoff, ClampsToMaxPlusJitter) {
    // 200 * 2^10 = 204800, clamped to 5000, + jitter => [5000, 5100]
    for (int i = 0; i < 50; ++i) {
        auto ms = computeBackoffMs(10).count();
        EXPECT_GE(ms, 5000);
        EXPECT_LE(ms, 5100);
    }
}

TEST(ComputeBackoff, CustomBaseAndMax) {
    // base=100, max=500 at attempt 3: 100*8=800 -> clamped to 500, + jitter => [500, 600]
    for (int i = 0; i < 50; ++i) {
        auto ms = computeBackoffMs(3, 100, 500).count();
        EXPECT_GE(ms, 500);
        EXPECT_LE(ms, 600);
    }
}

TEST(ComputeBackoff, ExponentialGrowth) {
    // Without jitter noise, attempt N should yield roughly base * 2^N.
    // Test that higher attempts yield >= lower attempts' minimums.
    int64_t prevMin = 0;
    for (int attempt = 0; attempt < 5; ++attempt) {
        int64_t minSeen = std::numeric_limits<int64_t>::max();
        for (int i = 0; i < 20; ++i) {
            minSeen = std::min(minSeen, computeBackoffMs(attempt).count());
        }
        EXPECT_GE(minSeen, prevMin)
            << "Attempt " << attempt << " should not be less than attempt "
            << (attempt - 1);
        prevMin = minSeen;
    }
}
