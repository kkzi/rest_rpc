#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace rpc
{
using tcp = boost::asio::ip::tcp;                // from <boost/asio/ip/tcp.hpp>
using string_view = boost::beast::string_view;   // from <boost/beast/core/string.hpp>
namespace http = boost::beast::http;             // from <boost/beast/http.hpp>

using http_hook_function_t = std::function<http::response<http::string_body>(const http::request<http::string_body> &)>;


struct http_server_config
{
    http_server_config(boost::asio::io_context & io, uint16_t port, std::string www_root)
        : io(io)
        , port(port)
        , www_root(www_root)
    {

    }

    boost::asio::io_context & io;
    uint16_t port;
    std::string www_root;
    std::map<http::verb, std::map<string_view, http_hook_function_t>> hook_functions;
};



// Return a reasonable mime type based on the extension of a file.
static boost::beast::string_view mime_type(boost::beast::string_view path)
{
    static std::map<string_view, string_view> types{
        {"", "application/text"},
        {".htm", "text/html"},
        {".html", "text/html"},
        {".php",  "text/html"},
        {".css",  "text/css"},
        {".txt",  "text/plain"},
        {".js",   "application/javascript"},
        {".json", "application/json"},
        {".xml",  "application/xml"},
        {".swf",  "application/x-shockwave-flash"},
        {".flv",  "video/x-flv"},
        {".png",  "image/png"},
        {".jpe",  "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".jpg",  "image/jpeg"},
        {".gif",  "image/gif"},
        {".bmp",  "image/bmp"},
        {".ico",  "image/vnd.microsoft.icon"},
        {".tiff", "image/tiff"},
        {".tif",  "image/tiff"},
        {".svg",  "image/svg+xml"},
        {".svgz", "image/svg+xml"},
    };

    const auto pos = path.rfind(".");
    const auto ext = pos == boost::beast::string_view::npos ? "" : path.substr(pos);
    return types.count(ext) ? types.at(ext) : "application/text";
}

// Append an HTTP rel-path to a local filesystem path.
// The returned path is normalized for the platform.
static std::string path_cat(boost::beast::string_view base, boost::beast::string_view path)
{
    if (base.empty()) return path.to_string();
    std::string result = base.to_string();
    char constexpr path_separator = '/';
    if (result.back() == path_separator) result.resize(result.size() - 1);
    result.append(path.data(), path.size());
    return result;
}

// This function produces an HTTP response for the given
// request. The type of the response object depends on the
// contents of the request, so the interface requires the
// caller to pass a generic lambda for receiving the response.
template<class Body, class Allocator, class Send>
void handle_request(const http_server_config & config , http::request<Body, http::basic_fields<Allocator>> && req, Send && send)
{
    // Returns a bad request response
    auto const bad_request = [&req](boost::beast::string_view why) {
        http::response<http::string_body> res{ http::status::bad_request, req.version() };
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = why.to_string();
        res.prepare_payload();
        return res;
    };

    // Returns a not found response
    auto const not_found = [&req](boost::beast::string_view target) {
        http::response<http::string_body> res{ http::status::not_found, req.version() };
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "The resource '" + target.to_string() + "' was not found.";
        res.prepare_payload();
        return res;
    };

    // Returns a server error response
    auto const server_error = [&req](boost::beast::string_view what) {
        http::response<http::string_body> res{ http::status::internal_server_error, req.version() };
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, "text/html");
        res.keep_alive(req.keep_alive());
        res.body() = "An error occurred: '" + what.to_string() + "'";
        res.prepare_payload();
        return res;
    };

    const auto & method = req.method();
    const auto & target = req.target();

    // Make sure we can handle the method
    if (method != http::verb::get && method != http::verb::post && method != http::verb::head) {
        return send(bad_request("Unknown HTTP-method"));
    }

    // Request path must be absolute and not contain "..".
    if (target.empty() || target[0] != '/' || target.find("..") != boost::beast::string_view::npos) {
        return send(bad_request("Illegal request-target"));
    }

    // Handle hook functions
    if (config.hook_functions.count(method) > 0) {
        const auto & funcs = config.hook_functions.at(method);
        std::string href(target);
        auto schema = href.substr(0, href.find_first_of('?', 0));
        if (funcs.count(schema) > 0) {
            const auto & func = funcs.at(schema);
            assert(func != nullptr);
            auto res = func(req);
            return send(std::move(res));
        }
    }

    // Build the path to the requested file
    std::string path = path_cat(config.www_root, target);
    if (target.back() == '/') path.append("index.html");

    // Attempt to open the file
    boost::beast::error_code ec;
    http::file_body::value_type body;
    body.open(path.c_str(), boost::beast::file_mode::scan, ec);

    // Handle the case where the file doesn't exist
    if (ec == boost::system::errc::no_such_file_or_directory) {
        return send(not_found(target));
    }

    // Handle an unknown error
    if (ec) {
        return send(server_error(ec.message()));
    }

    // Respond to HEAD request
    if (method == http::verb::head) {
        http::response<http::empty_body> res{ http::status::ok, req.version() };
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, mime_type(path));
        res.content_length(body.size());
        res.keep_alive(req.keep_alive());
        return send(std::move(res));
    }

    // Respond to GET request
    http::response<http::file_body> res{
        std::piecewise_construct,
        std::make_tuple(std::move(body)),
        std::make_tuple(http::status::ok, req.version())
    };
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, mime_type(path));
    res.content_length(body.size());
    res.keep_alive(req.keep_alive());
    return send(std::move(res));
}

