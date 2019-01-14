#pragma once

#include "rpc_defs.h"
#include <string>
#include <iostream>
#include <mutex>
#include <boost/asio.hpp>


namespace rpc
{
using boost::asio::ip::tcp;
using async_callback_t = std::function<void(const rpc::reply &)>;


class socket_helper
{
public:
    static void connect(tcp::socket & socket, const std::string & addr, uint16_t port)
    {
        tcp::resolver resolver(socket.get_io_context());
        tcp::resolver::query query(tcp::v4(), addr, std::to_string(port));
        tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
        boost::asio::connect(socket, endpoint_iterator);
    }

    template<typename... Args>
    static std::string pack_request(const std::string & url, Args && ... args)
    {
        return json{
            {"url", url},
            {"arguments", std::make_tuple(std::forward<Args>(args)...)},
        }.dump();
    }

    static std::string request(tcp::socket & socket, const std::string & data)
    {
        auto size = data.size();
        std::vector<boost::asio::const_buffer> message;
        message.push_back(boost::asio::buffer(&size, 4));
        message.push_back(boost::asio::buffer(data, size));
        boost::asio::write(socket, message);

        size = 0;
        socket.receive(boost::asio::buffer(&size, 4));
        std::string recv_data;
        recv_data.resize(size);
        boost::asio::read(socket, boost::asio::buffer(&recv_data[0], size));
        return recv_data;
    }

    template<typename... Args>
    static json call_helper(tcp::socket & socket, const std::string & rpc_name, Args && ... args)
    {
        const auto & req = pack_request(rpc_name, args...);
        const auto & rep = json::parse(request(socket, req));
        auto ret_code = json_util::get<int>(rep, "code", 0);
        if (ret_code != 0) {
            throw std::runtime_error(json_util::get<std::string>(rep, "description", ""));
        }
        return rep;
    }
};



class client final : private boost::noncopyable
{
public:
    client()
        : socket_(io_)
    {
    }

    ~client() = default;

    void connect(const std::string & addr, uint16_t port)
    {
        return socket_helper::connect(socket_, addr, port);
    }

    template<typename T = void, typename... Args>
    typename std::enable_if<std::is_void<T>::value>::type call(const std::string & rpc_name, Args... args)
    {
        const auto & rep = socket_helper::call_helper(socket_, rpc_name, args...);
        auto ret_code = json_util::get<int>(rep, "code", 0);
        if (ret_code != 0) {
            throw std::runtime_error(json_util::get<std::string>(rep, "description", ""));
        }
    }

    template<typename T, typename... Args>
    typename std::enable_if<!std::is_void<T>::value, T>::type call(const std::string & rpc_name, Args... args)
    {
        const auto & rep = socket_helper::call_helper(socket_, rpc_name, args...);
        auto ret_code = json_util::get<int>(rep, "code", 0);
        if (ret_code != 0) {
            throw std::runtime_error(json_util::get<std::string>(rep, "description", ""));
        }
        return json_util::get<T>(rep, "content");
    }

private:
    boost::asio::io_context io_;
    tcp::socket socket_;
};



class async_client final : private boost::noncopyable
{
public:
    async_client()
        : socket_(io_)
    {
        work_guard_ = std::make_shared<boost::asio::io_context::work>(work_io_);
        work_thread_ = std::thread([=] { work_io_.run(); });
    }

    ~async_client()
    {
        work_guard_.reset();
        work_io_.stop();
        work_thread_.join();
    }

    void connect(const std::string & addr, uint16_t port)
    {
        return socket_helper::connect(socket_, addr, port);
    }

    template<typename... Args>
    void call(const std::string & rpc_name, async_callback_t func, Args... args)
    {
        assert(func);
        work_io_.post([=]() {
            const auto & rep = socket_helper::call_helper(socket_, rpc_name, args...);
            func(rpc::reply(rep));
        });
    }


private:
    boost::asio::io_context io_;
    tcp::socket socket_;
    std::thread work_thread_;
    boost::asio::io_context work_io_;
    std::shared_ptr<boost::asio::io_context::work> work_guard_;
};

} // namespace rpc
