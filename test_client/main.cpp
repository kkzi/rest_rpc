#include <iostream>
#include "test_client.hpp"

using namespace rpc;

static uint16_t port = 9000;

void test_add() {
    try {
        boost::asio::io_service io_service;
        test_client client(io_service);
        client.connect("127.0.0.1", port);

        auto result = client.call<int>("add", 1, 2);

        std::cout << result << std::endl;
    }
    catch (const std::exception& e) {
        std::cout << e.what() << std::endl;
    }
}

void test_translate() {
    try {
        boost::asio::io_service io_service;
        test_client client(io_service);
        client.connect("127.0.0.1", port);

        auto result = client.call<std::string>("translate", "hello");
        std::cout << result << std::endl;
    }
    catch (const std::exception& e) {
        std::cout << e.what() << std::endl;
    }
}

void test_hello() {
    try {
        boost::asio::io_service io_service;
        test_client client(io_service);
        client.connect("127.0.0.1", port);

        client.call("hello", "purecpp");
    }
    catch (const std::exception& e) {
        std::cout << e.what() << std::endl;
    }
}

int main() {
    test_hello();
    test_add();
    test_translate();
    return 0;
}