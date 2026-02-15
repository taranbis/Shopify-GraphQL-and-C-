# Mock Shopify GraphQL Server

Lightweight Express + `graphql-js` server that mimics the Shopify Admin GraphQL API response shape.

See the **top-level README** for full build / run instructions and example output.

## Quick start

```bash
npm install
npm start          # listens on http://localhost:4000/graphql
```

## What it provides

| Feature | Details |
|---|---|
| Cursor pagination | `products(first, after)` with base-64 cursors |
| Shopify GIDs | `gid://shopify/Product/<n>` |
| Cost extensions | `extensions.cost.throttleStatus` on every response |
| Error simulation | ~3 % random 503; invalid-cursor GraphQL errors |
