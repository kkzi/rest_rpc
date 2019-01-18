# rpc_server 使用说明



## 说明

使用了 boost 库和 c++11 的部分特性，依赖 json 库



## rcp_server.hpp



### 实例化

```c++
rpc::server s(60060, 1);	// 第一个参数是绑定的端口号；第二个参数是rpc server内部的线程数量
```



### 注册被调用接口

```c++
s.route("add", [](int a, int b){ return a + b; });

struct dummy
{
    int add(int a, int b)
    {
        return a + b;
    }
}
dummy d;
s.route("add", &dummy::add, &d);
```



注册的时候只管写函数名称，不用关心函数有几个参数和返回值是什么类型的。函数参数和返回值支持自定义结构 ，不支持指针（rpc 没有传递指针的应用场景）



### 使用自定义类型作为函数的参数和返回值

**被调用接口的参数和返回值不支持使用指针**

当使用自定义类型时，需要定义序列化和反序列化方法。rpc_server使用json作为数据传输类型，因此需要定义自定义结构到json和从json到自定义结构的函数，示例如下：

```c++
// 自定义结构类型，本例有结构嵌套的情况
namespace satcore
{
    struct config
    {
        struct device
        {
            std::string id;
            std::string type;
            std::string address;
        };

        std::string satellite_id;
        uint16_t port;
        std::vector<device> tm_devices;
        std::vector<device> tc_devices;
    };
}

// 序列化和反序列化函数，两个结构的 key 值需要一一对应上
// json 结构可以很方便地从 stl 结构相互转换，更多用法

#include <json/json_util.h>

// satcore::config::device 结构转换
template<>
struct adl_serializer<satcore::config::device>
{
	// 从自定义结构到json的转换函数
    static void to_json(json & j, const satcore::config::device & t)
    {
        j = json{
            {"id", t.id},
            {"type", t.type},
            {"address", t.address},
        };
    }
    
	// 从json到自定义结构的转换
    static void from_json(const json & j, satcore::config::device & t)
    {
        t.id = json_util::get<std::string>(j, "id");
        t.type = json_util::get<std::string>(j, "type");
        t.address = json_util::get<std::string>(j, "address");
    }
};


// satcore::config 结构转换
template<>
struct adl_serializer<satcore::config>
{
	// 从自定义结构到json的转换函数
    static void to_json(json & j, const satcore::config & t)
    {
        j = json{
            {"satellite_id", t.satellite_id},
            {"port", t.port},
            {"tm_devices", t.tm_devices},
            {"tc_devices", t.tc_devices},
        };
    }

	// 从json到自定义结构的转换
    static void from_json(const json & j, satcore::config & t)
    {
        t.satellite_id = json_util::get<std::string>(j, "satellite_id");
        t.port = json_util::get<uint16_t>(j, "port", 0);
        t.tm_devices = json_util::get<std::vector<satcore::config::device>>(j, "tm_devices");
        t.tc_devices = json_util::get<std::vector<satcore::config::device>>(j, "tc_devices");
    }
};
```



当结构有变化时，需要同步修改对应的序列化和反序列化函数后才生效。





### 启动

```c++
s.run();	// 这个方法并不会阻塞主线程的执行。
			// 因此如果只是启动一个rpc服务器，需要使用其它方式阻塞主线程。

// 阻塞主线程
while (true) 
{
    
}

// 或者使用 boost 的 io_context
boost::asio::io_context io;
boost::asio::io_context::work work(io);
s.run();
io.run();
```





## rpc_client.hpp

rpc_client 是访问 rpc::server 的同步接口封装。



### 实例化

```c++
boost::asio::io_context io;
rpc::client client(io);
```



### 连接server

```c++
// 当 server 不在线时会抛异常
try {
    client.connect("127.0.0.1", 60050);
}
catch (const std::exception & e) {
    std::cout << e.what() << std::endl;
    return;
}
```


### rpc调用

```c++
try {
    auto sum = client.call<int>("add", 1, 2);	// 调用两个参数为int类型，返回值也为int的接口
    std::cout << sum << std::endl;

    auto text = client.call<std::string>("translate", "hello");
    std::cout << text << std::endl;

    client.call<void>("start");		// 调用无返回值无参数的接口

    satcore::config cfg;
    cfg.satellite_id = "11111";
    cfg.port = 2222;
    client.call("/config/set", cfg);	// 调用自定义结构参数的接口
    									// 使用自定义结构时需要引入结构对应的序列化反序列化函数
    									// 建议把结构定义和序列化写成单独文件，c/s两边都可以用
}
catch (const std::exception & e) {
    std::cout << e.what() << std::endl;
}
```





## message_hub

### 创建hub

```c++
rpc::message_hub hub;
bool success = hub.create(6666);		// 指定端口号为6666
```



### 订阅topic

```c++
void on_message(const std::string& topic, const std::string& message)
{
    std::cout << message << std::endl;
}

rpc::message_hub hub;
bool success = hub.connect("127.0.0.1", 6666);
hub.subscribe("topic.ats.test", on_message);
```



发布消息

```c++
rpc::message_hub hub;
bool success = hub.connect("127.0.0.1", 6666);
hub.publish("hello worrld");
```

