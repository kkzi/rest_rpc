#pragma once

#include <vector>
#include <string>
#include <cstdint>


namespace rest_rpc {

enum class result_code : int16_t {
    OK = 0,
    FAIL = 1,
};

enum class error_code {
    OK,
    UNKNOWN,
    FAIL,
    TIMEOUT,
    CANCEL,
    BADCONNECTION,
};

static const size_t MAX_BUF_LEN = 1048576 * 10;
static const size_t HEAD_LEN = 4;
static const size_t PAGE_SIZE = 1024 * 1024;

}  // namespace rest_rpc
