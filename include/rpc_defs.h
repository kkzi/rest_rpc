#pragma once


#ifndef BOOST_COROUTINES_NO_DEPRECATION_WARNING
#define BOOST_COROUTINES_NO_DEPRECATION_WARNING
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <cstdint>
#include <json/json_util.h>

namespace rpc
{


// common defines
enum class result_code : int16_t
{
    OK = 0,
    FAIL = 1,
    BAD_REQUEST = 400,
    UNAUTHORIZED = 401,
    NOT_FOUND = 404,
    REQUEST_TIMEOUT = 408,
    INTERNAL_SERVER_ERROR = 500,
    NOT_IMPLEMENTED = 501,
    BAD_GATEWAY = 502,
    SERVICE_UNAVAILABLE = 503,
    GATEWAY_TIMEOUT = 504,
};



struct reply
{
    uint16_t code{ (uint16_t)result_code::OK };
    std::string description{ "" };
    json content{ nullptr };

    reply() = default;

    reply(result_code code, const std::string & description = "", const json & content = nullptr)
        : code((int)code)
        , description(std::move(description))
        , content(std::move(content))
    {

    }

    virtual ~reply() = default;
};

struct success : public reply
{
    success(const std::string & message = "", const json & content = nullptr)
        : reply(result_code::OK, message, content)
    {

    }
};


}  // !namespace rpc



template<> struct adl_serializer<rpc::reply>
{
    static void to_json(json & j, const rpc::reply & t)
    {
        j = json{ {"code", t.code} };
        if (!t.description.empty()) j["description"] = t.description;
        if (!t.content.is_null()) j["content"] = t.content;
    }

    static void from_json(const json & j, rpc::reply & t)
    {
        t.code = json_util::get<uint16_t>(j, "code", 0);
        t.description = json_util::get<std::string>(j, "description", "");
        t.content = json_util::get(j, "content", json::object());
    }
};
