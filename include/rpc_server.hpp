#pragma once


#include <vector>
#include <string>
#include <memory>
#include <cstdint>
#include <iostream>
#include <functional>
#include <thread>
#include <boost/asio.hpp>
#include <boost/core/noncopyable.hpp>
#include <json/json_util.h>
#include "rpc_defs.h"


namespace rpc
{

using boost::asio::ip::tcp;
using io_context = boost::asio::io_context;
using thread_ptr = std::shared_ptr<std::thread>;
using debug_callback_t = std::function<void(const std::string &)>;

static const size_t MAX_BUF_LEN = 1048576 * 10;
static const size_t HEAD_LEN = 4;
static const size_t PAGE_SIZE = 1024 * 1024;

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

    template<class T, typename = std::enable_if_t<!std::is_void<T>::value>>
    static std::string response(result_code code, const std::string & message, const T & content)
    {
        return json(rpc::reply{ code, message, content }).dump();
    }

    static std::string response(result_code code, const std::string & message)
    {
        return response(code, message, nullptr);
    }

    static std::string success()
    {
        return response(result_code::OK, "", nullptr);
    }

    template<class T, typename = std::enable_if_t<!std::is_void<T>::value>>
    static std::string success(const T & content)
    {
        return response(result_code::OK, "", content);
    }

};






