/**
 * @file    headers.cpp
 * @brief   C++ 公共头文件与 C 链接检查
 */
#include "stm_littlefs.h"
extern "C" stm_err_t cpp_header_check()
{
    littlefs_handle_t handle = nullptr;
    return littlefs_delete(&handle);
}
