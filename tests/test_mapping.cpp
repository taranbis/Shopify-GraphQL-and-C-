/// @file test_mapping.cpp
/// Unit tests for mapping.hpp — JSON-to-Product parsing and error extraction.

#include "mapping.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace graphql_sync;
using json = nlohmann::json;

// ============================================================================
// parseProductNode
// ============================================================================

TEST(ParseProductNode, FullNode) {
    json node = {
        {"id", "gid://shopify/Product/1001"},
        {"title", "Widget"},
        {"updatedAt", "2024-01-01T00:00:00Z"}
    };

    auto p = parseProductNode(node);
    EXPECT_EQ(p.id, "gid://shopify/Product/1001");
    EXPECT_EQ(p.title, "Widget");
    EXPECT_EQ(p.updatedAt, "2024-01-01T00:00:00Z");
}

TEST(ParseProductNode, MissingFieldsDefaultToEmpty) {
    json node = json::object();

    auto p = parseProductNode(node);
    EXPECT_EQ(p.id, "");
    EXPECT_EQ(p.title, "");
    EXPECT_EQ(p.updatedAt, "");
}

TEST(ParseProductNode, PartialNode) {
    json node = {{"id", "gid://shopify/Product/42"}};

    auto p = parseProductNode(node);
    EXPECT_EQ(p.id, "gid://shopify/Product/42");
    EXPECT_EQ(p.title, "");
    EXPECT_EQ(p.updatedAt, "");
}

TEST(ParseProductNode, ExtraFieldsAreIgnored) {
    json node = {
        {"id", "gid://shopify/Product/99"},
        {"title", "Gadget"},
        {"updatedAt", "2024-06-15T12:00:00Z"},
        {"vendor", "Acme"},
        {"status", "ACTIVE"}
    };

    auto p = parseProductNode(node);
    EXPECT_EQ(p.id, "gid://shopify/Product/99");
    EXPECT_EQ(p.title, "Gadget");
    EXPECT_EQ(p.updatedAt, "2024-06-15T12:00:00Z");
}

// ============================================================================
// parseProductsPage
// ============================================================================

TEST(ParseProductsPage, NormalResponseWithMultipleEdges) {
    json response = {
        {"data", {
            {"products", {
                {"edges", json::array({
                    {{"cursor", "c1"}, {"node", {{"id", "id1"}, {"title", "P1"}, {"updatedAt", "2024-01-01T00:00:00Z"}}}},
                    {{"cursor", "c2"}, {"node", {{"id", "id2"}, {"title", "P2"}, {"updatedAt", "2024-01-02T00:00:00Z"}}}},
                    {{"cursor", "c3"}, {"node", {{"id", "id3"}, {"title", "P3"}, {"updatedAt", "2024-01-03T00:00:00Z"}}}}
                })},
                {"pageInfo", {{"hasNextPage", true}}}
            }}
        }}
    };

    auto result = parseProductsPage(response);
    ASSERT_EQ(result.products.size(), 3u);
    EXPECT_EQ(result.products[0].id, "id1");
    EXPECT_EQ(result.products[0].title, "P1");
    EXPECT_EQ(result.products[1].id, "id2");
    EXPECT_EQ(result.products[2].id, "id3");
    EXPECT_TRUE(result.hasNextPage);
    ASSERT_TRUE(result.lastCursor.has_value());
    EXPECT_EQ(result.lastCursor.value(), "c3");
}

TEST(ParseProductsPage, SingleEdge) {
    json response = {
        {"data", {
            {"products", {
                {"edges", json::array({
                    {{"cursor", "abc123"}, {"node", {
                        {"id", "gid://shopify/Product/999"},
                        {"title", "Solo Product"},
                        {"updatedAt", "2024-06-15T12:00:00Z"}
                    }}}
                })},
                {"pageInfo", {{"hasNextPage", false}}}
            }}
        }}
    };

    auto result = parseProductsPage(response);
    ASSERT_EQ(result.products.size(), 1u);
    EXPECT_EQ(result.products[0].title, "Solo Product");
    EXPECT_FALSE(result.hasNextPage);
    ASSERT_TRUE(result.lastCursor.has_value());
    EXPECT_EQ(result.lastCursor.value(), "abc123");
}

TEST(ParseProductsPage, EmptyEdges) {
    json response = {
        {"data", {
            {"products", {
                {"edges", json::array()},
                {"pageInfo", {{"hasNextPage", false}}}
            }}
        }}
    };

    auto result = parseProductsPage(response);
    EXPECT_TRUE(result.products.empty());
    EXPECT_FALSE(result.hasNextPage);
    EXPECT_FALSE(result.lastCursor.has_value());
}

