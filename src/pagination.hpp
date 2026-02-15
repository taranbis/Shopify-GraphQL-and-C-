#pragma once

#include "graphql_client.hpp"
#include "models.hpp"
#include "throttle.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace graphql_sync {

/// Orchestrates cursor-based pagination, retry logic, and throttle control.
class Paginator {
public:
    struct Stats {
        int    totalFetched      = 0;
        int    totalRequests     = 0;
        int    totalRetries      = 0;
        double totalSleepSeconds = 0.0;
        double avgQueryCost      = 0.0;
    };

    Paginator(GraphQLClient& client,
              ThrottleController& throttle,
              bool verbose = false);

    /// Fetch up to @p totalLimit products in pages of @p pageSize.
    std::vector<Product> fetchAllProducts(int totalLimit, int pageSize);

    Stats getStats() const { return mStats; }

private:
    static constexpr int kMaxAttempts = 6;

    GraphQLClient&      mClient;
    ThrottleController& mThrottle;
    bool                mVerbose;
    Stats               mStats{};

    /// Execute a query with exponential-backoff retry on transient failures.
    nlohmann::json executeWithRetry(const std::string& query,
                                    const nlohmann::json& variables);

    static bool isRetryableStatus(unsigned int status);
};

} // namespace graphql_sync