// Report a failure
static void fail(boost::system::error_code ec, char const* what)
{
    std::cerr << what << ": " << ec.message() << "\n";
}

// This is the C++11 equivalent of a generic lambda.
// The function object is used to send an HTTP message.
template<class Stream>
struct send_lambda
{
    Stream& stream_;
    bool& close_;
    boost::system::error_code& ec_;
    boost::asio::yield_context yield_;

    explicit send_lambda(Stream& stream, bool& close, boost::system::error_code& ec, boost::asio::yield_context yield)
        : stream_(stream)
        , close_(close)
        , ec_(ec)
        , yield_(yield)
    {
    }

    template<bool isRequest, class Body, class Fields>
    void operator()(http::message<isRequest, Body, Fields>&& msg) const
    {
        // Determine if we should close the connection after
        close_ = msg.need_eof();

        // We need the serializer here because the serializer requires
        // a non-const file_body, and the message oriented version of
        // http::write only works with const messages.
        http::serializer<isRequest, Body, Fields> sr{ msg };
        http::async_write(stream_, sr, yield_[ec_]);
    }
};

// Handles an HTTP server connection
static void do_session(tcp::socket & socket, const http_server_config & config, boost::asio::yield_context yield)
{
    bool close = false;
    boost::system::error_code ec;

    // This buffer is required to persist across reads
    boost::beast::flat_buffer buffer;

    // This lambda is used to send messages
    send_lambda<tcp::socket> lambda{ socket, close, ec, yield };

    for (;;)
    {
        // Read a request
        http::request<http::string_body> req;
        http::async_read(socket, buffer, req, yield[ec]);
        if (ec == http::error::end_of_stream)
            break;
        if (ec)
            return fail(ec, "read");

        // Send the response
        handle_request(config, std::move(req), lambda);
        if (ec)
            return fail(ec, "write");
        if (close)
        {
            // This means we should close the connection, usually because
            // the response indicated the "Connection: close" semantic.
            break;
        }
    }

    // Send a TCP shutdown
    socket.shutdown(tcp::socket::shutdown_send, ec);

    // At this point the connection is closed gracefully
}

//------------------------------------------------------------------------------

// Accepts incoming connections and launches the sessions
static void do_listen(const http_server_config & config, boost::asio::yield_context yield)
{
    tcp::acceptor acceptor(config.io, tcp::endpoint{ tcp::v4(), config.port });
    boost::system::error_code ec;
    for (;;) {
        tcp::socket socket(config.io);
        acceptor.async_accept(socket, yield[ec]);
        if (ec) {
            fail(ec, "accept");
        }
        else {
            boost::asio::spawn(
                acceptor.get_executor().context() , std::bind(&do_session, std::move(socket), config, std::placeholders::_1)
            );
        }
    }
}



class http_server
{
public:
    http_server(boost::asio::io_context & io, uint16_t port, const std::string & www_root = "./", size_t thread_count = 1)
        : config_(io, port, www_root)
        , threads_(thread_count)
    {

    }

public:
    // Register a hook function to handle custom requests
    void hook(http::verb method, const string_view & target, http_hook_function_t func)
    {
        assert(func != nullptr);
        if (config_.hook_functions.count(method) == 0) {
            config_.hook_functions[method] = std::map<string_view, http_hook_function_t>{};
        }
        config_.hook_functions[method][target] = func;
    }

    void run()
    {
        //boost::asio::spawn(io_, std::bind(&do_listen, std::ref(io_), tcp::endpoint{ tcp::v4(), port_ }, www_root_, std::placeholders::_1));
        boost::asio::spawn(config_.io, std::bind(&do_listen, config_, std::placeholders::_1));

        static std::vector<std::thread> v;
        v.reserve(threads_);
        for (auto i = 0; i < threads_; ++i) {
            v.emplace_back(std::thread([=]() { config_.io.run(); }));
        }
    }

private:
    http_server_config config_;
    size_t threads_;
};

}  // namespace rpc


using http_server = rpc::http_server;
