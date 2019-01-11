#pragma once

#include "json/json_util.h"
#include "satcore_def.h"


template<>
struct adl_serializer<satcore::process_info>
{
    static void to_json(json & j, const satcore::process_info & t)
    {
        j = json{
            {"process_name", t.name},
            {"version", t.version},
        };
    }
};

template<>
struct adl_serializer<satcore::process_status>
{
    static void to_json(json & j, const satcore::process_status & t)
    {
        j = json{
            {"process_id", t.process_id},
            {"rpc_port", t.rpc_port},
            {"status", t.status},
        };
    }
};


template<>
struct adl_serializer<satcore::config::device>
{
    static void to_json(json & j, const satcore::config::device & t)
    {
        j = json{
            {"id", t.id},
            {"type", t.type},
            {"address", t.address},
        };
    }

    static void from_json(const json & j, satcore::config::device & t)
    {
        t.id = json_util::get<std::string>(j, "id");
        t.type = json_util::get<std::string>(j, "type");
        t.address = json_util::get<std::string>(j, "address");
    }
};


template<>
struct adl_serializer<satcore::config>
{
    static void to_json(json & j, const satcore::config & t)
    {
        j = json{
            {"satellite_id", t.satellite_id},
            {"port", t.port},
            {"tm_devices", t.tm_devices},
            {"tc_devices", t.tc_devices},
        };
    }

    static void from_json(const json & j, satcore::config & t)
    {
        t.satellite_id = json_util::get<std::string>(j, "satellite_id");
        t.port = json_util::get<uint16_t>(j, "port", 0);
        t.tm_devices = json_util::get<std::vector<satcore::config::device>>(j, "tm_devices");
        t.tc_devices = json_util::get<std::vector<satcore::config::device>>(j, "tc_devices");
    }
};

