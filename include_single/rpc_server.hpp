/*
a rest rpc server with json
version 0.1.4
https://github.com/kkzi/rest_rpc

Licensed under the MIT License <http://opensource.org/licenses/MIT>.
SPDX-License-Identifier: MIT
Copyright (c) 2018 kkzi@github

Permission is hereby  granted, free of charge, to any  person obtaining a copy
of this software and associated  documentation files (the "Software"), to deal
in the Software  without restriction, including without  limitation the rights
to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/


#pragma once


#include <vector>
#include <string>
#include <memory>
#include <cstdint>
#include <iostream>
#include <functional>
#include <boost/asio.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <json/json_util.h>


namespace rpc
{

using boost::asio::ip::tcp;

// common defines
enum class result_code : int16_t
{
    OK = 0,
    FAIL = 1,
    BAD_REQUEST = 400,
    UNAUTHORIZED = 401,
    NOT_FOUND = 404,
    REQUEST_TIMEOUT = 408,
    INTERNAL_SERVER_ERROR = 500,
    NOT_IMPLEMENTED = 501,
    BAD_GATEWAY = 502,
    SERVICE_UNAVAILABLE = 503,
    GATEWAY_TIMEOUT = 504,
};

static const size_t MAX_BUF_LEN = 1048576 * 10;
static const size_t HEAD_LEN = 4;
static const size_t PAGE_SIZE = 1024 * 1024;



class io_context_pool final : private boost::noncopyable
{
public:
    explicit io_context_pool(std::size_t pool_size)
        : io_cursor_(0)
    {
        for (auto i = 0; i < pool_size; ++i) {
            auto io = std::make_shared<boost::asio::io_context>();
            io_contexts_.push_back(io);
            works_.push_back(std::make_shared<boost::asio::io_context::work>(*io));
        }
    }

    ~io_context_pool()
    {
        stop();
    }

    void run()
    {
        std::vector<std::shared_ptr<std::thread>> threads;
        for (auto & io : io_contexts_) {
            threads.emplace_back(std::make_shared<std::thread>([](io_context_ptr svr) {
                svr->run();
            }, io));
        }

        for (auto & t : threads) {
            t->join();
        }
    }

    void stop()
    {
        for (auto & io : io_contexts_) {
            io->stop();
            io->reset();
        }
    }

    boost::asio::io_context & get_io_service()
    {
        auto & io = *io_contexts_[io_cursor_];
        ++io_cursor_;
        if (io_cursor_ == io_contexts_.size()) io_cursor_ = 0;
        return io;
    }

private:
    typedef std::shared_ptr<boost::asio::io_context> io_context_ptr;
    typedef std::shared_ptr<boost::asio::io_context::work> work_ptr;

    std::vector<io_context_ptr> io_contexts_;
    std::vector<work_ptr> works_;
    std::size_t io_cursor_;
};



struct packer
{
    template<typename ... Args>
    static std::string request(const std::string & url, Args && ... args)
    {
        return json{
            {"url", url},
            {"arguments", std::make_tuple(std::forward<Args>(args)...)},
        }.dump();
    }

    template<typename T>
    static std::string response(result_code code, const std::string & message, const T & content)
    {
        return json{ {"code", (int)code}, {"description", message}, {"content", content} }.dump();
    }

    static std::string response(result_code code, const std::string & message)
    {
        return json{ {"code", (int)code}, {"description", message} }.dump();
    }

    template<typename T>
    static std::string success(const T & content)
    {
        return json{ {"code", result_code::OK}, {"content", content} }.dump();
    }

    static std::string success()
    {
        return json{ {"code", result_code::OK} }.dump();
    }
};




enum class execute_mode { SYNC, ASYNC };

template<typename T>
struct function_traits;

template<typename Ret, typename... Args>
struct function_traits<Ret(Args...)>
{
public:
    using args_tuple_t = std::tuple<std::remove_const_t<std::remove_reference_t<Args>>...>;
};

template<typename Ret, typename... Args>
struct function_traits<Ret(*)(Args...)> : function_traits<Ret(Args...)> {};

template <typename Ret, typename... Args>
struct function_traits<std::function<Ret(Args...)>> : function_traits<Ret(Args...)> {};

template <typename ReturnType, typename ClassType, typename... Args>
struct function_traits<ReturnType(ClassType::*)(Args...)> : function_traits<ReturnType(Args...)> {};

template <typename ReturnType, typename ClassType, typename... Args>
struct function_traits<ReturnType(ClassType::*)(Args...) const> : function_traits<ReturnType(Args...)> {};

template<typename Callable>
struct function_traits : function_traits<decltype(&Callable::operator())> {};




class connection;
class router : boost::noncopyable
{
public:
    static router & get()
    {
        static router instance;
        return instance;
    }

    template<execute_mode mode, typename Function>
    void route(const std::string & name, Function func)
    {
        using namespace std::placeholders;
        map_invokers_[name] = [=](const json & arguments, std::string & reply, execute_mode & exe_mode) {
            using args_tuple = typename function_traits<Function>::args_tuple_t;
            exe_mode = execute_mode::SYNC;
            try {
                auto tp = arguments.get<args_tuple>();
                call(func, reply, tp);
                exe_mode = mode;
            }
            catch (const std::exception & e) {
                reply = packer::response(result_code::FAIL, e.what());
            }
        };
    }

    template<execute_mode mode, typename Function, typename Self>
    void route(const std::string & name, const Function & func, Self * self)
    {
        using namespace std::placeholders;
        map_invokers_[name] = [=](const json & args, std::string & reply, execute_mode & exe_mode) {
            using args_tuple = typename function_traits<Function>::args_tuple_t;
            exe_mode = execute_mode::SYNC;
            try {
                auto tp = args.get<args_tuple>();
                call_member(func, self, reply, tp);
                exe_mode = mode;
            }
            catch (const std::exception & e) {
                reply = packer::response(result_code::FAIL, e.what());
            }
        };
    }

    template<typename T>
    void execute_function(const std::string & data, T conn)
    {
        std::string result;
        try {
            const auto & req = json::parse(data);
            const auto & url = json_util::get<std::string>(req, "url", "");

            auto it = map_invokers_.find(url);
            if (it == map_invokers_.end()) {
                result = packer::response(result_code::FAIL, "unknown request: " + url);
                callback_to_server_(url, result, conn, true);
                return;
            }

            const auto & args = json_util::get(req, "arguments", json::array());
            execute_mode mode;
            it->second(args, result, mode);
            if (mode == execute_mode::SYNC && callback_to_server_) {
                callback_to_server_(url, result, conn, false);
            }
        }
        catch (const std::exception & e) {
            result = packer::response(result_code::FAIL, e.what());
            callback_to_server_("", result, conn, true);
        }
    }

    std::string execute_function(const std::string & data)
    {
        try {
            const auto & req = json::parse(data);
            const auto & url = json_util::get<std::string>(req, "url", "");

            auto it = map_invokers_.find(url);
            if (it == map_invokers_.end()) {
                return packer::response(result_code::FAIL, "unknown request: " + url);
            }

            const auto & args = json_util::get(req, "arguments", json::array());
            execute_mode mode;
            std::string result;
            it->second(args, result, mode);
            return result;
        }
        catch (const std::exception & e) {
            return packer::response(result_code::FAIL, e.what());
        }
    }

    void unroute(const std::string & name)
    {
        map_invokers_.erase(name);
    }

    void set_callback(const std::function<void(const std::string &, const std::string &, connection *, bool)> & callback)
    {
        callback_to_server_ = callback;
    }


    router() = default;

private:
    router(const router &) = delete;
    router(router &&) = delete;

    template<typename F, size_t... idx, typename... Args>
    static typename std::result_of<F(Args...)>::type
        call_helper(const F & f, const std::index_sequence<idx...> &, const std::tuple<Args...> & tup)
    {
        return f(std::get<idx>(tup)...);
    }

    template<typename F, typename... Args>
    static typename std::enable_if<std::is_void<typename std::result_of<F(Args...)>::type>::value>::type
        call(const F & f, std::string & result, std::tuple<Args...> & tp)
    {
        call_helper(f, std::make_index_sequence<sizeof...(Args)>{}, tp);
        result = packer::success();
    }

    template<typename F, typename... Args>
    static typename std::enable_if<!std::is_void<typename std::result_of<F(Args...)>::type>::value>::type
        call(const F & f, std::string & result, const std::tuple<Args...> & tp)
    {
        auto r = call_helper(f, std::make_index_sequence<sizeof...(Args)>{}, tp);
        result = packer::success(r);
    }

    template<typename F, typename Self, size_t... idx, typename... Args>
    static typename std::result_of<F(Self, Args...)>::type call_member_helper(
        const F & f, Self * self, const std::index_sequence<idx...> &,
        const std::tuple<Args...> & tup)
    {
        return (*self.*f)(std::get<idx>(tup)...);
    }

    template<typename F, typename Self, typename... Args>
    static typename std::enable_if<std::is_void<typename std::result_of<F(Self, Args...)>::type>::value>::type
        call_member(const F & f, Self * self, std::string & result, const std::tuple<Args...> & tp)
    {
        call_member_helper(f, self, typename std::make_index_sequence<sizeof...(Args)>{}, tp);
        result = packer::success();
    }

    template<typename F, typename Self, typename... Args>
    static typename std::enable_if<!std::is_void<typename std::result_of<F(Self, Args...)>::type>::value>::type
        call_member(const F & f, Self * self, std::string & result, const std::tuple<Args...> & tp)
    {
        auto r = call_member_helper(f, self, typename std::make_index_sequence<sizeof...(Args)>{}, tp);
        result = packer::success(r);
    }


private:
    std::map<std::string, std::function<void(const json & args, std::string & reply, execute_mode & mode)>> map_invokers_;
    std::function<void(const std::string &, const std::string &, connection *, bool)> callback_to_server_;
};



class connection final : public std::enable_shared_from_this<connection>, private boost::noncopyable
{
public:
    connection(boost::asio::io_service& io_service, std::size_t timeout_seconds)
        : socket_(io_service)
        , data_(PAGE_SIZE)
        , message_{ boost::asio::buffer(head_), boost::asio::buffer(data_.data(), data_.size()) }
        , timer_(io_service)
        , timeout_seconds_(timeout_seconds)
        , has_closed_(false)
    {

    }

    ~connection()
    {
        close();
    }

    void start()
    {
        read_head();
    }

    tcp::socket& socket()
    {
        return socket_;
    }

    bool has_closed() const
    {
        return has_closed_;
    }

    void response(std::string data)
    {
        auto len = sizeof(uint32_t);
        message_[0] = boost::asio::buffer(&len, len);
        message_[1] = boost::asio::buffer(data);
        reset_timer();
        auto self = this->shared_from_this();
        boost::asio::async_write(socket_, message_, [this, self](boost::system::error_code ec, std::size_t length) {
            cancel_timer();
            if (has_closed()) return;

            if (!ec) {
                read_head();
            }
            else {
                //LOG(INFO) << ec.message();
            }
        });
    }

    void set_conn_id(int64_t id)
    {
        conn_id_ = id;
    }

    int64_t conn_id() const
    {
        return conn_id_;
    }

private:
    void read_head()
    {
        reset_timer();
        auto self(this->shared_from_this());
        boost::asio::async_read(socket_, boost::asio::buffer(head_), [this, self](boost::system::error_code ec, std::size_t length) {
            if (!socket_.is_open()) {
                //LOG(INFO) << "socket already closed";
                return;
            }

            if (!ec) {
                const int body_len = *((int*)(head_));
                if (body_len > 0 && body_len < MAX_BUF_LEN) {
                    if (data_.size() < body_len) { data_.resize(body_len); }
                    read_body(body_len);
                    return;
                }

                if (body_len == 0) {  // nobody, just head, maybe as heartbeat.
                    cancel_timer();
                    read_head();
                }
                else {
                    //LOG(INFO) << "invalid body len";
                    close();
                }
            }
            else {
                //LOG(INFO) << ec.message();
                close();
            }
        });
    }

    void read_body(std::size_t size)
    {
        auto self(this->shared_from_this());
        boost::asio::async_read(socket_, boost::asio::buffer(data_.data(), size), [this, self](boost::system::error_code ec, std::size_t length) {
            cancel_timer();

            if (!socket_.is_open()) {
                //LOG(INFO) << "socket already closed";
                return;
            }

            if (!ec) {
                std::string data(data_.begin(), data_.end());
                router::get().execute_function(data, this);
            }
            else {
                //LOG(INFO) << ec.message();
            }
        });
    }

    void reset_timer()
    {
        if (timeout_seconds_ == 0) { return; }

        auto self(this->shared_from_this());
        timer_.expires_from_now(std::chrono::seconds(timeout_seconds_));
        timer_.async_wait([this, self](const boost::system::error_code& ec) {
            if (has_closed()) { return; }

            if (ec) { return; }

            //LOG(INFO) << "rpc connection timeout";
            close();
        });
    }

    void cancel_timer()
    {
        if (timeout_seconds_ == 0) { return; }

        timer_.cancel();
    }

    void close()
    {
        has_closed_ = true;
        if (socket_.is_open()) {
            boost::system::error_code ignored_ec;
            socket_.shutdown(tcp::socket::shutdown_both, ignored_ec);
            socket_.close(ignored_ec);
        }
    }

    tcp::socket socket_;
    char head_[HEAD_LEN];
    std::vector<char> data_;
    std::array<boost::asio::mutable_buffer, 2> message_;
    boost::asio::steady_timer timer_;
    std::size_t timeout_seconds_;
    int64_t conn_id_ = 0;
    std::atomic_bool has_closed_;
};




class server final : private boost::noncopyable
{

public:
    server(uint16_t port, size_t size, size_t timeout_seconds = 15, size_t check_seconds = 10)
        : io_pool_(size)
        , acceptor_(io_pool_.get_io_service(), tcp::endpoint(tcp::v4(), port))
        , timeout_seconds_(timeout_seconds)
        , check_seconds_(check_seconds)
    {
        using namespace std::placeholders;
        router::get().set_callback(std::bind(&server::callback, this, _1, _2, _3, _4));
        do_accept();
        check_thread_ = std::make_shared<std::thread>(std::bind(&server::clean, this));
    }

    ~server()
    {
        io_pool_.stop();
        thd_->join();
    }

    void run()
    {
        thd_ = std::make_shared<std::thread>([this] { io_pool_.run(); });
    }

    template<execute_mode model = execute_mode::SYNC, typename Function>
    void route(std::string const & name, const Function & f)
    {
        router::get().route<model>(name, f);
    }

    template<execute_mode model = execute_mode::SYNC, typename Function, typename Self>
    void route(std::string const & name, const Function & f, Self * self)
    {
        router::get().route<model>(name, f, self);
    }

    void response(int64_t conn_id, const std::string & result)
    {
        std::unique_lock<std::mutex> lock(mtx_);
        auto it = connections_.find(conn_id);
        if (it != connections_.end()) {
            it->second->response(result);
        }
    }

private:
    void do_accept()
    {
        auto conn = std::make_shared<connection>(io_pool_.get_io_service(), timeout_seconds_);
        acceptor_.async_accept(conn->socket(), [=](boost::system::error_code ec) {
            if (ec) {
                //LOG(INFO) << "acceptor error: " << ec.message();
            }
            else {
                conn->start();
                std::unique_lock<std::mutex> lock(mtx_);
                conn->set_conn_id(conn_id_);
                connections_.emplace(conn_id_++, conn);
            }

            do_accept();
        });
    }

    void clean()
    {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(check_seconds_));

            std::unique_lock<std::mutex> lock(mtx_);
            for (auto it = connections_.cbegin(); it != connections_.cend();) {
                if (it->second->has_closed()) {
                    it = connections_.erase(it);
                }
                else {
                    ++it;
                }
            }
        }
    }

    void callback(const std::string & topic, const std::string & result, connection * conn, bool has_error = false)
    {
        response(conn->conn_id(), result);
    }

    io_context_pool io_pool_;
    tcp::acceptor acceptor_;
    std::shared_ptr<std::thread> thd_;
    std::size_t timeout_seconds_;

    std::unordered_map<int64_t, std::shared_ptr<connection>> connections_;
    int64_t conn_id_ = 0;
    std::mutex mtx_;
    std::shared_ptr<std::thread> check_thread_;
    size_t check_seconds_;
};




#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <map>

using tcp = boost::asio::ip::tcp;                // from <boost/asio/ip/tcp.hpp>
using string_view = boost::beast::string_view;   //from <boost/beast/core/string.hpp>
namespace http = boost::beast::http;             // from <boost/beast/http.hpp>

class http_server
{
public:
    using hook_function_t = std::function<http::response<http::string_body>(const http::request<http::string_body> &)>;

public:
    // Accepts incoming connections and launches the sessions
    static void do_listen(boost::asio::io_context& ioc, tcp::endpoint endpoint, std::string const& doc_root, boost::asio::yield_context yield)
    {
        boost::system::error_code ec;

        // Open the acceptor
        tcp::acceptor acceptor(ioc);
        acceptor.open(endpoint.protocol(), ec);
        if (ec) return fail(ec, "open");

        // Bind to the server address
        acceptor.bind(endpoint, ec);
        if (ec) return fail(ec, "bind");

        // Start listening for connections
        acceptor.listen(boost::asio::socket_base::max_listen_connections, ec);
        if (ec) return fail(ec, "listen");

        for (;;) {
            tcp::socket socket(ioc);
            acceptor.async_accept(socket, yield[ec]);
            if (ec) {
                fail(ec, "accept");
            }
            else {
                boost::asio::spawn(acceptor.get_executor().context(), std::bind(&do_session, std::move(socket), doc_root, std::placeholders::_1));
            }
        }
    }

    // Register a hook function to handle custom requests
    static void hook(http::verb method, const string_view & target, hook_function_t func)
    {
        assert(func != nullptr);
        if (hook_functions_.count(method) == 0) {
            hook_functions_[method] = std::map<string_view, hook_function_t>{};
        }
        hook_functions_[method][target] = func;
    }

private:
    static string_view mime_type(string_view path)
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

    static std::string path_cat(boost::beast::string_view base, boost::beast::string_view path)
    {
        if (base.empty()) return path.to_string();
        std::string result = base.to_string();
        char constexpr path_separator = '/';
        if (result.back() == path_separator) result.resize(result.size() - 1);
        result.append(path.data(), path.size());
        return result;
    }

    template<class Body, class Allocator, class Send>
    static void handle_request(boost::beast::string_view doc_root, http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send)
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

        auto method = req.method();
        // Make sure we can handle the method
        if (method != http::verb::get && method != http::verb::post) {
            return send(bad_request("Unsupported http method"));
        }

        // Request path must be absolute and not contain "..".
        const auto & target = req.target();
        if (target.empty() || target[0] != '/' || target.find("..") != boost::beast::string_view::npos) {
            return send(bad_request("Illegal request-target"));
        }

        // Handle hook functions
        if (hook_functions_.count(method) > 0) {
            const auto & funcs = hook_functions_.at(method);
            if (funcs.count(target) > 0) {
                const auto & func = funcs.at(target);
                assert(func != nullptr);
                auto res = func(req);
                return send(std::move(res));
            }
        }

        // Build the path to the requested file
        std::string path = path_cat(doc_root, target);
        if (target.back() == '/') path.append("index.html");

        // Attempt to open the file
        boost::beast::error_code ec;
        http::file_body::value_type body;
        body.open(path.c_str(), boost::beast::file_mode::scan, ec);

        // Handle the case where the file doesn't exist
        if (ec == boost::system::errc::no_such_file_or_directory) return send(not_found(target));

        // Handle an unknown error
        if (ec) return send(server_error(ec.message()));

        // Respond to HEAD request
        if (req.method() == http::verb::head) {
            http::response<http::empty_body> res{ http::status::ok, req.version() };
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, mime_type(path));
            res.content_length(body.size());
            res.keep_alive(req.keep_alive());
            return send(std::move(res));
        }

        // Respond to GET request
        http::response<http::file_body> res{ std::piecewise_construct, std::make_tuple(std::move(body)), std::make_tuple(http::status::ok, req.version()) };
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, mime_type(path));
        res.content_length(body.size());
        res.keep_alive(req.keep_alive());
        return send(std::move(res));
    }

    static void fail(boost::system::error_code ec, char const* what)
    {
        std::cerr << what << ": " << ec.message() << "\n";
    }

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
    static void do_session(tcp::socket& socket, std::string const& doc_root, boost::asio::yield_context yield)
    {
        bool close = false;
        boost::system::error_code ec;

        // This buffer is required to persist across reads
        boost::beast::flat_buffer buffer;

        // This lambda is used to send messages
        send_lambda<tcp::socket> lambda{ socket, close, ec, yield };

        for (;;) {
            // Read a request
            http::request<http::string_body> req;
            http::async_read(socket, buffer, req, yield[ec]);
            if (ec == http::error::end_of_stream)
                break;
            if (ec)
                return fail(ec, "read");

            // Send the response
            handle_request(doc_root, std::move(req), lambda);
            if (ec)
                return fail(ec, "write");

            if (close) {
                // This means we should close the connection, usually because
                // the response indicated the "Connection: close" semantic.
                break;
            }
        }

        // Send a TCP shutdown
        socket.shutdown(tcp::socket::shutdown_send, ec);

        // At this point the connection is closed gracefully
    }

private:
    static std::map<http::verb, std::map<string_view, hook_function_t>> hook_functions_;

}; // class http_server

std::map<http::verb, std::map<string_view, http_server::hook_function_t>> http_server::hook_functions_;


}  // namespace rpc