TEST(ParseProductsPage, MissingDataFieldThrows) {
    json response = {
        {"errors", json::array({{{"message", "something went wrong"}}})}
    };

    EXPECT_THROW(parseProductsPage(response), std::runtime_error);
}

TEST(ParseProductsPage, NullDataReturnsEmpty) {
    json response = {
        {"data", nullptr},
        {"errors", json::array({{{"message", "Access denied"}}})}
    };

    auto result = parseProductsPage(response);
    EXPECT_TRUE(result.products.empty());
    EXPECT_FALSE(result.hasNextPage);
    EXPECT_FALSE(result.lastCursor.has_value());
}

TEST(ParseProductsPage, MissingProductsFieldThrows) {
    json response = {
        {"data", {{"other", "stuff"}}}
    };

    EXPECT_THROW(parseProductsPage(response), std::runtime_error);
}

TEST(ParseProductsPage, HasNextPageFalseOnLastPage) {
    json response = {
        {"data", {
            {"products", {
                {"edges", json::array({
                    {{"cursor", "last"}, {"node", {{"id", "id-last"}, {"title", "Last"}, {"updatedAt", "2024-12-31T23:59:59Z"}}}}
                })},
                {"pageInfo", {{"hasNextPage", false}}}
            }}
        }}
    };

    auto result = parseProductsPage(response);
    ASSERT_EQ(result.products.size(), 1u);
    EXPECT_FALSE(result.hasNextPage);
}

TEST(ParseProductsPage, LastCursorIsFromFinalEdge) {
    // Verifies that lastCursor tracks the *last* edge's cursor, not the first.
    json response = {
        {"data", {
            {"products", {
                {"edges", json::array({
                    {{"cursor", "first-cursor"}, {"node", {{"id", "a"}, {"title", "A"}, {"updatedAt", "2024-01-01T00:00:00Z"}}}},
                    {{"cursor", "middle-cursor"}, {"node", {{"id", "b"}, {"title", "B"}, {"updatedAt", "2024-01-02T00:00:00Z"}}}},
                    {{"cursor", "last-cursor"}, {"node", {{"id", "c"}, {"title", "C"}, {"updatedAt", "2024-01-03T00:00:00Z"}}}}
                })},
                {"pageInfo", {{"hasNextPage", true}}}
            }}
        }}
    };

    auto result = parseProductsPage(response);
    ASSERT_TRUE(result.lastCursor.has_value());
    EXPECT_EQ(result.lastCursor.value(), "last-cursor");
}

// ============================================================================
// extractGraphqlErrors
// ============================================================================

TEST(ExtractGraphqlErrors, NoErrorsField) {
    json response = {
        {"data", {{"products", {}}}}
    };

    auto errors = extractGraphqlErrors(response);
    EXPECT_TRUE(errors.empty());
}

TEST(ExtractGraphqlErrors, EmptyErrorsArray) {
    json response = {
        {"errors", json::array()}
    };

    auto errors = extractGraphqlErrors(response);
    EXPECT_TRUE(errors.empty());
}

TEST(ExtractGraphqlErrors, SingleError) {
    json response = {
        {"errors", json::array({
            {{"message", "Field 'foo' not found"}}
        })}
    };

    auto errors = extractGraphqlErrors(response);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0], "Field 'foo' not found");
}

TEST(ExtractGraphqlErrors, MultipleErrors) {
    json response = {
        {"errors", json::array({
            {{"message", "Field 'foo' not found"}},
            {{"message", "Access denied"}}
        })}
    };

    auto errors = extractGraphqlErrors(response);
    ASSERT_EQ(errors.size(), 2u);
    EXPECT_EQ(errors[0], "Field 'foo' not found");
    EXPECT_EQ(errors[1], "Access denied");
}

TEST(ExtractGraphqlErrors, MissingMessageUsesDefault) {
    json response = {
        {"errors", json::array({
            {{"locations", json::array()}}  // no "message" key
        })}
    };

    auto errors = extractGraphqlErrors(response);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0], "Unknown GraphQL error");
}

TEST(ExtractGraphqlErrors, ErrorsFieldNotAnArrayIsIgnored) {
    json response = {
        {"errors", "some string"}
    };

    auto errors = extractGraphqlErrors(response);
    EXPECT_TRUE(errors.empty());
}

TEST(ExtractGraphqlErrors, EmptyResponse) {
    json response = json::object();

    auto errors = extractGraphqlErrors(response);
    EXPECT_TRUE(errors.empty());
}
