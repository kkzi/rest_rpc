#pragma once

#include "json.hpp"

using namespace nlohmann;

namespace json_util
{
    template<typename T>
    T get(const json &obj, const std::string &key, const T &default_value = T())
    {
        return obj.count(key) ? obj.at(key).get<T>() : default_value;
    }

    template<typename T>
    T get(const json &obj, int pos, const T &default_value = T())
    {
        return obj.size() > pos ? obj.at(pos).get<T>() : default_value;
    }
}
