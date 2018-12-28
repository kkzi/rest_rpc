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