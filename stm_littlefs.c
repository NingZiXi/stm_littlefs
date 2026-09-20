/**
 * @file    stm_littlefs.c
 * @brief   分区边界、LittleFS 回调和适配对象生命周期
 */
#include "stm_littlefs.h"
#include <stdlib.h>

struct littlefs_context {
    struct lfs_config lfs;
    flash_handle_t flash;
    uint32_t offset_bytes;
    uint32_t size_bytes;
    stm_err_t last_error;
    struct littlefs_context *next;
};
static littlefs_handle_t devices;

// Flash 故障会停用句柄，不能让 LittleFS 当作局部坏块继续写入。
static int result(littlefs_handle_t dev, stm_err_t error)
{
    dev->last_error = error;
    switch (error) {
    case STM_OK: return LFS_ERR_OK;
    case STM_ERR_INVALID_ARG:
    case STM_ERR_INVALID_CONFIG:
    case STM_ERR_OUT_OF_RANGE: return LFS_ERR_INVAL;
    case STM_ERR_NO_MEM: return LFS_ERR_NOMEM;
    default: return LFS_ERR_IO;
    }
}

// 先检查块内范围和对齐，使用 64 位中间值避免地址溢出。
static int address(littlefs_handle_t dev, lfs_block_t block, lfs_off_t off,
                   lfs_size_t size, uint32_t align, uint32_t *physical)
{
    if (block >= dev->lfs.block_count || off > dev->lfs.block_size ||
        size > dev->lfs.block_size - off || off % align || size % align) {
        return result(dev, STM_ERR_OUT_OF_RANGE);
    }
    uint64_t relative = (uint64_t)block * dev->lfs.block_size + off;
    if (relative + size > dev->size_bytes ||
        (uint64_t)dev->offset_bytes + relative > UINT32_MAX) {
        return result(dev, STM_ERR_OUT_OF_RANGE);
    }
    *physical = dev->offset_bytes + (uint32_t)relative;
    return 0;
}

static int read_block(const struct lfs_config *c, lfs_block_t block,
                      lfs_off_t off, void *buffer, lfs_size_t size)
{
    littlefs_handle_t dev = c->context;
    uint32_t physical;
    int err = address(dev, block, off, size, c->read_size, &physical);
    if (err) { return err; }
    if (!buffer && size) { return result(dev, STM_ERR_INVALID_ARG); }
    return result(dev, flash_read(dev->flash, physical, buffer, size));
}

static int program_block(const struct lfs_config *c, lfs_block_t block,
                         lfs_off_t off, const void *buffer, lfs_size_t size)
{
    littlefs_handle_t dev = c->context;
    uint32_t physical;
    int err = address(dev, block, off, size, c->prog_size, &physical);
    if (err) { return err; }
    if (!buffer && size) { return result(dev, STM_ERR_INVALID_ARG); }
    return result(dev, flash_write(dev->flash, physical, buffer, size));
}

static int erase_block(const struct lfs_config *c, lfs_block_t block)
{
    littlefs_handle_t dev = c->context;
    uint32_t physical;
    int err = address(dev, block, 0U, c->block_size, c->block_size, &physical);
    return err ? err : result(dev, flash_erase(dev->flash, physical, c->block_size));
}

static int sync_device(const struct lfs_config *c)
{
    littlefs_handle_t dev = c->context;
    flash_info_t info;
    stm_err_t err = flash_get_info(dev->flash, &info);
    if (err == STM_OK && !info.ready) { err = STM_ERR_INVALID_STATE; }
    // stm_flash 的成功写入已经等待 BUSY 清零并读回校验，没有待刷写队列。
    return result(dev, err);
}

