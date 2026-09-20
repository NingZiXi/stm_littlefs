# 主机软件测试

需要 CMake 3.22+、C/C++ GCC 或 Clang、官方 LittleFS v2.11.2，以及同级 `stm_flash v3.0.0`、`stm_common v1.0.0` 的头文件。

```sh
cmake -S tests -B build/tests -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure
```

默认通过组件 CMake 获取固定版本的官方 LittleFS；离线时添加 `-DSTM_LITTLEFS_SOURCE_DIR=/absolute/path/to/littlefs`。Flash/Common 在其他目录时分别设置 `FLASH_SOURCE_DIR`、`COMMON_SOURCE_DIR`。Windows 指定 MinGW 的 C/C++ 编译器并把其 bin 加入 PATH；测试不使用 MCU 交叉编译器。

真实适配层和官方 LittleFS 在主机运行，Flash API 由 NOR 模型提供。使用实际 Flash 公共头文件和最小 HAL 状态桩；不模拟 OSPI 电气行为。

覆盖分区范围/重叠检查、缓存分配失败清理、错误映射、文件创建/追加/改名/删除/重挂载，以及逐个 program/erase 位置部分写入或部分擦除后丢失 RAM 状态的恢复。分区首尾保护区必须不变。断电模型在文件替换期间的各突变点分别中断，恢复后文件必须为完整旧版本或完整新版本。

所有输出位于 `build/`。这些检查验证软件契约，不替代器件供电、硬件掉电和长期耐久验证。
