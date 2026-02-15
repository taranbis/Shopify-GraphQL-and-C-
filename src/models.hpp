#pragma once

#include <string>

namespace graphql_sync {

/// Mirrors Shopify Product (subset of fields used in sync).
struct Product {
    std::string id;          // e.g. "gid://shopify/Product/1042"
    std::string title;
    std::string updatedAt;   // ISO-8601
};

} // namespace graphql_sync
