#include "graphql_client.hpp"
#include "util.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>

#ifdef GRAPHQL_SYNC_HAS_SSL
#include <boost/beast/ssl.hpp>
#include <boost/asio/ssl.hpp>
#endif

#include <iostream>
#include <stdexcept>

namespace beast = boost::beast;
namespace http  = beast::http;
namespace net   = boost::asio;
using tcp       = net::ip::tcp;

namespace graphql_sync {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

GraphQLClient::GraphQLClient(const std::string& endpoint,
                             const std::string& accessToken,
                             int timeoutMs)
    : mAccessToken(accessToken)
    , mTimeoutMs(timeoutMs)
{
    auto parts = parseUrl(endpoint);
    mHost   = parts.host;
    mPort   = parts.port;
    mTarget = parts.target;
    mUseSsl = (parts.scheme == "https");

    if (mUseSsl) {
#ifndef GRAPHQL_SYNC_HAS_SSL
        throw std::runtime_error(
            "HTTPS endpoint requested but SSL support was not compiled in. "
            "Rebuild with OpenSSL to enable HTTPS.");
#endif
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

GraphQLClient::Response
GraphQLClient::execute(const std::string& query,
                       const nlohmann::json& variables)
{
    nlohmann::json payload;
    payload["query"] = query;
    if (!variables.empty()) {
        payload["variables"] = variables;
    }

    std::string body = payload.dump();

    if (mVerbose) {
        std::cerr << "[GraphQLClient] POST " << mHost << ":" << mPort
                  << mTarget << "\n";
        if (body.size() <= 300) {
            std::cerr << "[GraphQLClient] Body: " << body << "\n";
        } else {
            std::cerr << "[GraphQLClient] Body: " << body.substr(0, 300)
                      << " ...(truncated)\n";
        }
    }

    return mUseSsl ? doHttpsRequest(body) : doHttpRequest(body);
}

// ---------------------------------------------------------------------------
// Plain HTTP
// ---------------------------------------------------------------------------

GraphQLClient::Response
GraphQLClient::doHttpRequest(const std::string& requestBody)
{
    net::io_context ioc;
    tcp::resolver   resolver(ioc);
    beast::tcp_stream stream(ioc);

    // Resolve + connect with timeout.
    auto const results = resolver.resolve(mHost, mPort);
    stream.expires_after(std::chrono::milliseconds(mTimeoutMs));
    stream.connect(results);

    // Build request.
    http::request<http::string_body> req{http::verb::post, mTarget, 11};
    req.set(http::field::host, mHost);
    req.set(http::field::content_type, "application/json");
    req.set(http::field::accept, "application/json");
    req.set(http::field::user_agent, "graphql_sync/1.0");
    if (!mAccessToken.empty()) {
        req.set("X-Shopify-Access-Token", mAccessToken);
    }
    req.body() = requestBody;
    req.prepare_payload();

    // Send.
    http::write(stream, req);

    // Receive.
    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    stream.expires_after(std::chrono::milliseconds(mTimeoutMs));
    http::read(stream, buffer, res);

    // Parse JSON body.
    Response response;
    response.httpStatus = res.result_int();
    try {
        response.body = nlohmann::json::parse(res.body());
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(
            std::string("Failed to parse JSON response: ") + e.what());
    }

    if (mVerbose) {
        std::cerr << "[GraphQLClient] HTTP " << response.httpStatus << "\n";
    }

    // Graceful shutdown (non-critical errors are swallowed).
    beast::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_both, ec);

    return response;
}

// ---------------------------------------------------------------------------
// HTTPS (compiled only when OpenSSL is available)
// ---------------------------------------------------------------------------

GraphQLClient::Response
GraphQLClient::doHttpsRequest(const std::string& requestBody)
{
#ifdef GRAPHQL_SYNC_HAS_SSL
    namespace ssl = net::ssl;

    net::io_context ioc;
    ssl::context    ctx(ssl::context::tlsv12_client);
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(ssl::verify_peer);

    tcp::resolver resolver(ioc);
    beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);

    // SNI hostname.
    if (!SSL_set_tlsext_host_name(stream.native_handle(), mHost.c_str())) {
        throw std::runtime_error("Failed to set SNI hostname");
    }

    auto const results = resolver.resolve(mHost, mPort);
    beast::get_lowest_layer(stream).expires_after(
        std::chrono::milliseconds(mTimeoutMs));
    beast::get_lowest_layer(stream).connect(results);

    stream.handshake(ssl::stream_base::client);

    // Build request.
    http::request<http::string_body> req{http::verb::post, mTarget, 11};
    req.set(http::field::host, mHost);
    req.set(http::field::content_type, "application/json");
    req.set(http::field::accept, "application/json");
    req.set(http::field::user_agent, "graphql_sync/1.0");
    if (!mAccessToken.empty()) {
        req.set("X-Shopify-Access-Token", mAccessToken);
    }
    req.body() = requestBody;
    req.prepare_payload();

    http::write(stream, req);

    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    beast::get_lowest_layer(stream).expires_after(
        std::chrono::milliseconds(mTimeoutMs));
    http::read(stream, buffer, res);

    Response response;
    response.httpStatus = res.result_int();
    try {
        response.body = nlohmann::json::parse(res.body());
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(
            std::string("Failed to parse JSON response: ") + e.what());
    }

    if (mVerbose) {
        std::cerr << "[GraphQLClient] HTTPS " << response.httpStatus << "\n";
    }

    beast::error_code ec;
    stream.shutdown(ec);

    return response;
#else
    (void)requestBody;
    throw std::runtime_error("HTTPS not supported: built without OpenSSL");
#endif
}

} // namespace graphql_sync
