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
        map_invokers_[name] = [=](connection * conn, const json & arguments, std::string & reply, execute_mode & exe_mode) {
            using args_tuple = typename function_traits<Function>::args_tuple_t;
            exe_mode = execute_mode::SYNC;
            try {
                auto tp = arguments.get<args_tuple>();
                call(func, conn, reply, tp);
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
        map_invokers_[name] = [=](connection * conn, const json & args, std::string & reply, execute_mode & exe_mode) {
            using args_tuple = typename function_traits<Function>::args_tuple_t;
            exe_mode = execute_mode::SYNC;
            try {
                auto tp = args.get<args_tuple>();
                call_member(func, self, conn, reply, tp);
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
            it->second(conn, args, result, mode);
            if (mode == execute_mode::SYNC && callback_to_server_) {
                callback_to_server_(url, result, conn, false);
            }
        }
        catch (const std::exception & ex) {
            result = packer::response(result_code::FAIL, ex.what());
            callback_to_server_("", result, conn, true);
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
        call_helper(const F & f, const std::index_sequence<idx...> &, const std::tuple<Args...> & tup, connection * conn)
    {
        return f(std::get<idx>(tup)...);
    }

    template<typename F, typename... Args>
    static typename std::enable_if<std::is_void<typename std::result_of<F(Args...)>::type>::value>::type
        call(const F & f, connection * conn, std::string & result, std::tuple<Args...> & tp)
    {
        call_helper(f, std::make_index_sequence<sizeof...(Args)>{}, tp, conn);
        result = packer::success();
    }

    template<typename F, typename... Args>
    static typename std::enable_if<!std::is_void<typename std::result_of<F(Args...)>::type>::value>::type
        call(const F & f, connection * conn, std::string & result, const std::tuple<Args...> & tp)
    {
        auto r = call_helper(f, std::make_index_sequence<sizeof...(Args)>{}, tp, conn);
        result = packer::success(r);
    }

    template<typename F, typename Self, size_t... idx, typename... Args>
    static typename std::result_of<F(Self, Args...)>::type call_member_helper(
        const F & f, Self * self, const std::index_sequence<idx...> &,
        const std::tuple<Args...> & tup, connection * conn = 0)
    {
        return (*self.*f)(std::get<idx>(tup)...);
    }

    template<typename F, typename Self, typename... Args>
    static typename std::enable_if<std::is_void<typename std::result_of<F(Self, Args...)>::type>::value>::type
        call_member(const F & f, Self * self, connection * conn, std::string & result, const std::tuple<Args...> & tp)
    {
        call_member_helper(f, self, typename std::make_index_sequence<sizeof...(Args)>{}, tp, conn);
        result = packer::success();
    }

    template<typename F, typename Self, typename... Args>
    static typename std::enable_if<!std::is_void<typename std::result_of<F(Self, Args...)>::type>::value>::type
        call_member(const F & f, Self * self, connection * conn, std::string & result, const std::tuple<Args...> & tp)
    {
        auto r = call_member_helper(f, self, typename std::make_index_sequence<sizeof...(Args)>{}, tp, conn);
        result = packer::success(r);
    }


private:
    std::map<std::string, std::function<void(connection *, const json & args, std::string & reply, execute_mode & mode)>> map_invokers_;
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

    void response(const char* data, size_t len)
    {
        assert(message_[1].size() >= len);
        message_[0] = boost::asio::buffer(&len, sizeof(int32_t));
        message_[1] = boost::asio::buffer((char*)data, len);
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

    void response(int64_t conn_id, const char * data, size_t size)
    {
        std::unique_lock<std::mutex> lock(mtx_);
        auto it = connections_.find(conn_id);
        if (it != connections_.end()) {
            it->second->response(data, size);
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
        response(conn->conn_id(), result.data(), result.size());
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


}  // namespace rpc
