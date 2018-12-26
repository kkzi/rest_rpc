#pragma once


#include <boost/asio.hpp>
#include "common.hpp"
#include "packer.hpp"
#include "meta_util.hpp"
#include "json/json_util.h"

namespace rest_rpc {

enum class execute_mode { SYNC, ASYNC };
class connection;

class router : boost::noncopyable
{
public:
    static router & get()
    {
        static router instance;
        return instance;
    }

    template<execute_mode model, typename Function>
    void register_handler(std::string const& name, Function f)
    {
        return register_nonmember_func<model>(name, std::move(f));
    }

    template<execute_mode model, typename Function, typename Self>
    void register_handler(std::string const& name, const Function& f, Self* self)
    {
        return register_member_func<model>(name, f, self);
    }

    void remove_handler(std::string const& name)
    {
        this->map_invokers_.erase(name);
    }

    void set_callback(const std::function<void(const std::string&, const std::string&, connection*, bool)>& callback)
    {
        callback_to_server_ = callback;
    }

    template<typename T>
    void route(const std::string & data, T conn)
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
            execute_mode model;
            it->second(conn, args, result, model);
            if (model == execute_mode::SYNC && callback_to_server_) {
                callback_to_server_(url, result, conn, false);
            }
        }
        catch (const std::exception & ex) {
            result = packer::response(result_code::FAIL, ex.what());
            callback_to_server_("", result, conn, true);
        }
    }

    router() = default;

private:
    router(const router&) = delete;
    router(router&&) = delete;

    template<typename F, size_t... Indexes, typename... Args>
    static typename std::result_of<F(connection*, Args...)>::type
        call_helper(const F& f, const std::index_sequence<Indexes...>&, const std::tuple<Args...>& tup, connection* ptr)
    {
        return f(ptr, std::get<Indexes>(tup)...);
    }

    template<typename F, typename... Args>
    static typename std::enable_if<std::is_void<typename std::result_of<F(connection*, Args...)>::type>::value>::type
        call(const F& f, connection* ptr, std::string& result, std::tuple<Args...>& tp)
    {
        call_helper(f, std::make_index_sequence<sizeof...(Args)>{}, tp, ptr);
        result = packer::success();
    }

    template<typename F, typename... Args>
    static typename std::enable_if<!std::is_void<typename std::result_of<F(connection*, Args...)>::type>::value>::type
        call(const F& f, connection* ptr, std::string& result, const std::tuple<Args...>& tp)
    {
        auto r = call_helper(f, std::make_index_sequence<sizeof...(Args)>{}, tp, ptr);
        result = packer::success(r);
    }

    template<typename F, typename Self, size_t... Indexes, typename... Args>
    static typename std::result_of<F(Self, connection*, Args...)>::type call_member_helper(
        const F& f, Self* self, const std::index_sequence<Indexes...>&,
        const std::tuple<Args...>& tup, connection* ptr = 0)
    {
        return (*self.*f)(ptr, std::get<Indexes>(tup)...);
    }

    template<typename F, typename Self, typename... Args>
    static typename std::enable_if<std::is_void<typename std::result_of<F(Self, connection*, Args...)>::type>::value>::type
        call_member(const F& f, Self* self, connection* ptr, std::string& result, const std::tuple<Args...>& tp)
    {
        call_member_helper(f, self, typename std::make_index_sequence<sizeof...(Args)>{}, tp, ptr);
        result = packer::success();
    }

    template<typename F, typename Self, typename... Args>
    static typename std::enable_if<!std::is_void<typename std::result_of<F(Self, connection*, Args...)>::type>::value>::type
        call_member(const F& f, Self* self, connection* ptr, std::string& result, const std::tuple<Args...>& tp)
    {
        auto r = call_member_helper(f, self, typename std::make_index_sequence<sizeof...(Args)>{}, tp, ptr);
        result = packer::success(r);
    }

    template<typename Function, execute_mode mode = execute_mode::SYNC>
    struct invoker
    {
        template<execute_mode model>
        static inline void apply(const Function & func, connection * conn, const json & args, std::string & result, execute_mode & exe_model)
        {
            using args_tuple = typename function_traits<Function>::args_tuple_t;

            exe_model = execute_mode::SYNC;
            try {
                auto tp = args.get<args_tuple>();
                call(func, conn, result, tp);
                exe_model = model;
            }
            catch (std::invalid_argument & e) {
                result = packer::response(result_code::FAIL, e.what());
            }
            catch (const std::exception & e) {
                result = packer::response(result_code::FAIL, e.what());
            }
        }

        template<execute_mode model, typename Self>
        static inline void apply_member(const Function & func, Self * self, connection * conn, const json & args, std::string & result, execute_mode & exe_model)
        {
            using args_tuple = typename function_traits<Function>::args_tuple_t;
            exe_model = execute_mode::SYNC;
            try {
                auto tp = args.get<args_tuple>();
                call_member(func, self, conn, result, tp);
                exe_model = model;
            }
            //catch (std::invalid_argument & e) {
            //    result = packer::response(result_code::FAIL, e.what());
            //}
            catch (const std::exception & e) {
                result = packer::response(result_code::FAIL, e.what());
            }
        }
    };

    template<execute_mode model, typename Function>
    void register_nonmember_func(std::string const& name, Function f)
    {
        using namespace std::placeholders;
        this->map_invokers_[name] = { std::bind(&invoker<Function>::template apply<model>, std::move(f), _1, _2, _3, _4) };
    }

    template<execute_mode model, typename Function, typename Self>
    void register_member_func(const std::string& name, const Function& f, Self* self)
    {
        using namespace std::placeholders;
        this->map_invokers_[name] = { std::bind(&invoker<Function>::template apply_member<model, Self>, f, self, _1, _2, _3, _4) };
    }

    std::map<std::string, std::function<void(connection*, const json & arguments, std::string & reply, execute_mode & model)>> map_invokers_;
    std::function<void(const std::string &, const std::string &, connection*, bool)> callback_to_server_;
};

}  // namespace rest_rpc