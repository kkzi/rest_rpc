#pragma once

#include <thread>
#include "io_context_pool.hpp"
#include "connection.hpp"
#include "router.hpp"


namespace rpc
{
using boost::asio::ip::tcp;

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
        //thd_ = std::make_shared<std::thread>(std::bind( &io_context_pool::run, io_pool_));
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

} // namespace rpc