class connection;
class router final : private boost::noncopyable
{
public:
    static router & instance()
    {
        static router ins;
        return ins;
    }

protected:
    router() = default;

public:
    template<execute_mode mode, typename Function>
    void route(const std::string & name, Function func)
    {
        using namespace std::placeholders;
        assert(!name.empty());
        auto url = fix_url(name);
        map_invokers_[url] = [=](const json & arguments, std::string & reply, execute_mode & exe_mode) {
            using args_tuple = typename function_traits<Function>::args_tuple_t;
            exe_mode = execute_mode::SYNC;
            try {
                const auto & tp = arguments.get<args_tuple>();
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
        auto url = fix_url(name);
        map_invokers_[url] = [=](const json & args, std::string & reply, execute_mode & exe_mode) {
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

    void unroute(const std::string & name)
    {
        map_invokers_.erase(name);
    }

    std::string fix_url(const std::string & url)
    {
        return url.at(0) == '/' ? std::move(url) : '/' + url;
    }

    template<typename T>
    void execute_function(const std::string & data, T conn)
    {
        std::string result;
        try {
            const auto & req = json::parse(data);
            auto url = json_util::get<std::string>(req, "url", "");
            url = fix_url(url);

            auto it = map_invokers_.find(url);
            if (it == map_invokers_.end()) {
                result = packer::response(result_code::FAIL, "unknown request: " + url);
                if (callback_to_server_) callback_to_server_(url, result, conn, true);
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
            if (callback_to_server_) callback_to_server_("", result, conn, true);
        }
    }

    // Handle from http request
    std::string execute_function(const std::string & data)
    {
        try {
            const auto & req = json::parse(data);
            auto url = json_util::get<std::string>(req, "url", "");
            url = fix_url(url);

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

    void set_callback(const std::function<void(const std::string &, const std::string &, connection *, bool)> & callback)
    {
        callback_to_server_ = callback;
    }


private:
    router(const router &) = delete;
    router(router &&) = delete;

private:
    template<typename F, size_t... idx, typename... Args>
    static typename std::result_of<F(Args...)>::type
        call_helper(const F & f, const std::index_sequence<idx...> &, const std::tuple<Args...> & tup)
    {
        return f(std::get<idx>(tup)...);
    }

    template<typename F, typename... Args>
    static typename std::enable_if<std::is_void<typename std::result_of<F(Args...)>::type>::value>::type
        call(const F & f, std::string & result, const std::tuple<Args...> & tp)
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
    std::function<void(const std::string &, const std::string &, connection *, bool)> callback_to_server_{ nullptr };
};



class connection final : public std::enable_shared_from_this<connection>, private boost::noncopyable
{
public:
    connection(boost::asio::io_service& io_service, std::size_t timeout_seconds, debug_callback_t log_func)
        : socket_(io_service)
        , data_(PAGE_SIZE)
        , message_{ boost::asio::buffer(head_), boost::asio::buffer(data_.data(), data_.size()) }
        , timer_(io_service)
        , timeout_seconds_(timeout_seconds)
        , log_func_(log_func)
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

    void response(const char * data, size_t len)
    {
        if (log_func_) log_func_("response: " + std::string(data, len));

        message_[0] = boost::asio::buffer(&len, 4);
        message_[1] = boost::asio::buffer((char *)data, len);
        reset_timer();
        auto self = this->shared_from_this();
        boost::asio::async_write(socket_, message_, [this, self](boost::system::error_code ec, std::size_t length) {
            if (log_func_) log_func_("response ok");

            cancel_timer();
            if (has_closed()) return;

            if (!ec) {
                read_head();
            }
            else {
                if (log_func_) log_func_("response failed: " + ec.message());
            }
        });
    }

    void set_connection_id(int64_t id)
    {
        conn_id_ = id;
    }

    int64_t connection_id() const
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
                return;
            }

            if (!ec) {
                const int body_len = *((int*)(head_));
                if (body_len > 0 && body_len < MAX_BUF_LEN) {
                    data_.resize(body_len);
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
                if (log_func_) log_func_("connection already closed. " + std::to_string(conn_id_));
                return;
            }

            if (!ec) {
                std::string data(data_.begin(), data_.end());
                if (log_func_) log_func_("handle request: " + data);
                router::instance().execute_function(data, this);
            }
            else {
                if (log_func_) log_func_("read request failed: " + ec.message());
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

private:
    tcp::socket socket_;
    char head_[HEAD_LEN];
    std::vector<char> data_;
    std::array<boost::asio::mutable_buffer, 2> message_;
    boost::asio::steady_timer timer_;
    std::size_t timeout_seconds_;
    int64_t conn_id_ = 0;
    std::atomic_bool has_closed_;

    debug_callback_t  log_func_;
};




class server final : private boost::noncopyable
{

public:
    server(uint16_t port, size_t size, size_t timeout_seconds = 15, size_t check_seconds = 3)
        : intruppted_(false)
        , acceptor_(io_, tcp::endpoint(tcp::v4(), port))
        , timeout_seconds_(timeout_seconds)
        , check_seconds_(check_seconds)
    {
        using namespace std::placeholders;
        router::instance().set_callback(std::bind(&server::callback, this, _1, _2, _3, _4));

        do_accept();

        for (auto i = 0; i < size; ++i) {
            work_threads_.emplace_back(std::make_shared<std::thread>([=] { io_.run(); }));
        }
        check_thread_ = std::make_shared<std::thread>(std::bind(&server::clean, this));

        route("/ping", []() { return "pong"; });
    }

    ~server()
    {
        intruppted_.store(true);
        acceptor_.close();
        io_.stop();
        io_.reset();
        check_thread_->join();

        for (auto & t : work_threads_) {
            t->join();
        }
    }

    void run(bool block = false)
    {
        if (block) io_.run();
    }

    template<execute_mode model = execute_mode::SYNC, typename Function>
    void route(std::string const & name, const Function & f)
    {
        router::instance().route<model>(name, f);
    }

    template<execute_mode model = execute_mode::SYNC, typename Function, typename Self>
    void route(std::string const & name, const Function & f, Self * self)
    {
        router::instance().route<model>(name, f, self);
    }

    void response(int64_t conn_id, std::string result)
    {
        std::unique_lock<std::mutex> lock(mtx_);
        auto it = connections_.find(conn_id);
        if (it != connections_.end()) {
            it->second->response(result.data(), result.length());
        }
    }

    void set_debug_enable(debug_callback_t func)
    {
        assert(func);
        log_func_ = func;
    }

    void set_debug_disable()
    {
        log_func_ = nullptr;
    }

private:
    void do_accept()
    {
        auto conn = std::make_shared<connection>(io_, timeout_seconds_, log_func_);
        acceptor_.async_accept(conn->socket(), [=](boost::system::error_code ec) {
            if (ec) {
                if (log_func_) log_func_("accept failed: " + ec.message());
                return;
            }
            else {
                if (log_func_) log_func_("accept success. " + std::to_string(conn_id_));
                conn->start();
                std::unique_lock<std::mutex> lock(mtx_);
                conn->set_connection_id(conn_id_);
                connections_.emplace(conn_id_++, conn);
            }

            do_accept();
        });
    }

    void clean()
    {
        while (!intruppted_) {
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
        response(conn->connection_id(), result);
    }

private:
    std::atomic_bool intruppted_;
    io_context io_;
    tcp::acceptor acceptor_;
    std::vector<thread_ptr> work_threads_;
    thread_ptr check_thread_;
    std::size_t timeout_seconds_;

    std::unordered_map<int64_t, std::shared_ptr<connection>> connections_;
    int64_t conn_id_ = 0;
    std::mutex mtx_;
    size_t check_seconds_;

    debug_callback_t log_func_;
};


}  // namespace rpc
