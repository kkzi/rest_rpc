#pragma once

#include <vector>
#include <string>
#include <cstdint>


namespace rest_rpc
{

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

static const size_t MAX_BUF_LEN = 1048576 * 10;
static const size_t HEAD_LEN = 4;
static const size_t PAGE_SIZE = 1024 * 1024;

}  // namespace rest_rpc
