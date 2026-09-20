/**
 * @file    stm_littlefs.h
 * @brief   LittleFS 与 stm_flash 的分区块设备适配
 */
#ifndef STM_LITTLEFS_H
#define STM_LITTLEFS_H

#include "stm_flash.h"
#include "lfs.h"

#define STM_LITTLEFS_VERSION "1.0.0"

#if defined(LFS_READONLY) || defined(LFS_THREADSAFE)
#error "stm_littlefs requires read/write LittleFS with external serialization"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct littlefs_context *littlefs_handle_t;

typedef struct {
    flash_handle_t flash;       // 借用已创建的 Flash，须比适配对象活得更久
    uint32_t offset_bytes;     // 分区起点，按擦除粒度对齐
    uint32_t size_bytes;       // 分区长度，按擦除粒度对齐，至少两个块
    uint32_t read_size;        // 0 使用 16 字节
    uint32_t prog_size;        // 0 使用 Flash 页大小
    uint32_t cache_size;       // 0 使用 read_size、prog_size 的较大值
    uint32_t lookahead_size;   // 0 使用 32 字节，须为 8 的倍数
    int32_t block_cycles;      // 0 使用 500；-1 关闭；正数须小于 INT32_MAX
} littlefs_config_t;

/**
 * @brief 创建分区适配对象及缓存；不挂载、不格式化、不擦写。
 * @param config 创建配置，调用期间复制；Flash 仍由调用者持有。
 * @param out_handle 输出位置，调用前须为 NULL；失败保持原值。
 * @return STM_OK 或参数、状态、配置、不支持及内存错误。
 * @note 同一 Flash 句柄的活动分区不得重叠。所有接口须在普通线程串行调用。
 */
stm_err_t littlefs_create(const littlefs_config_t *config, littlefs_handle_t *out_handle);

/**
 * @brief 释放对象及缓存并清空句柄；不释放 Flash、不执行卸载。
 * @param handle 句柄地址，空句柄可重复删除。
 * @return STM_OK 或 STM_ERR_INVALID_ARG。
 * @note 调用前必须关闭全部文件并 lfs_unmount；组件无法自动检测挂载状态。
 */
stm_err_t littlefs_delete(littlefs_handle_t *handle);

/**
 * @brief 获取可直接用于 lfs_format/lfs_mount 的配置，不访问设备。
 * @param handle 有效实例。
 * @param config 输出只读指针，在删除适配对象前有效；失败不修改输出。
 * @return STM_OK 或 STM_ERR_INVALID_ARG。
 * @note 不复制或修改该配置；同一对象只允许一个活动 lfs_t 使用缓存。
 */
stm_err_t littlefs_get_config(littlefs_handle_t handle, const struct lfs_config **config);

/**
 * @brief 查询最近一次块设备回调的底层返回值，不访问设备。
 * @param handle 有效实例。
 * @param error 输出 stm_err_t；后续成功的块回调会更新为 STM_OK。
 * @return STM_OK 或 STM_ERR_INVALID_ARG；文件系统逻辑错误仍以 lfs_* 返回值为准。
 */
stm_err_t littlefs_get_last_error(littlefs_handle_t handle, stm_err_t *error);

#ifdef __cplusplus
}
#endif
#endif