stm_err_t littlefs_create(const littlefs_config_t *config, littlefs_handle_t *out_handle)
{
    if (!config || !out_handle || !config->flash) { return STM_ERR_INVALID_ARG; }
    if (*out_handle) { return STM_ERR_INVALID_STATE; }
    flash_info_t info;
    stm_err_t err = flash_get_info(config->flash, &info);
    if (err != STM_OK) { return err; }
    if (!info.ready) { return STM_ERR_INVALID_STATE; }
    uint32_t read_cap = info.read_mode == FLASH_READ_QUAD ? FLASH_CAP_READ_QUAD : FLASH_CAP_READ_SINGLE;
    uint32_t required = read_cap | FLASH_CAP_PROGRAM | FLASH_CAP_ERASE;
    if ((info.capabilities & required) != required) { return STM_ERR_NOT_SUPPORTED; }
    uint32_t block = info.erase_size;
    if (block < 128U || block > INT32_MAX || !info.page_size ||
        config->offset_bytes > info.size_bytes ||
        config->size_bytes > info.size_bytes - config->offset_bytes ||
        config->offset_bytes % block || config->size_bytes % block ||
        config->size_bytes / block < 2U) { return STM_ERR_INVALID_CONFIG; }

    uint32_t read = config->read_size ? config->read_size : 16U;
    uint32_t prog = config->prog_size ? config->prog_size : info.page_size;
    uint32_t cache = config->cache_size ? config->cache_size : (read > prog ? read : prog);
    uint32_t lookahead = config->lookahead_size ? config->lookahead_size : 32U;
    int32_t cycles = config->block_cycles ? config->block_cycles : 500;
    if (read > block || prog > block || cache > block || block % cache ||
        cache % read || cache % prog || lookahead % 8U ||
        lookahead > UINT32_MAX / 8U || cycles < -1 || cycles == INT32_MAX) { return STM_ERR_INVALID_CONFIG; }
    uint64_t end = (uint64_t)config->offset_bytes + config->size_bytes;
    for (littlefs_handle_t it = devices; it; it = it->next) {
        if (it->flash == config->flash && config->offset_bytes < (uint64_t)it->offset_bytes + it->size_bytes &&
            it->offset_bytes < end) { return STM_ERR_INVALID_STATE; }
    }
    littlefs_handle_t dev = calloc(1U, sizeof(*dev));
    if (!dev) { return STM_ERR_NO_MEM; }
    dev->lfs = (struct lfs_config){
        .context = dev, .read = read_block, .prog = program_block,
        .erase = erase_block, .sync = sync_device,
        .read_size = read, .prog_size = prog, .block_size = block,
        .block_count = config->size_bytes / block, .cache_size = cache,
        .lookahead_size = lookahead, .block_cycles = cycles,
    };
    dev->lfs.read_buffer = calloc(1U, cache);
    dev->lfs.prog_buffer = calloc(1U, cache);
    dev->lfs.lookahead_buffer = calloc(1U, lookahead);
    if (!dev->lfs.read_buffer || !dev->lfs.prog_buffer || !dev->lfs.lookahead_buffer) {
        free(dev->lfs.read_buffer);
        free(dev->lfs.prog_buffer);
        free(dev->lfs.lookahead_buffer);
        free(dev);
        return STM_ERR_NO_MEM;
    }
    dev->flash = config->flash;
    dev->offset_bytes = config->offset_bytes;
    dev->size_bytes = config->size_bytes;
    dev->next = devices;
    devices = dev;
    *out_handle = dev;
    return STM_OK;
}

stm_err_t littlefs_delete(littlefs_handle_t *handle)
{
    if (!handle) { return STM_ERR_INVALID_ARG; }
    if (!*handle) { return STM_OK; }
    littlefs_handle_t dev = *handle;
    littlefs_handle_t *link = &devices;
    while (*link && *link != dev) { link = &(*link)->next; }
    if (!*link) { return STM_ERR_INVALID_ARG; }
    *link = dev->next;
    free(dev->lfs.read_buffer);
    free(dev->lfs.prog_buffer);
    free(dev->lfs.lookahead_buffer);
    free(dev);
    *handle = NULL;
    return STM_OK;
}

stm_err_t littlefs_get_config(littlefs_handle_t handle, const struct lfs_config **config)
{
    if (!handle || !config) { return STM_ERR_INVALID_ARG; }
    *config = &handle->lfs;
    return STM_OK;
}

stm_err_t littlefs_get_last_error(littlefs_handle_t handle, stm_err_t *error)
{
    if (!handle || !error) { return STM_ERR_INVALID_ARG; }
    *error = handle->last_error;
    return STM_OK;
}
