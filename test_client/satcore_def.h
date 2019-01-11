#pragma once

#include <string>
#include <cstdint>
#include <vector>


namespace satcore
{


const static std::string PROCESS_NAME = "SatCore";
const static std::string PROCESS_VERSION = "1.0.0";


/*!
 * \class process_info
 *
 * \brief 描述进程信息
 *
 * \author xuan.guo@atomdatatech.com
 * \date December 2018
 */
struct process_info
{
    std::string name{ PROCESS_NAME };
    std::string version{ PROCESS_VERSION };
};


/*!
 * \class process_status
 *
 * \brief 描述进程状态信息
 *
 * \author xuan.guo@atomdatatech.com
 * \date December 2018
 */
struct process_status
{
    uint32_t process_id;
    uint16_t rpc_port;
    std::string status;
};


/*!
 * \class config
 *
 * \brief 描述SatCore进程接收的参数配置
 *
 * \author xuan.guo@atomdatatech.com
 * \date December 2018
 */
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



}  // namespace satcore
