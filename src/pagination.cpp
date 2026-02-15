#include "pagination.hpp"
#include "mapping.hpp"
#include "queries.hpp"
#include "util.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>

namespace graphql_sync {

Paginator::Paginator(GraphQLClient& client,
                     ThrottleController& throttle,
                     bool verbose)
    : mClient(client)
    , mThrottle(throttle)
    , mVerbose(verbose) {}

// ---------------------------------------------------------------------------
// Public: paginated fetch
// ---------------------------------------------------------------------------

std::vector<Product> Paginator::fetchAllProducts(int totalLimit, int pageSize) 
{
    std::vector<Product> allProducts;
    std::optional<std::string> cursor;

    while (static_cast<int>(allProducts.size()) < totalLimit) {
        // --- throttle gate ---
        mThrottle.maybeSleepBeforeNextRequest();

        const int remaining  = totalLimit - static_cast<int>(allProducts.size());
        const int fetchCount = std::min(pageSize, remaining);

        nlohmann::json variables;
        variables["first"] = fetchCount;
        if (cursor.has_value()) {
            variables["after"] = cursor.value();
        }

        if (mVerbose) {
            std::cerr << "[Paginator] Fetching page: first=" << fetchCount;
            if (cursor) std::cerr << ", after=" << *cursor;
            std::cerr << "\n";
        }

        nlohmann::json response;
        try {
            response = executeWithRetry(queries::kProductsQuery, variables);
        } catch (const std::exception& e) {
            std::cerr << "[Paginator] Fatal error after retries: "
                      << e.what() << "\n";
            break;
        }

        ++mStats.totalRequests;

        // calculate the cost - we need to know if we throttle next request
        mThrottle.observeResponse(response);

        const auto errors = extractGraphqlErrors(response);
        if (!errors.empty()) {
            std::cerr << "[Paginator] GraphQL errors:\n";
            for (const auto& err : errors) {
                std::cerr << "  - " << err << "\n";
            }
            if (!response.contains("data") || response["data"].is_null()) {
                std::cerr << "[Paginator] No data returned; stopping.\n";
                break;
            }
        }

        PageResult page;
        try {
            page = parseProductsPage(response);
        } catch (const std::exception& e) {
            std::cerr << "[Paginator] Failed to parse page: "
                      << e.what() << "\n";
            break;
        }

        if (page.products.empty()) {
            if (mVerbose) {
                std::cerr << "[Paginator] Empty page received; stopping.\n";
            }
            break;
        }

        allProducts.insert(allProducts.end(),
                           page.products.begin(),
                           page.products.end());

        if (mVerbose) {
            std::cerr << "[Paginator] Got " << page.products.size()
                      << " products (total so far: "
                      << allProducts.size() << ")\n";
        }

        if (!page.hasNextPage) {
            if (mVerbose) {
                std::cerr << "[Paginator] No more pages.\n";
            }
            break;
        }

        cursor = page.lastCursor;
    }

    // --- populate stats ---
    mStats.totalFetched      = static_cast<int>(allProducts.size());
    mStats.totalSleepSeconds = mThrottle.totalSleepSeconds();
    mStats.avgQueryCost      = mThrottle.avgQueryCost();

    return allProducts;
}

// ---------------------------------------------------------------------------
// Private: retry wrapper
// ---------------------------------------------------------------------------

nlohmann::json Paginator::executeWithRetry( const std::string& query,
                                            const nlohmann::json& variables) 
{
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        try {
            const auto resp = mClient.execute(query, variables);

            if (isRetryableStatus(resp.httpStatus)) {
                ++mStats.totalRetries;
                auto backoff = computeBackoffMs(attempt);

                if (mVerbose) {
                    std::cerr << "[Retry] HTTP " << resp.httpStatus
                              << " — attempt " << (attempt + 1) << "/"
                              << kMaxAttempts << ", backoff "
                              << backoff.count() << " ms\n";
                }

                if (attempt == kMaxAttempts - 1) {
                    throw std::runtime_error(
                        "Max retries exceeded.  Last HTTP status: " +
                        std::to_string(resp.httpStatus));
                }

                std::this_thread::sleep_for(backoff);
                continue;
            }

            return resp.body;

        } catch (const std::runtime_error& e) {
            // Re-throw our own "max retries" sentinel.
            std::string msg = e.what();
            if (msg.find("Max retries exceeded") != std::string::npos) {
                throw;
            }

            // Network / timeout — retryable.
            ++mStats.totalRetries;

            if (mVerbose) {
                std::cerr << "[Retry] Network error: " << e.what()
                          << " — attempt " << (attempt + 1) << "/"
                          << kMaxAttempts << "\n";
            }

            if (attempt == kMaxAttempts - 1) {
                throw std::runtime_error(
                    std::string("Max retries exceeded.  Last error: ") +
                    e.what());
            }

            std::this_thread::sleep_for(computeBackoffMs(attempt));
        }
    }

    throw std::runtime_error("Max retries exceeded (unreachable)");
}

bool Paginator::isRetryableStatus(unsigned int status) {
    return status == 429 || status >= 500;
}

} // namespace graphql_sync
