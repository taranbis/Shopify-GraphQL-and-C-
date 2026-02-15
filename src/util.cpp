#include "util.hpp"

#include <algorithm>
#include <chrono>
#include <random>
#include <stdexcept>

namespace graphql_sync {

UrlParts parseUrl(const std::string& url) {
    UrlParts parts;

    // --- scheme ---
    auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) {
        throw std::invalid_argument("Invalid URL (missing scheme): " + url);
    }
    parts.scheme = url.substr(0, schemeEnd);

    // --- authority (host[:port]) ---
    auto hostStart = schemeEnd + 3;
    auto pathStart = url.find('/', hostStart);

    std::string authority;
    if (pathStart == std::string::npos) {
        authority    = url.substr(hostStart);
        parts.target = "/";
    } else {
        authority    = url.substr(hostStart, pathStart - hostStart);
        parts.target = url.substr(pathStart);
    }

    // --- host / port ---
    auto colon = authority.find(':');
    if (colon == std::string::npos) {
        parts.host = authority;
        parts.port = (parts.scheme == "https") ? "443" : "80";
    } else {
        parts.host = authority.substr(0, colon);
        parts.port = authority.substr(colon + 1);
    }

    if (parts.host.empty()) {
        throw std::invalid_argument("Invalid URL (empty host): " + url);
    }
    return parts;
}

std::chrono::milliseconds computeBackoffMs(int attempt, int64_t baseMs, int64_t maxMs) {
    // Exponential: base * 2^attempt, clamped to maxMs.
    int64_t backoff = baseMs * (int64_t{1} << attempt);
    backoff = std::min(backoff, maxMs);

    // Jitter: uniform random in [0, 100] ms.
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int64_t> jitter(0, 100);
    backoff += jitter(rng);

    return std::chrono::milliseconds(backoff);
}

} // namespace graphql_sync
