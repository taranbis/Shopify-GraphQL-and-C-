#pragma once

#include <string>

namespace graphql_sync {
namespace queries {

/// Shopify-style products query with cursor-based pagination.
/// Variables: $first (Int!), $after (String, nullable).
inline const std::string kProductsQuery = R"(
query FetchProducts($first: Int!, $after: String) {
  products(first: $first, after: $after) {
    edges {
      cursor
      node {
        id
        title
        updatedAt
      }
    }
    pageInfo {
      hasNextPage
    }
  }
}
)";

} // namespace queries
} // namespace graphql_sync
