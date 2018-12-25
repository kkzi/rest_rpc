#pragma once
#include <string>
#include <boost/asio.hpp>
#include "../codec.h"

using boost::asio::ip::tcp;
using namespace rest_rpc;
using namespace rest_rpc::rpc_service;

class test_client : private boost::noncopyable
{
public:
    test_client(boost::asio::io_service& io_service)
        : io_service_(io_service)
        , socket_(io_service) 
    {

    }

    void connect(const std::string& addr, const std::string& port) 
    {
        tcp::resolver resolver(io_service_);
        tcp::resolver::query query(tcp::v4(), addr, port);
        tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);

        boost::asio::connect(socket_, endpoint_iterator);
    }

    std::string call(const char* data, size_t size) 
    {
        bool r = send(data, size);
        if (!r) {
            throw std::runtime_error("call failed");
        }

        socket_.receive(boost::asio::buffer(&size, 4));
        std::string recv_data;
        recv_data.resize(size);
        socket_.receive(boost::asio::buffer(&recv_data[0], size));
        return recv_data;
    }

    std::string call(std::string content) 
    {
        return call(content.data(), content.size());
    }

    template<typename T = void, typename... Args>
    typename std::enable_if<std::is_void<T>::value>::type call(std::string rpc_name, Args... args) 
    {
        auto ret = msgpack_codec::pack_args(rpc_name, args...);
        call(ret.data(), ret.size());
        return;
    }

    template<typename T, typename... Args>
    typename std::enable_if<!std::is_void<T>::value, T>::type call(std::string rpc_name, Args... args) 
    {
        msgpack_codec codec;
        auto ret = codec.pack_args(rpc_name, args...);

        auto result = call(ret.data(), ret.size());

        auto tp = codec.unpack<T>(result.data(), result.size());
        return tp;
        //try {
        //    return tp.value(0).get<T>();
        //    //return tp.get<T>(0);
        //}
        //catch (const std::exception & e) {
        //    throw std::logic_error(e.what());
        //}
    }

private:
    bool send(const char* data, size_t size) 
    {
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

