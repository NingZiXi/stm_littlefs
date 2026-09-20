# 最小参考示例

`main.c` 是独立参考入口，有 `main()` 和完整 `board_init()`，不自动加入库、不依赖日志或 RTT。

参考硬件 STM32H723ZG + W25Q256JV-IQ，25 MHz HSE、CPU 550 MHz、HCLK 275 MHz，OSPI 34.375 MHz。GPIO/OCTOSPI 及 MSP 由参考工程的 CubeMX 文件提供；同时需要启动文件、HAL/CMSIS、SysTick 和内部 RAM 堆。

示例分区固定为 `[16 MiB, 17 MiB)`。默认 `EXAMPLE_LITTLEFS_FORMAT=0`，挂载失败直接返回；挂载成功会创建或覆盖 `hello.txt`，写入、同步、读回比较，然后关闭并卸载。

仅在明确预留该分区且需要首次建盘时设置 `EXAMPLE_LITTLEFS_FORMAT=1`。启用后每次启动都会格式化分区，清除原文件；首次建盘后应关闭。观察 `example_result`（适配/Flash 错误）和 `example_lfs_result`（文件系统错误）。

编译时替换应用的原 `main.c`，不能同时链接多个 `main` 或 `Error_Handler`。`lfs_file_open` 使用官方文件缓存分配，示例要求启用 LittleFS 的 malloc。

板级时钟代码改编自参考 STM32H723 工程。原版权声明：Copyright (c) 2026 STMicroelectronics. All rights reserved. This software is licensed under terms that can be found in the LICENSE file in the root directory of this software component. If no LICENSE file comes with this software, it is provided AS-IS.

上述 ST 代码保留其原始授权条款，不适用组件原创代码的 MIT 许可。
