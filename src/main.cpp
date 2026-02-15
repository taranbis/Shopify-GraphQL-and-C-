#include "graphql_client.hpp"
#include "models.hpp"
#include "pagination.hpp"
#include "throttle.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>

struct Config {
    std::string endpoint  = "http://localhost:4000/graphql";
    int         total     = 750;
    int         pageSize  = 100;
    int         timeoutMs = 5000;
    bool        verbose   = true;
};

static void printUsage() {
    std::cout
        << "Usage: graphql_sync [options]\n\n"
        << "Options:\n"
        << "  --endpoint URL   GraphQL endpoint          "
           "(default: http://localhost:4000/graphql)\n"
        << "  --total N        Total products to fetch    (default: 250)\n"
        << "  --page-size N    Products per request page  (default: 50)\n"
        << "  --timeout-ms N   HTTP timeout in ms         (default: 5000)\n"
        << "  --verbose        Enable verbose diagnostics\n"
        << "  --help, -h       Show this message\n";
}

static Config parseArgs(int argc, char* argv[]) {
    Config cfg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if ((arg == "--endpoint") && i + 1 < argc) {
            cfg.endpoint = argv[++i];
        } else if ((arg == "--total") && i + 1 < argc) {
            cfg.total = std::stoi(argv[++i]);
        } else if ((arg == "--page-size") && i + 1 < argc) {
            cfg.pageSize = std::stoi(argv[++i]);
        } else if ((arg == "--timeout-ms") && i + 1 < argc) {
            cfg.timeoutMs = std::stoi(argv[++i]);
        } else if (arg == "--verbose") {
            cfg.verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            std::exit(0);
        } else {
            std::cerr << "Unknown argument: " << arg << "\n\n";
            printUsage();
            std::exit(1);
        }
    }
    return cfg;
}

int main(int argc, char* argv[]) {
    try {
        Config cfg = parseArgs(argc, argv);

        std::cout
            << "=== graphql_sync ===\n"
            << "Endpoint:   " << cfg.endpoint  << "\n"
            << "Total:      " << cfg.total     << "\n"
            << "Page size:  " << cfg.pageSize  << "\n"
            << "Timeout:    " << cfg.timeoutMs << " ms\n"
            << "Verbose:    " << (cfg.verbose ? "yes" : "no") << "\n"
            << "====================\n\n";

        graphql_sync::GraphQLClient client(cfg.endpoint, "", cfg.timeoutMs);
        client.setVerbose(cfg.verbose);
        graphql_sync::ThrottleController throttle(/*safetyMargin=*/20.0);
        graphql_sync::Paginator paginator(client, throttle, cfg.verbose);

        const auto products = paginator.fetchAllProducts(cfg.total, cfg.pageSize);

        std::cout << "\n--- Fetched Products (" << products.size()
                  << ") ---\n";
        for (std::size_t i = 0; i < products.size(); ++i) {
            const auto& p = products[i];
            std::cout << std::setw(4) << (i + 1) << "  "
                      << std::left << std::setw(36) << p.id << "  "
                      << std::setw(40) << p.title << "  "
                      << p.updatedAt << "\n";
        }

        const auto stats = paginator.getStats();
        std::cout
            << "\n=== Summary Report ===\n"
            << "Total fetched:       " << stats.totalFetched      << "\n"
            << "Total requests:      " << stats.totalRequests     << "\n"
            << "Total retries:       " << stats.totalRetries      << "\n"
            << "Total sleep (s):     " << std::fixed << std::setprecision(2)
                                       << stats.totalSleepSeconds << "\n"
            << "Avg query cost:      " << std::fixed << std::setprecision(2)
                                       << stats.avgQueryCost      << "\n"
            << "======================\n";

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
}
