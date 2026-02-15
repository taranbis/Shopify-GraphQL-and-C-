# graphql_sync — C++ GraphQL Client with Shopify-Style Pagination & Throttling

A production-style portfolio project demonstrating how to migrate a C++ Shopify REST integration to the **Shopify Admin GraphQL API**, covering:

- **Cursor-based pagination** (`products(first:, after:)` / `pageInfo.hasNextPage`)
- **Cost-based rate limiting** using `extensions.cost.throttleStatus`
- **Robust retries** with exponential back-off + jitter on HTTP 429, 5xx, and timeouts
- **GraphQL error handling** — HTTP 200 responses that contain `"errors": [...]`

Everything runs locally against a bundled Node.js mock server — no Shopify credentials needed.

---

## Repository layout

```
/
├── CMakeLists.txt              # C++20 CMake build
├── README.md                   # ← you are here
├── src/
│   ├── main.cpp                # CLI entry point
│   ├── graphql_client.hpp/cpp  # Boost.Beast HTTP(S) GraphQL client
│   ├── throttle.hpp/cpp        # Cost-aware sleep controller
│   ├── pagination.hpp/cpp      # Cursor pagination + retry orchestrator
│   ├── mapping.hpp/cpp         # JSON → C++ struct mapping
│   ├── models.hpp              # Product struct
│   ├── queries.hpp             # GraphQL query strings
│   └── util.hpp/cpp            # URL parsing, sleep, backoff helpers
└── server_mock/
    ├── package.json
    ├── index.js                # Express + graphql-js mock server
    └── README.md
```

---

## Prerequisites

| Tool | Version |
|---|---|
| C++ compiler | C++20 (MSVC 2019+, GCC 10+, Clang 12+) |
| CMake | ≥ 3.16 |
| Boost | ≥ 1.75 (Beast / Asio / System) |
| Node.js | ≥ 16 |
| OpenSSL | _optional_ — enables HTTPS support |

### Installing Boost (Windows / vcpkg)

```powershell
vcpkg install boost-beast boost-asio boost-system
```

### Installing Boost (Linux / macOS)

```bash
# Ubuntu / Debian
sudo apt install libboost-all-dev

# macOS (Homebrew)
brew install boost
```

---

## 1. Start the mock server

```bash
cd server_mock
npm install
npm start
```

You should see:

```
Mock Shopify GraphQL server running at http://localhost:4000/graphql
  Products : 500
  Throttle : max=1000  restore=50/s
```

---

## 2. Build the C++ client

```bash
# From the repository root:
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

> **Windows with vcpkg:**  
> Pass `-DCMAKE_TOOLCHAIN_FILE=[vcpkg root]/scripts/buildsystems/vcpkg.cmake` to the first cmake command.

### Conan (Windows)

If you use Conan, run `conan install` for each configuration you need. **CMakeUserPresets.json** includes the Conan-generated presets and adds **conan-debug** (Debug build with its own Conan install). Available build presets:

| Preset          | Description |
|-----------------|-------------|
| `conan-release` | Release (from Conan generators in `build/`) |
| `conan-debug`   | Debug (uses separate Conan install in `build_debug/`) |

**One-time setup:**

1. **Release** (you likely have this already):
   ```powershell
   conan install . --output-folder=build --build=missing
   ```
   (Or with short form: `conan install . -of=build --build=missing`. Omit `-s build_type=Release` if your default profile is already Release.)
2. **Debug** (required for the `conan-debug` preset so headers like `nlohmann/json.hpp` are found):
   ```powershell
   conan install . -s build_type=Debug --output-folder=build_debug --build=missing
   ```

**Then select a Conan preset:** In Visual Studio, use the CMake preset dropdown (toolbar or **Project** → **CMake Settings**) and choose **conan-debug** or **conan-release** — **not** "x64 Debug" or "x64 Release". Those defaults do not use the Conan toolchain, so Boost and other dependencies will not be found.

Debug builds use `build_debug/build/`; Release uses `build/build/`.

**Troubleshooting — "Could NOT find Boost" / "Could NOT find nlohmann_json":**  
You are not using a Conan preset. Select **conan-release** or **conan-debug** from the CMake preset dropdown, then **Project** → **Delete Cache and Reconfigure** (or delete the build folder and configure again).

---

## 3. Run

**Command line (Windows):**
```powershell
.\build\build\Release\graphql_sync.exe --endpoint http://localhost:4000/graphql --total 250 --page-size 50 --timeout-ms 5000
```

**Command line (Linux/macOS):**
```bash
./build/graphql_sync \
  --endpoint http://localhost:4000/graphql \
  --total 250 \
  --page-size 50 \
  --timeout-ms 5000
