#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace graphql_sync {

/// Low-level GraphQL HTTP client built on Boost.Beast.
/// Sends a JSON-encoded POST and returns the response ready to be parse.
class GraphQLClient {
public:
    struct Response {
        unsigned int httpStatus = 0;
        nlohmann::json body;
    };

    /// @param endpoint     Full URL, e.g. "http://localhost:4000/graphql"
    /// @param accessToken  Optional Shopify access token (sent as X-Shopify-Access-Token)
    /// @param timeoutMs    Per-operation timeout in milliseconds
    GraphQLClient(const std::string& endpoint,
                  const std::string& accessToken = "",
                  int timeoutMs = 5000);

    /// Execute a GraphQL query/mutation.
    /// @throws std::runtime_error on network / timeout / parse errors.
    Response execute(const std::string& query,
                     const nlohmann::json& variables = nlohmann::json::object());

    void setVerbose(bool v) { mVerbose = v; }

private:
    std::string mHost;
    std::string mPort;
    std::string mTarget;
    std::string mAccessToken;
    int         mTimeoutMs;
    bool        mVerbose = false;
    bool        mUseSsl  = false;

    Response doHttpRequest(const std::string& requestBody);
    Response doHttpsRequest(const std::string& requestBody);
};

} // namespace graphql_sync
