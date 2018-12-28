#pragma once


#include <boost/asio.hpp>
#include "common.hpp"
#include "packer.hpp"
#include "json/json_util.h"

namespace rpc {

enum class execute_mode { SYNC, ASYNC };
class connection;



template<typename T>
struct function_traits;

template<typename Ret, typename... Args>
struct function_traits<Ret(Args...)>
{
public:
    using args_tuple_t = std::tuple<std::remove_const_t<std::remove_reference_t<Args>>...>;
};

template<typename Ret, typename... Args>
struct function_traits<Ret(*)(Args...)> : function_traits<Ret(Args...)> {};

template <typename Ret, typename... Args>
struct function_traits<std::function<Ret(Args...)>> : function_traits<Ret(Args...)> {};

template <typename ReturnType, typename ClassType, typename... Args>
struct function_traits<ReturnType(ClassType::*)(Args...)> : function_traits<ReturnType(Args...)> {};

template <typename ReturnType, typename ClassType, typename... Args>
struct function_traits<ReturnType(ClassType::*)(Args...) const> : function_traits<ReturnType(Args...)> {};

template<typename Callable>
struct function_traits : function_traits<decltype(&Callable::operator())> {};



class router : boost::noncopyable
{
public:
    static router & get()
    {
        static router instance;
        return instance;
    }

    template<execute_mode mode, typename Function>
    void route(const std::string & name, Function func)
    {
        using namespace std::placeholders;
        map_invokers_[name] = [=](connection * conn, const json & arguments, std::string & reply, execute_mode & exe_mode) {
            using args_tuple = typename function_traits<Function>::args_tuple_t;
            exe_mode = execute_mode::SYNC;
            try {
                auto tp = arguments.get<args_tuple>();
                call(func, conn, reply, tp);
                exe_mode = mode;
            }
            catch (const std::exception & e) {
                reply = packer::response(result_code::FAIL, e.what());
            }
        };
    }

    template<execute_mode mode, typename Function, typename Self>
    void route(const std::string & name, const Function & func, Self * self)
    {
        using namespace std::placeholders;
        map_invokers_[name] = [=](connection * conn, const json & args, std::string & reply, execute_mode & exe_mode) {
            using args_tuple = typename function_traits<Function>::args_tuple_t;
            exe_mode = execute_mode::SYNC;
            try {
                auto tp = args.get<args_tuple>();
                call_member(func, self, conn, reply, tp);
                exe_mode = mode;
            }
            catch (const std::exception & e) {
                reply = packer::response(result_code::FAIL, e.what());
            }
        };
    }

    template<typename T>
    void execute_function(const std::string & data, T conn)
    {
        std::string result;
        try {
            const auto & req = json::parse(data);
            const auto & url = json_util::get<std::string>(req, "url", "");

            auto it = map_invokers_.find(url);
            if (it == map_invokers_.end()) {
                result = packer::response(result_code::FAIL, "unknown request: " + url);
                callback_to_server_(url, result, conn, true);
                return;
            }

            const auto & args = json_util::get(req, "arguments", json::array());
            execute_mode mode;
            it->second(conn, args, result, mode);
            if (mode == execute_mode::SYNC && callback_to_server_) {
                callback_to_server_(url, result, conn, false);
            }
        }
        catch (const std::exception & ex) {
            result = packer::response(result_code::FAIL, ex.what());
            callback_to_server_("", result, conn, true);
        }
    }

    void unroute(const std::string & name)
    {
        map_invokers_.erase(name);
    }

    void set_callback(const std::function<void(const std::string &, const std::string &, connection *, bool)> & callback)
    {
        callback_to_server_ = callback;
    }


    router() = default;

private:
    router(const router &) = delete;
    router(router &&) = delete;

    template<typename F, size_t... idx, typename... Args>
    static typename std::result_of<F(Args...)>::type
        call_helper(const F & f, const std::index_sequence<idx...> &, const std::tuple<Args...> & tup, connection * conn)
    {
        return f(std::get<idx>(tup)...);
    }

    template<typename F, typename... Args>
    static typename std::enable_if<std::is_void<typename std::result_of<F(Args...)>::type>::value>::type
        call(const F & f, connection * conn, std::string & result, std::tuple<Args...> & tp)
    {
        call_helper(f, std::make_index_sequence<sizeof...(Args)>{}, tp, conn);
        result = packer::success();
    }

    template<typename F, typename... Args>
    static typename std::enable_if<!std::is_void<typename std::result_of<F(Args...)>::type>::value>::type
        call(const F & f, connection * conn, std::string & result, const std::tuple<Args...> & tp)
    {
        auto r = call_helper(f, std::make_index_sequence<sizeof...(Args)>{}, tp, conn);
        result = packer::success(r);
    }

    template<typename F, typename Self, size_t... idx, typename... Args>
    static typename std::result_of<F(Self, Args...)>::type call_member_helper(
        const F & f, Self * self, const std::index_sequence<idx...> &,
        const std::tuple<Args...> & tup, connection * conn = 0)
    {
        return (*self.*f)(std::get<idx>(tup)...);
    }

    template<typename F, typename Self, typename... Args>
    static typename std::enable_if<std::is_void<typename std::result_of<F(Self, Args...)>::type>::value>::type
        call_member(const F & f, Self * self, connection * conn, std::string & result, const std::tuple<Args...> & tp)
    {
        call_member_helper(f, self, typename std::make_index_sequence<sizeof...(Args)>{}, tp, conn);
        result = packer::success();
    }

    template<typename F, typename Self, typename... Args>
    static typename std::enable_if<!std::is_void<typename std::result_of<F(Self, Args...)>::type>::value>::type
        call_member(const F & f, Self * self, connection * conn, std::string & result, const std::tuple<Args...> & tp)
    {
        auto r = call_member_helper(f, self, typename std::make_index_sequence<sizeof...(Args)>{}, tp, conn);
        result = packer::success(r);
    }


private:
    std::map<std::string, std::function<void(connection *, const json & args, std::string & reply, execute_mode & mode)>> map_invokers_;
    std::function<void(const std::string &, const std::string &, connection *, bool)> callback_to_server_;
};

} // namespace rpc
