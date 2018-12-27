#pragma once
#include <string>
#include <iostream>
#include <boost/asio.hpp>
#include <json/json_util.h>
#include "packer.hpp"
#include "meta_util.hpp"

using boost::asio::ip::tcp;
using namespace rpc;

class test_client : private boost::noncopyable
{
public:
    test_client(boost::asio::io_service& io_service)
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

    std::string call(const std::string & content)
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

    template<typename T = void, typename... Args>
    typename std::enable_if<std::is_void<T>::value>::type call(std::string rpc_name, Args... args)
    {
        auto ret = packer::request(rpc_name, args...);
        call(ret);
        return;
    }

    template<typename T, typename... Args>
    typename std::enable_if<!std::is_void<T>::value, T>::type call(std::string rpc_name, Args... args)
    {
        auto req = packer::request(rpc_name, args...);
        const auto & rep = json::parse(call(req));

        std::string text = rep.dump();

        //auto code = json_util::get<int>(rep, "code", -1);
        //auto content = json_util::get<T>(rep, "content");
        return json_util::get<T>(rep, "content");
    }

private:
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

    boost::asio::io_service & io_service_;
    tcp::socket socket_;
};

