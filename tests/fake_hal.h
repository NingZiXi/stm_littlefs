/**
 * @file    fake_hal.h
 * @brief   主机测试仅需 Flash 公共头文件的 HAL 状态类型
 */
#ifndef FAKE_HAL_H
#define FAKE_HAL_H
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
#endif