```

Add `--verbose` for per-request diagnostics.

### Running from Visual Studio 2022 with arguments

1. **Startup dropdown:** Set the **Startup Item** to **graphql_sync (mock server args)** (or **graphql_sync (verbose)**). The repo includes **launch.vs.json** with these profiles and the README arguments; they appear in the dropdown when you open the folder as a CMake project.
2. **Or set arguments manually:** Right‑click the `graphql_sync` target → **Debug and Launch Settings** → edit the config, or: **Project** → **graphql_sync Properties** → **Configuration Properties** → **Debugging** → **Command Arguments** and set:
   ```
   --endpoint http://localhost:4000/graphql --total 250 --page-size 50 --timeout-ms 5000
   ```
3. Start the mock server (see §1), then press **F5** or **Ctrl+F5** to run.

### CLI options

| Flag | Default | Description |
|---|---|---|
| `--endpoint URL` | `http://localhost:4000/graphql` | GraphQL endpoint |
| `--total N` | `250` | Max products to fetch |
| `--page-size N` | `50` | Products per page (`first`) |
| `--timeout-ms N` | `5000` | HTTP timeout (ms) |
| `--verbose` | off | Print per-request debug info |

---

## Example output

```
=== graphql_sync ===
Endpoint:   http://localhost:4000/graphql
Total:      250
Page size:  50
Timeout:    5000 ms
Verbose:    no
====================

--- Fetched Products (250) ---
   1  gid://shopify/Product/1001            Product 1 - Widget                        2024-01-01T01:00:00.000Z
   2  gid://shopify/Product/1002            Product 2 - Gadget                        2024-01-01T02:00:00.000Z
   3  gid://shopify/Product/1003            Product 3 - Doohickey                     2024-01-01T03:00:00.000Z
   ...
 250  gid://shopify/Product/1250            Product 250 - Gizmo                       2024-01-11T10:00:00.000Z

=== Summary Report ===
Total fetched:       250
Total requests:      5
Total retries:       0
Total sleep (s):     0.00
Avg query cost:      52.00
======================
```

---

## Key design decisions

### GraphQL errors inside HTTP 200

Shopify (and GraphQL in general) may return a 200 OK whose body contains
`"errors": [...]`. The client checks for this **before** treating the data as
valid, and prints user-friendly messages.

### Cursor pagination

Each page returns `edges[].cursor` and `pageInfo.hasNextPage`. The last cursor
from the page is passed as the `after` variable in the next request. This
continues until the requested total is reached or no more pages remain.

### Cost-based throttling

Every Shopify GraphQL response includes `extensions.cost.throttleStatus`:

```json
{
  "extensions": {
    "cost": {
      "requestedQueryCost": 52,
      "actualQueryCost": 52,
      "throttleStatus": {
        "maximumAvailable": 1000,
        "currentlyAvailable": 844,
        "restoreRate": 50
      }
    }
  }
}
```

`ThrottleController` reads these fields and, before the next request, sleeps
exactly long enough for the bucket to restore to `requestedQueryCost + margin`.

### Retry policy

| Condition | Action |
|---|---|
| HTTP 429 (rate limit) | Retry with back-off |
| HTTP 5xx | Retry with back-off |
| Network timeout | Retry with back-off |
| Other errors | Fail immediately |

Back-off schedule: **200 ms → 400 → 800 → 1600 → 3200 → 5000 ms** (capped),
with ±100 ms random jitter. Maximum **6 attempts**.

### SSL / HTTPS

If OpenSSL is detected at build time, HTTPS endpoints are supported
transparently (Boost.Beast SSL stream + SNI). Without OpenSSL the build
succeeds but only HTTP endpoints work. The mock server uses plain HTTP.

---

## Coding style

| Element | Convention | Example |
|---|---|---|
| Classes / structs | PascalCase | `GraphQLClient`, `PageResult` |
| Functions | camelCase | `parseUrl()`, `fetchAllProducts()` |
| Local variables | camelCase | `pageSize`, `allProducts` |
| Class members | `m` prefix + PascalCase | `mHost`, `mVerbose`, `mTimeoutMs` |
| Constants | `k` prefix + PascalCase | `kMaxAttempts`, `kProductsQuery` |

---

## License

This project is provided as a portfolio demonstration and is available under
the MIT License.
