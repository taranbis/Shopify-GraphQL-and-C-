#include "mapping.hpp"

#include <stdexcept>

namespace graphql_sync {

Product parseProductNode(const nlohmann::json& node) {
    Product p;
    p.id        = node.value("id", "");
    p.title     = node.value("title", "");
    p.updatedAt = node.value("updatedAt", "");
    return p;
}

PageResult parseProductsPage(const nlohmann::json& responseBody) {
    PageResult result;

    if (!responseBody.contains("data")) {
        throw std::runtime_error("Response missing 'data' field");
    }

    const auto& data = responseBody["data"];
    if (data.is_null()) {
        // data can be null when top-level errors exist.
        return result;
    }

    if (!data.contains("products")) {
        throw std::runtime_error("Response missing 'data.products' field");
    }
    const auto& products = data["products"];

    // --- edges ---
    if (products.contains("edges") && products["edges"].is_array()) {
        for (const auto& edge : products["edges"]) {
            if (edge.contains("node")) {
                result.products.push_back(parseProductNode(edge["node"]));
            }
            if (edge.contains("cursor")) {
                result.lastCursor = edge["cursor"].get<std::string>();
            }
        }
    }

    // --- pageInfo ---
    if (products.contains("pageInfo")) {
        result.hasNextPage =
            products["pageInfo"].value("hasNextPage", false);
    }

    return result;
}

std::vector<std::string>
extractGraphqlErrors(const nlohmann::json& responseBody) {
    std::vector<std::string> errors;

    if (responseBody.contains("errors") &&
        responseBody["errors"].is_array()) {
        for (const auto& err : responseBody["errors"]) {
            errors.push_back(err.value("message", "Unknown GraphQL error"));
        }
    }
    return errors;
}

} // namespace graphql_sync
