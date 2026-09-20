# stm_littlefs

LittleFS 与 `stm_flash` 的分区适配组件。提供不透明句柄、块地址转换、缓存和错误码转换；文件与目录操作使用官方 `lfs_*` API。无日志、RTT 或 RTOS 依赖。

版本 **v1.0.1**，默认依赖 `stm_flash v3.0.0`、间接依赖 `stm_common v1.0.0`，以及官方 [LittleFS v2.11.2](https://github.com/littlefs-project/littlefs/tree/v2.11.2)。硬件支持范围随 `stm_flash`；当前为 STM32H7 OSPI + W25Q256JV-IQ。适配层不直接依赖型号或 HAL 外设结构。

## CMake 接入

先由 CubeMX 创建 `stm32cubemx` HAL 配置目标，然后在工程根目录添加：

```cmake
include(FetchContent)
FetchContent_Declare(stm_littlefs
    GIT_REPOSITORY https://gitee.com/nzxhg/stm_littlefs.git
    GIT_TAG v1.0.1
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Lib/stm_littlefs
)
FetchContent_MakeAvailable(stm_littlefs)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE stm_littlefs)
```

也可克隆本组件后 `add_subdirectory(Lib/stm_littlefs)`。链接 `stm_littlefs` 会公开传递 Flash、公共错误码、LittleFS 和 CubeMX HAL 配置，不必重复链接依赖。

依赖解析：

- Flash：已有 `stm_flash` target → 同级 `stm_flash/` → FetchContent `v3.0.0`。
- LittleFS：已有 `littlefs` target → `STM_LITTLEFS_SOURCE_DIR` 本地源码 → FetchContent 官方 v2.11.2 的固定提交 `adad0fbbcf5382c20978d07f94f9c13be9041c1b`。
- Flash 的 `stm_common` 依赖由 Flash 自己解析。

组件自动拉取官方源码，为 `lfs.c`、`lfs_util.c` 创建静态库 `littlefs` 并公开头文件路径，主工程无需单独声明或拉取 `littlefs`。默认下载到本组件同级的 `littlefs/`：按上面的配置即为 `Lib/littlefs/`，编译产物和 FetchContent 管理文件仍在构建目录中。默认关闭 LittleFS 的诊断打印。自动下载目录应由主工程忽略，不重复提交官方源码。

以下选项在添加组件前设置；修改缓存中的旧值须使用 CMake GUI、`-D` 或新构建目录：

```cmake
set(STM_LITTLEFS_FLASH_GIT_REPOSITORY "https://gitee.com/nzxhg/stm_flash.git" CACHE STRING "")
set(STM_COMMON_GIT_REPOSITORY "https://gitee.com/nzxhg/stm_common.git" CACHE STRING "")
# 可选：更改自动下载位置；仍会获取和检查固定版本。
set(STM_LITTLEFS_FETCH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Lib/littlefs" CACHE PATH "")
# 离线 LittleFS，目录内应有 lfs.c/h、lfs_util.c/h。
set(STM_LITTLEFS_SOURCE_DIR "/absolute/path/to/littlefs" CACHE PATH "")
set(STM_LITTLEFS_FETCH OFF CACHE BOOL "")
# 此选项关闭 Flash 自动下载，需要已有 target 或同级源码。
set(STM_LITTLEFS_FETCH_FLASH OFF CACHE BOOL "")
```

`STM_LITTLEFS_GIT_REPOSITORY` 可切换官方 LittleFS 的可信镜像，镜像必须包含固定提交。使用 Gitee 获取本组件不会自动把官方 LittleFS 地址改到 Gitee。已有 target/手工源码由应用负责版本兼容性。

## 使用

先完成板级 HAL、时钟、GPIO 和 OSPI 初始化，再创建 Flash：

```c
#include "stm_littlefs.h"

flash_handle_t flash = NULL;
littlefs_handle_t adapter = NULL;
const struct lfs_config *cfg = NULL;
flash_config_t flash_cfg = {
    .bus = {.type = FLASH_BUS_OSPI, .handle.ospi = &hospi1},
    .chip = FLASH_CHIP_AUTO,
    .read_mode = FLASH_READ_QUAD,
};
stm_err_t err = flash_create(&flash_cfg, &flash);
if (err == STM_OK) {
    littlefs_config_t partition = {
        .flash = flash,
        .offset_bytes = 16U * 1024U * 1024U,
        .size_bytes = 1024U * 1024U,
    };
    err = littlefs_create(&partition, &adapter);
}
if (err == STM_OK) { err = littlefs_get_config(adapter, &cfg); }
// err 成功后用 lfs_mount(&fs, cfg) 挂载，再使用官方文件 API。
// 使用结束：关闭全部文件 → lfs_unmount → littlefs_delete → flash_delete。
```

上例分区为 `[16 MiB, 17 MiB)`，必须事先预留。首次建盘由应用明确调用 `lfs_format`；组件不会因挂载失败自动格式化。不要在已挂载的文件系统上格式化。

完整的 [example/main.c](example/main.c) 包含 `board_init()`、挂载、写文件、读回、关闭、卸载和清理。仅供参考，不自动纳入组件目标；其擦写范围和板级依赖见 [example/README.md](example/README.md)。

## 参数与契约

| 参数 | 默认值 / 规则 |
|---|---|
| `flash` | 借用的 Flash 句柄，删除适配对象之前必须保持有效 |
| `offset_bytes` / `size_bytes` | 必填分区范围，擦除粒度对齐，至少两个块 |
| `read_size` | 0 → 16 字节 |
| `prog_size` | 0 → `flash_get_info().page_size` |
| `cache_size` | 0 → read/prog 较大值；须同时为两者整数倍且整除块大小 |
| `lookahead_size` | 0 → 32 字节，须为 8 的倍数且不大于 UINT32_MAX/8 |
| `block_cycles` | 0 → 500；-1 关闭块级磨损均衡；正数小于 INT32_MAX |

`block_size` 取 Flash 的 `erase_size`，`block_count=size_bytes/block_size`。`prog_size` 是文件系统对齐约束，不意味着物理 Flash 只能整页写入。器件换型或布局变更时核对已有卷的格式兼容性，不能直接用不同几何参数挂载原卷。

`littlefs_create/delete/get_config/get_last_error` 返回 `stm_err_t`；官方文件操作返回 LittleFS 自身的负错误码。`get_config` 输出的配置和缓存归适配对象所有，禁止修改或复制后共用；一个适配对象对应一个活动 `lfs_t`。创建时分配对象、读缓存、编程缓存和 lookahead 缓存，删除时释放，堆须位于内部 RAM。官方 `lfs_file_open` 可能另外分配文件缓存。

使用 `LFS_NO_MALLOC` 时文件必须用 `lfs_file_opencfg` 提供缓存；该宏不会禁止适配层自身的 `calloc/free`。本组件要求普通线程与外部串行访问，第一版不支持 `LFS_READONLY` / `LFS_THREADSAFE`。多任务须在每次完整 `lfs_*` 调用外围加锁，不能仅锁住单个 Flash 回调；多个分区共享 Flash 时还须串行化设备访问。

同一 Flash 句柄的重叠适配分区会被拒绝；这不能阻止其他代码通过裸 Flash API 擦写该区域。挂载期间必须由文件系统独占分区。所有对象创建/删除也须串行执行。

## 错误与同步

| 底层返回 | LittleFS 回调返回 |
|---|---|
| `STM_OK` | `LFS_ERR_OK` |
| 参数、配置、范围错误 | `LFS_ERR_INVAL` |
| 内存不足 | `LFS_ERR_NOMEM` |
| 通信、超时、校验、保护、需要擦除、失效句柄等 | `LFS_ERR_IO` |

`littlefs_get_last_error` 查询最近一次块设备回调的底层返回值；后续成功回调会覆盖它，文件不存在等文件系统逻辑错误不会记录为底层错误。

Flash v3 写入/擦除成功前已经等待芯片就绪并读回校验，`sync` 无额外数据需要刷写，只查询实例是否可用。`lfs_file_sync` / `lfs_file_close` 仍必不可少，因为 LittleFS 自身有尚未提交的缓存和元数据。

Flash 通信或校验故障会停用句柄，适配层返回 I/O 错误，不伪装成可跳过的局部坏块。恢复时由应用停止文件系统访问、确认底层硬件状态，再重建 Flash 和文件系统对象；组件不自动清除故障、重试擦写或格式化。

## 测试与许可

[tests/README.md](tests/README.md) 提供官方 LittleFS + NOR 模型的主机测试，覆盖文件操作、分区边界、分配失败和写入中断恢复。测试源码可用于 CI，实板记录、日志、固件和数据备份仅保留本地。

组件原创代码使用 [MIT License](LICENSE)。LittleFS 为独立下载的 BSD-3-Clause 依赖，须保留其上游许可证。示例板级时钟代码的 ST 授权见 [example/README.md](example/README.md)；HAL/CMSIS 由使用者工程提供。
