#pragma once

#include <string>
#include <iostream>
#include <boost/asio.hpp>
#include <json/json_util.h>


namespace rpc
{
using boost::asio::ip::tcp;

class client : private boost::noncopyable
{
public:
    client(boost::asio::io_service& io_service)
        : io_service_(io_service)
        , socket_(io_service)
    {

    }

    void connect(const std::string& addr, uint16_t port)
    {
        tcp::resolver resolver(io_service_);
        tcp::resolver::query query(tcp::v4(), addr, std::to_string(port));
        tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
        boost::asio::connect(socket_, endpoint_iterator);
    }

    template<typename T = void, typename... Args>
    typename std::enable_if<std::is_void<T>::value>::type call(const std::string & rpc_name, Args... args)
    {
        const auto & req = pack_request(rpc_name, args...);
        const auto & rep = json::parse(call_helper(req));
        auto ret_code = json_util::get<int>(rep, "code", 0);
        if (ret_code != 0) {
            throw std::runtime_error(json_util::get<std::string>(rep, "description", ""));
        }
    }

    template<typename T, typename... Args>
    typename std::enable_if<!std::is_void<T>::value, T>::type call(const std::string & rpc_name, Args... args)
    {
        const auto & req = pack_request(rpc_name, args...);
        const auto & rep = json::parse(call_helper(req));
        auto ret_code = json_util::get<int>(rep, "code", 0);
        if (ret_code != 0) {
            throw std::runtime_error(json_util::get<std::string>(rep, "description", ""));
        }
        return json_util::get<T>(rep, "content");
    }

private:
    template<typename ... Args>
    static std::string pack_request(const std::string & url, Args && ... args)
    {
        return json{
            {"url", url},
            {"arguments", std::make_tuple(std::forward<Args>(args)...)},
        }.dump();
    }

    std::string call_helper(const std::string & content)
    {
        bool r = send(content);
        if (!r) {
            throw std::runtime_error("call failed");
        }

        int32_t size = 0;
        socket_.receive(boost::asio::buffer(&size, 4));
        std::string recv_data;
        recv_data.resize(size);
        socket_.receive(boost::asio::buffer(&recv_data[0], size));
        return recv_data;
    }

    bool send(const std::string & data)
    {
        auto size = data.size();
        std::vector<boost::asio::const_buffer> message;
        message.push_back(boost::asio::buffer(&size, 4));
        message.push_back(boost::asio::buffer(data, size));
        boost::system::error_code ec;
        boost::asio::write(socket_, message, ec);
        return ec == 0;
    }

private:
    boost::asio::io_service & io_service_;
    tcp::socket socket_;
};

} // namespace rpc
