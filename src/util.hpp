#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace graphql_sync {

/// Decomposed URL components.
struct UrlParts {
    std::string scheme;   // "http" or "https"
    std::string host;
    std::string port;     // "80", "443", "4000", etc.
    std::string target;   // path component (e.g. "/graphql")
};

/// Parse an HTTP(S) URL into its components.
/// Throws std::invalid_argument on malformed input.
UrlParts parseUrl(const std::string& url);

/// Compute exponential-backoff delay with random jitter.
/// attempt is 0-based.  Clamped to [baseMs .. maxMs] before jitter.
std::chrono::milliseconds computeBackoffMs(int attempt,
                                           int64_t baseMs = 200,
                                           int64_t maxMs  = 5000);

} // namespace graphql_sync
