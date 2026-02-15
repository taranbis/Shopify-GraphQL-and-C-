#pragma once

#include "models.hpp"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace graphql_sync {

/// Result of parsing one page of the products connection.
struct PageResult {
    std::vector<Product>          products;
    std::optional<std::string>    lastCursor;
    bool                          hasNextPage = false;
};

/// Parse a full GraphQL products-connection response body into a PageResult.
/// Throws std::runtime_error if the expected shape is missing.
PageResult parseProductsPage(const nlohmann::json& responseBody);

/// Map a single product JSON node into a Product struct.
Product parseProductNode(const nlohmann::json& node);

/// Return human-readable error messages from a GraphQL response (may be empty).
std::vector<std::string> extractGraphqlErrors(const nlohmann::json& responseBody);

} // namespace graphql_sync
