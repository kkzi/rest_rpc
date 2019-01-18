#include <iostream>
#include "rpc_client.hpp"
#include "http_server.hpp"
#include "satcore_def.h"
#include "satcore_serialization.h"


class dummy
{
public:
    void add(const rpc::reply & rep)
    {
        std::cout << json_util::get<int>(rep, "content", 0) << std::endl;

        rep.code == 0;
        rep.description;

        std::cout << json_util::get<int>(rep, "content", 0) << std::endl;

    }
};

int main()
{

    rpc::async_client async_client;
    try {
        async_client.connect("127.0.0.1", 9000);

        async_client.call("test", [](const rpc::reply & rep) {
            std::cout << json(rep).dump() << std::endl;
        });

        async_client.call("hello", [](const rpc::reply & rep) {
            std::cout << json(rep).dump() << std::endl;
        }, "hello");

        dummy d;
        async_client.call("add2", std::bind(&dummy::add, &d, std::placeholders::_1), 100, 1);
    }
    catch (const std::exception & e) {
        std::cout << e.what() << std::endl;;
    }


    try {
        //auto sum = client.call<int>("add", 1, 2);
        //std::cout << sum << std::endl;

        //auto text = client.call<std::string>("translate", "hello");
        //std::cout << text << std::endl;


        //client.call<void>("start");

        //satcore::config cfg;
        //cfg.satellite_id = "11111";
        //cfg.port = 2222;

        //client.call<void>("/config/set", json{
        //    {"satellite_id", "test"},
        //    {"port", 111111},
        //    }.dump());

        //client.call("/config/set", cfg);
        //Sleep(20 * 1000);

        //auto cfg2 = client.call<satcore::config>("/config");
        //std::cout << cfg2.satellite_id;


        //client.call<void>("test");
        //client.call<void>("hello", "hello");
        //auto sum = client.call<int>("add2", 3, 3);
        //std::cout << sum << std::endl;



    }
    catch (const std::exception & e) {
        std::cout << e.what() << std::endl;
    }


    //boost::asio::io_context io;
    //rpc::http_server http(io, 9999);
    //http.run();
    //io.run();



    return 0;
}


