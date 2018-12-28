#pragma once

#include "json/json_util.h"
#include "common.hpp"


namespace rpc 
{

struct packer
{
    template<typename ... Args>
    static std::string request(const std::string & url, Args && ... args)
    {
        return json{
            {"url", url},
            {"arguments", std::make_tuple(std::forward<Args>(args)...)},
        }.dump();
    }

    template<typename T>
    static std::string response(result_code code, const std::string & message, const T & content)
    {
        return json{ {"code", (int)code}, {"description", message}, {"content", content} }.dump();
    }

    static std::string response(result_code code, const std::string & message)
    {
        return json{ {"code", (int)code}, {"description", message} }.dump();
    }

    template<typename T>
    static std::string success(const T & content)
    {
        return json{ {"code", result_code::OK}, {"content", content} }.dump();
    }

    static std::string success()
    {
        return json{ {"code", result_code::OK} }.dump();
    }


};

} // namespace rpc 
