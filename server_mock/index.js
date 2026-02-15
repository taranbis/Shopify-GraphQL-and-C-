/**
 * Mock Shopify Admin GraphQL Server
 *
 * Provides:
 *   - products(first, after) with cursor-based pagination
 *   - Shopify-style GIDs  (gid://shopify/Product/<n>)
 *   - extensions.cost / throttleStatus on every response
 *   - Shopify-style 429 throttle enforcement when budget is exhausted
 *   - Occasional simulated 503 for retry testing (~10 %)
 *   - GraphQL-level errors for malformed cursors / invalid args
 */

const express = require("express");
const { graphql, buildSchema } = require("graphql");

const PORT = process.env.PORT || 4000;

// ---------------------------------------------------------------------------
// 1.  Generate mock product catalogue
// ---------------------------------------------------------------------------

const TOTAL_PRODUCTS = 1000;
const PRODUCT_ADJECTIVES = [
  "Widget",
  "Gadget",
  "Doohickey",
  "Thingamajig",
  "Gizmo",
];

const products = Array.from({ length: TOTAL_PRODUCTS }, (_, i) => {
  const num = i + 1;
  const date = new Date(2024, 0, 1);
  date.setHours(date.getHours() + num);
  return {
    id: `gid://shopify/Product/${1000 + num}`,
    title: `Product ${num} - ${PRODUCT_ADJECTIVES[i % PRODUCT_ADJECTIVES.length]}`,
    updatedAt: date.toISOString(),
  };
});

// ---------------------------------------------------------------------------
// 2.  Cost / throttle state  (Shopify-like)
// ---------------------------------------------------------------------------

const throttle = {
  maximumAvailable: 200,
  currentlyAvailable: 200,
  restoreRate: 50, // points per second
  lastRequestTime: Date.now(),
};

function computeRequestCost(first) {
  // Shopify charges:  base (2) + first * per-node (1)
  return 2 + first;
}

/// Restore budget points based on elapsed time since last request.
function restoreBudget() {
  const now = Date.now();
  const elapsedSec = (now - throttle.lastRequestTime) / 1000;
  throttle.currentlyAvailable = Math.min(
    throttle.maximumAvailable,
    throttle.currentlyAvailable + elapsedSec * throttle.restoreRate
  );
  throttle.lastRequestTime = now;
}

/// Build the extensions.cost object for the response.
function buildCostInfo(requestedCost, actualCost) {
  return {
    requestedQueryCost: requestedCost,
    actualQueryCost: actualCost !== undefined ? actualCost : requestedCost,
    throttleStatus: {
      maximumAvailable: throttle.maximumAvailable,
      currentlyAvailable:
        Math.round(throttle.currentlyAvailable * 100) / 100,
      restoreRate: throttle.restoreRate,
    },
  };
}

// ---------------------------------------------------------------------------
// 3.  Cursor helpers
// ---------------------------------------------------------------------------

function encodeCursor(index) {
  return Buffer.from(`cursor:${index}`).toString("base64");
}

function decodeCursor(cursor) {
  try {
    const decoded = Buffer.from(cursor, "base64").toString("utf-8");
    const match = decoded.match(/^cursor:(\d+)$/);
    if (!match) return null;
    return parseInt(match[1], 10);
  } catch {
    return null;
  }
}

// ---------------------------------------------------------------------------
// 4.  GraphQL schema + resolvers
// ---------------------------------------------------------------------------

const schema = buildSchema(`
  type Query {
    products(first: Int!, after: String): ProductConnection!
  }

  type ProductConnection {
    edges: [ProductEdge!]!
    pageInfo: PageInfo!
  }

  type ProductEdge {
    cursor: String!
    node: Product!
  }

  type Product {
    id: ID!
    title: String!
    updatedAt: String!
  }

  type PageInfo {
    hasNextPage: Boolean!
  }
`);

const root = {
  products: ({ first, after }) => {
    // Validate first.
    if (first < 1 || first > 250) {
      throw new Error(`'first' must be between 1 and 250, got ${first}`);
    }

    // Decode cursor -> starting index.
    let startIndex = 0;
    if (after != null) {
      const idx = decodeCursor(after);
      if (idx === null) {
        throw new Error(`Invalid cursor: '${after}'`);
      }
      startIndex = idx + 1;
    }

    const slice = products.slice(startIndex, startIndex + first);
    const edges = slice.map((product, i) => ({
      cursor: encodeCursor(startIndex + i),
      node: product,
    }));

    const hasNextPage = startIndex + first < products.length;

    return {
      edges,
      pageInfo: { hasNextPage },
    };
  },
};

// ---------------------------------------------------------------------------
// 5.  Express application
// ---------------------------------------------------------------------------

const app = express();
app.use(express.json());

app.post("/graphql", async (req, res) => {
  const { query, variables } = req.body;

  if (!query) {
    return res.status(400).json({
      errors: [{ message: "Missing 'query' in request body" }],
    });
  }

  // ~10 % chance of a transient 503 (exercises retry logic).
  if (Math.random() < 0.1) {
    console.log("[mock] Simulating 503 Service Unavailable");
    return res.status(503).json({
      errors: [{ message: "Service temporarily unavailable" }],
    });
  }

  // Compute cost and restore budget based on elapsed time.
  const first = (variables && variables.first) || 10;
  const requestedCost = computeRequestCost(first);
  restoreBudget();

  // Enforce throttle: reject if budget is insufficient (Shopify-style 429).
  if (throttle.currentlyAvailable < requestedCost) {
    console.log(
      `[mock] 429 Throttled (available=${throttle.currentlyAvailable.toFixed(
        2
      )}, cost=${requestedCost})`
    );
    return res.status(429).json({
      errors: [{ message: "Throttled" }],
      extensions: { cost: buildCostInfo(requestedCost, 0) },
    });
  }

  // Deduct the cost of this request.
  throttle.currentlyAvailable = Math.max(
    0,
    throttle.currentlyAvailable - requestedCost
  );
  const costInfo = buildCostInfo(requestedCost);

  try {
    const result = await graphql({
      schema,
      source: query,
      rootValue: root,
      variableValues: variables || {},
    });

    // Shopify-style: always include extensions.cost.
    res.json({
      ...result,
      extensions: { cost: costInfo },
    });
  } catch (err) {
    res.status(500).json({
      errors: [{ message: err.message }],
      extensions: { cost: costInfo },
    });
  }
});

// Health / info endpoint.
app.get("/graphql", (_req, res) => {
  res.json({
    message: "Shopify Admin GraphQL Mock Server",
    products: products.length,
    hint: "POST { query, variables } to this endpoint.",
  });
});

// ---------------------------------------------------------------------------
// 6.  Start
// ---------------------------------------------------------------------------

app.listen(PORT, () => {
  console.log(
    `Mock Shopify GraphQL server running at http://localhost:${PORT}/graphql`
  );
  console.log(`  Products : ${products.length}`);
  console.log(
    `  Throttle : max=${throttle.maximumAvailable}  restore=${throttle.restoreRate}/s`
  );
});
