/**
 * @file    test_littlefs.c
 * @brief   官方 LittleFS 文件操作、分区边界、错误注入和模拟断电
 */
#include "stm_littlefs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define BLOCK 4096U
#define BLOCKS 32U
static unsigned char storage[(BLOCKS + 2U) * BLOCK], snapshot[sizeof(storage)];
struct flash_context { int ready; };
static struct flash_context flash = {1};
static stm_err_t injected;
static int operations, mutations, cut_at, allocation_at, allocations, live;
static jmp_buf power_cut;
static lfs_t fs;
static lfs_file_t file;
static unsigned char file_cache[256], old_data[8192], new_data[8192], readback[8192];
static const struct lfs_file_config file_config = {.buffer = file_cache};
static const struct lfs_config *cfg;
static littlefs_handle_t dev;
extern stm_err_t cpp_header_check(void);

void *test_calloc(size_t count, size_t size)
{
    if (++allocations == allocation_at) { return NULL; }
    void *p = calloc(count, size);
    if (p) { live++; }
    return p;
}
void test_free(void *p) { if (p) { live--; free(p); } }

stm_err_t flash_get_info(flash_handle_t handle, flash_info_t *info)
{
    if (!handle || !info) { return STM_ERR_INVALID_ARG; }
    *info = (flash_info_t){.size_bytes = sizeof(storage), .page_size = 256,
        .erase_size = BLOCK, .capabilities = 15, .read_mode = FLASH_READ_QUAD,
        .ready = (uint8_t)handle->ready};
    return STM_OK;
}
static stm_err_t check_range(uint32_t offset, size_t size)
{
    operations++;
    if (injected) { stm_err_t error = injected; injected = STM_OK; return error; }
    if (!flash.ready) { return STM_ERR_INVALID_STATE; }
    if (offset > sizeof(storage) || size > sizeof(storage) - offset) { return STM_ERR_OUT_OF_RANGE; }
    return STM_OK;
}
stm_err_t flash_read(flash_handle_t handle, uint32_t offset, void *data, size_t size)
{
    (void)handle;
    stm_err_t error = check_range(offset, size);
    if (!error) { memcpy(data, storage + offset, size); }
    return error;
}
stm_err_t flash_write(flash_handle_t handle, uint32_t offset, const void *data, size_t size)
{
    (void)handle;
    stm_err_t error = check_range(offset, size);
    if (error) { return error; }
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; i++) {
        if ((storage[offset + i] & bytes[i]) != bytes[i]) { return FLASH_ERR_NEEDS_ERASE; }
    }
    int cut = ++mutations == cut_at;
    size_t written = cut ? size / 2U : size;
    for (size_t i = 0; i < written; i++) { storage[offset + i] &= bytes[i]; }
    if (cut) { longjmp(power_cut, 1); }
    return STM_OK;
}
stm_err_t flash_erase(flash_handle_t handle, uint32_t offset, size_t size)
{
    (void)handle;
    CHECK(offset % BLOCK == 0 && size % BLOCK == 0);
    stm_err_t error = check_range(offset, size);
    if (error) { return error; }
    int cut = ++mutations == cut_at;
    memset(storage + offset, 255, cut ? size / 2U : size);
    if (cut) { longjmp(power_cut, 1); }
    return STM_OK;
}
static littlefs_config_t config(void)
{
    return (littlefs_config_t){.flash = &flash, .offset_bytes = BLOCK, .size_bytes = BLOCKS * BLOCK};
}
static void create(void)
{
    littlefs_config_t input = config();
    CHECK(littlefs_create(&input, &dev) == STM_OK);
    CHECK(littlefs_get_config(dev, &cfg) == STM_OK);
}
static void guards(void)
{
    for (unsigned i = 0; i < BLOCK; i++) {
        CHECK(storage[i] == 0xA5 && storage[sizeof(storage) - BLOCK + i] == 0x5A);
    }
}
static void write_file(const unsigned char *data, size_t size)
{
    CHECK(lfs_file_opencfg(&fs, &file, "data", LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC, &file_config) == 0);
    CHECK(lfs_file_write(&fs, &file, data, (lfs_size_t)size) == (lfs_ssize_t)size);
    CHECK(lfs_file_close(&fs, &file) == 0);
}
static void read_file(void)
{
    CHECK(lfs_file_opencfg(&fs, &file, "data", LFS_O_RDONLY, &file_config) == 0);
    CHECK(lfs_file_size(&fs, &file) == sizeof(readback));
    CHECK(lfs_file_read(&fs, &file, readback, sizeof(readback)) == sizeof(readback));
    CHECK(lfs_file_close(&fs, &file) == 0);
}
static void lifecycle_and_errors(void)
{
    littlefs_config_t input = config();
    CHECK(cpp_header_check() == STM_OK);
    CHECK(littlefs_create(NULL, &dev) == STM_ERR_INVALID_ARG);
    CHECK(littlefs_create(&input, NULL) == STM_ERR_INVALID_ARG);
    CHECK(littlefs_delete(NULL) == STM_ERR_INVALID_ARG);
    for (int i = 1; i <= 4; i++) {
        allocations = 0; allocation_at = i;
        CHECK(littlefs_create(&input, &dev) == STM_ERR_NO_MEM && !dev && live == 0);
    }
    allocation_at = 0;
    input.offset_bytes++;
    CHECK(littlefs_create(&input, &dev) == STM_ERR_INVALID_CONFIG);
    input = config(); input.size_bytes = UINT32_MAX;
    CHECK(littlefs_create(&input, &dev) == STM_ERR_INVALID_CONFIG);
    input = config(); input.cache_size = 300;
    CHECK(littlefs_create(&input, &dev) == STM_ERR_INVALID_CONFIG);
    input = config(); input.lookahead_size = 7;
    CHECK(littlefs_create(&input, &dev) == STM_ERR_INVALID_CONFIG);
    input = config(); input.block_cycles = -2;
    CHECK(littlefs_create(&input, &dev) == STM_ERR_INVALID_CONFIG);
    input = config(); input.block_cycles = INT32_MAX;
    CHECK(littlefs_create(&input, &dev) == STM_ERR_INVALID_CONFIG);
    int before = operations;
    create(); CHECK(operations == before); // 创建不读写存储。
    input = config(); littlefs_handle_t other = NULL;
    CHECK(littlefs_create(&input, &other) == STM_ERR_INVALID_STATE && !other);
    CHECK(littlefs_create(&input, &dev) == STM_ERR_INVALID_STATE);
    CHECK(cfg->block_count == BLOCKS && cfg->block_size == BLOCK && cfg->prog_size == 256);
    CHECK(cfg->read(cfg, BLOCKS, 0, readback, 16) == LFS_ERR_INVAL);
    CHECK(cfg->read(cfg, UINT32_MAX, 0, readback, 16) == LFS_ERR_INVAL);
    CHECK(cfg->read(cfg, 0, BLOCK - 16, readback, 32) == LFS_ERR_INVAL);
    CHECK(cfg->prog(cfg, 0, 1, new_data, 256) == LFS_ERR_INVAL);
    CHECK(cfg->read(cfg, 0, 0, NULL, 16) == LFS_ERR_INVAL);
    CHECK(cfg->erase(cfg, BLOCKS) == LFS_ERR_INVAL);
    CHECK(operations == before);
    stm_err_t errors[] = {STM_ERR_TIMEOUT, STM_ERR_VERIFY, FLASH_ERR_PROTECTED, FLASH_ERR_NEEDS_ERASE};
    for (unsigned i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
        injected = errors[i];
        CHECK(cfg->prog(cfg, 0, 0, new_data, 256) == LFS_ERR_IO);
        stm_err_t actual;
        CHECK(littlefs_get_last_error(dev, &actual) == STM_OK && actual == errors[i]);
    }
    flash.ready = 0; CHECK(cfg->sync(cfg) == LFS_ERR_IO);
    flash.ready = 1; CHECK(cfg->sync(cfg) == 0);
    CHECK(littlefs_delete(&dev) == STM_OK && !dev && live == 0);
    CHECK(littlefs_delete(&dev) == STM_OK);
    // 相邻分区各持有独立的缓存和地址范围。
    input = config(); input.size_bytes = 16U * BLOCK;
    CHECK(littlefs_create(&input, &dev) == STM_OK);
    input.offset_bytes += input.size_bytes;
    CHECK(littlefs_create(&input, &other) == STM_OK);
    CHECK(littlefs_delete(&other) == STM_OK);
    CHECK(littlefs_delete(&dev) == STM_OK && live == 0);
}
static void filesystem(void)
{
    create();
    int before = mutations;
    CHECK(lfs_mount(&fs, cfg) < 0 && mutations == before); // 无自动格式化。
    CHECK(lfs_format(&fs, cfg) == 0);
    CHECK(lfs_mount(&fs, cfg) == 0);
    write_file(old_data, sizeof(old_data));
    CHECK(lfs_file_opencfg(&fs, &file, "data", LFS_O_WRONLY | LFS_O_APPEND, &file_config) == 0);
    CHECK(lfs_file_write(&fs, &file, new_data, 512) == 512);
    CHECK(lfs_file_close(&fs, &file) == 0);
    CHECK(lfs_rename(&fs, "data", "renamed") == 0);
    struct lfs_info info;
    CHECK(lfs_stat(&fs, "renamed", &info) == 0 && info.size == 8704);
    CHECK(lfs_remove(&fs, "renamed") == 0);
    CHECK(lfs_stat(&fs, "renamed", &info) == LFS_ERR_NOENT);
    write_file(old_data, sizeof(old_data));
    CHECK(lfs_unmount(&fs) == 0);
    CHECK(littlefs_delete(&dev) == STM_OK);
    create(); CHECK(lfs_mount(&fs, cfg) == 0);
    read_file(); CHECK(memcmp(readback, old_data, sizeof(readback)) == 0);
    CHECK(lfs_unmount(&fs) == 0);
    CHECK(littlefs_delete(&dev) == STM_OK && live == 0);
    guards();
}
static void power_loss(void)
{
    memcpy(snapshot, storage, sizeof(storage));
    create(); CHECK(lfs_mount(&fs, cfg) == 0);
    mutations = 0; write_file(new_data, sizeof(new_data));
    int points = mutations;
    CHECK(lfs_unmount(&fs) == 0); CHECK(littlefs_delete(&dev) == STM_OK);
    for (int point = 1; point <= points; point++) {
        memcpy(storage, snapshot, sizeof(storage));
        create(); CHECK(lfs_mount(&fs, cfg) == 0);
        mutations = 0; cut_at = point;
        if (setjmp(power_cut) == 0) {
            write_file(new_data, sizeof(new_data));
            CHECK(0); // 每个写入或擦除位置必须触发半途掉电。
        }
        cut_at = 0;
        // 模拟 CPU 丢失所有文件系统状态；文件缓存为静态，无待回收的文件堆。
        memset(&fs, 0, sizeof(fs)); memset(&file, 0, sizeof(file));
        CHECK(littlefs_delete(&dev) == STM_OK && live == 0);
        create(); CHECK(lfs_mount(&fs, cfg) == 0);
        read_file();
        CHECK(memcmp(readback, old_data, sizeof(readback)) == 0 ||
              memcmp(readback, new_data, sizeof(readback)) == 0);
        CHECK(lfs_unmount(&fs) == 0); CHECK(littlefs_delete(&dev) == STM_OK);
        guards();
    }
    printf("PASS: %d interrupted program/erase positions recover committed old/new contents\n", points);
}
int main(void)
{
    memset(storage, 255, sizeof(storage));
    memset(storage, 0xA5, BLOCK); memset(storage + sizeof(storage) - BLOCK, 0x5A, BLOCK);
    for (unsigned i = 0; i < sizeof(old_data); i++) {
        old_data[i] = (unsigned char)(i * 37U); new_data[i] = (unsigned char)(i * 71U + 11U);
    }
    lifecycle_and_errors(); filesystem(); power_loss();
    CHECK(live == 0);
    puts("PASS: lifecycle, allocation cleanup, partition guards, errors, files, append, rename, delete, remount");
    return 0;
}
