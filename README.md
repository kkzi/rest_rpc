# rest_rpc


fork from https://github.com/qicosmos/rest_rpc.git

使用 [json](https://github.com/nlohmann/json) 替代 msgpack, 有性能损耗



# quick example

server code

```c++
#include "include_single/rpc_server.hpp"

using namespace rpc;

struct dummy
{
    int add(int a, int b) { return a + b; }
};

std::string translate(const std::string& orignal)
{
    std::string temp = orignal;
    for (auto& c : temp) {
        c = std::toupper(c);
    }
    return temp;
}

void hello(const std::string& str)
{
    std::cout << "hello " << str << std::endl;
}

void test(const std::string& str)
{
    std::cout << "hello " << str << std::endl;
}

int main()
{
    rpc::server server(9000, 4);

    dummy d;
    server.route("add", &dummy::add, &d);
    server.route("translate", translate);
    server.route("hello", test);

    server.route("test", test);

    //server.route("add2", [](int a, int b) ->int { return a + b; });

    server.run();

    std::string str;
    std::cin >> str;
}
```

client code

```c++
#include <iostream>
#include "test_client.hpp"
#include "../codec.h"

using namespace rest_rpc;
using namespace rest_rpc::rpc_service;

void test_add() {
	try{
		boost::asio::io_service io_service;
		test_client client(io_service);
		client.connect("127.0.0.1", "9000");

		auto result = client.call<int>("add", 1, 2);

		std::cout << result << std::endl; //output 3
	}
	catch (const std::exception& e){
		std::cout << e.what() << std::endl;
	}
}

void test_translate() {
	try {
		boost::asio::io_service io_service;
		test_client client(io_service);
		client.connect("127.0.0.1", "9000");

		auto result = client.call<std::string>("translate", "hello");
		std::cout << result << std::endl; //output HELLO
	}
	catch (const std::exception& e) {
		std::cout << e.what() << std::endl;
	}
}

void test_hello() {
	try {
		boost::asio::io_service io_service;
		test_client client(io_service);
		client.connect("127.0.0.1", "9000");

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
```

