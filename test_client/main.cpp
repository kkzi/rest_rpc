#include <iostream>
#include "rpc_client.hpp"
#include "http_server.hpp"
#include "satcore_def.h"
#include "satcore_serialization.h"


void a()
{

}


int main()
{

    //boost::asio::io_service io_service;
    rpc::client client;

    try {
        //client.connect("127.0.0.1", 60050);
        client.connect("127.0.0.1", 9000);
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

        client.async_call("start", []() {

        });

        satcore::config cfg;
        cfg.satellite_id = "11111";
        cfg.port = 2222;

        //client.call<void>("/config/set", json{
        //    {"satellite_id", "test"},
        //    {"port", 111111},
        //    }.dump());

        client.call("/config/set", cfg);
        Sleep(20 * 1000);

        auto cfg2 = client.call<satcore::config>("/config");
        std::cout << cfg2.satellite_id;
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