#include <iostream>
#include "rpc_client.hpp"

int main()
{
    boost::asio::io_service io_service;
    rpc::client client(io_service);
    client.connect("127.0.0.1", 9000);

    try {
        client.call<void>("test", "purecpp");

        auto sum = client.call<int>("add", 1, 2);
        std::cout << sum << std::endl;

        auto text = client.call<std::string>("translate", "hello");
        std::cout << text << std::endl;
    }
    catch (const std::exception & e) {
        std::cout << e.what() << std::endl;
    }
    return 0;
}