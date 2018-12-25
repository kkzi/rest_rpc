#ifndef REST_RPC_CODEC_H_
#define REST_RPC_CODEC_H_

//#include <msgpack.hpp>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace rest_rpc {
namespace rpc_service {

struct msgpack_codec {

    template<typename... Args>
    static std::string pack_args(Args&&... args)
    {
        const auto & args_tuple = std::make_tuple(std::forward<Args>(args)...);
        const auto & buffer = json(args_tuple).dump();
        return std::move(buffer);
    }

    template<typename Arg, typename... Args, typename = typename std::enable_if<std::is_enum<Arg>::value>::type>
    static std::string pack_args_str(Arg arg, Args&&... args)
    {
        const auto & args_tuple = std::make_tuple(std::forward<Args>(args)...);
        //json::dump()
        const auto & buffer = json(args_tuple).dump();
        return std::move(buffer);
    }

    template<typename T>
    std::string pack(T&& t) const
    {
        const auto & buffer = json(t).dump();
        return std::move(buffer);
    }

    template<typename T>
    T unpack(char const* data, size_t length)
    {
        try {
            auto arr = json::parse(std::string(data, length));
            return arr.at(0).get<T>();
        }
        catch (const std::exception & e) { 
            throw std::invalid_argument(std::string("args not match. ") + e.what());
        }
    }

};
}  // namespace rpc_service
}  // namespace rest_rpc

#endif  // REST_RPC_CODEC_H_