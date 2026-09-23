/**
 * @file    main.c
 * @brief   LittleFS 分区挂载和文件读写的最小板级示例
 */
#include "stm_littlefs.h"
#include "flash_ospi.h"
#include <string.h>
#include "main.h"
#include "gpio.h"
#include "octospi.h"

#ifndef EXAMPLE_LITTLEFS_FORMAT
#define EXAMPLE_LITTLEFS_FORMAT 0
#endif

volatile stm_err_t example_result = STM_OK;
volatile int example_lfs_result = 0;
static void board_init(void);
static flash_ospi_context_t flash_host;

// 示例占用 [16 MiB, 17 MiB)，必须由应用明确预留；禁止与固件或原始数据重叠。
int main(void)
{
    board_init();
    flash_handle_t device = NULL;
    littlefs_handle_t adapter = NULL;
    const struct lfs_config *lfs_config = NULL;
    lfs_t fs = {0};
    lfs_file_t file = {0};
    const flash_config_t config = {
        .device = &flash_device_w25q256jv_iq,
        .host = flash_ospi_bind(&flash_host, &hospi1,
                                __HAL_RCC_GET_OSPI_SOURCE() == RCC_OSPICLKSOURCE_HCLK
                                    ? HAL_RCC_GetHCLKFreq() : 0U),
        .read_mode = FLASH_READ_QUAD,
    };
    example_result = flash_create(&config, &device);
    if (example_result != STM_OK) { goto finish; }
    const littlefs_config_t partition = {
        .flash = device, .offset_bytes = 16U * 1024U * 1024U,
        .size_bytes = 1024U * 1024U,
    };
    example_result = littlefs_create(&partition, &adapter);
    if (example_result != STM_OK) { goto finish; }
    example_result = littlefs_get_config(adapter, &lfs_config);
    if (example_result != STM_OK) { goto finish; }
#if EXAMPLE_LITTLEFS_FORMAT
    // 仅首次建盘时明确启用；每次运行都会重建此分区，清除原有文件。
    example_lfs_result = lfs_format(&fs, lfs_config);
    if (example_lfs_result != 0) { goto finish; }
#endif
    example_lfs_result = lfs_mount(&fs, lfs_config);
    if (example_lfs_result != 0) { goto finish; } // 挂载失败不自动格式化。
    example_lfs_result = lfs_file_open(&fs, &file, "hello.txt", LFS_O_RDWR | LFS_O_CREAT | LFS_O_TRUNC);
    if (example_lfs_result == 0) {
        const char message[] = "Hello LittleFS\n";
        char actual[sizeof(message)] = {0};
        lfs_ssize_t count = lfs_file_write(&fs, &file, message, sizeof(message));
        if (count != (lfs_ssize_t)sizeof(message)) {
            example_lfs_result = count < 0 ? (int)count : LFS_ERR_IO;
        }
        if (example_lfs_result == 0) { example_lfs_result = lfs_file_sync(&fs, &file); }
        if (example_lfs_result == 0) {
            lfs_soff_t offset = lfs_file_seek(&fs, &file, 0, LFS_SEEK_SET);
            if (offset < 0) { example_lfs_result = (int)offset; }
        }
        if (example_lfs_result == 0) {
            count = lfs_file_read(&fs, &file, actual, sizeof(actual));
            if (count != (lfs_ssize_t)sizeof(actual) || memcmp(actual, message, sizeof(actual))) {
                example_lfs_result = count < 0 ? (int)count : LFS_ERR_IO;
            }
        }
        int close_error = lfs_file_close(&fs, &file);
        if (example_lfs_result == 0) { example_lfs_result = close_error; }
    }
    int unmount_error = lfs_unmount(&fs);
    if (example_lfs_result == 0) { example_lfs_result = unmount_error; }
finish:
    if (adapter) {
        // 两套错误独立观察，不将 LittleFS 负数直接转换成 stm_err_t。
        if (example_result == STM_OK) { littlefs_get_last_error(adapter, (stm_err_t *)&example_result); }
        stm_err_t cleanup = littlefs_delete(&adapter);
        if (example_result == STM_OK) { example_result = cleanup; }
    }
    stm_err_t cleanup = flash_delete(&device);
    if (example_result == STM_OK) { example_result = cleanup; }
    for (;;) { __WFI(); }
}

// 初始化本板 HAL、时钟、GPIO 和 OCTOSPI
static void board_init(void)
{
    MPU_Region_InitTypeDef region = {0};

    // Region 0 禁止访问未配置的外部地址区。
    HAL_MPU_Disable();
    region.Enable = MPU_REGION_ENABLE;
    region.Number = MPU_REGION_NUMBER0;
    region.BaseAddress = 0x00000000UL;
    region.Size = MPU_REGION_SIZE_4GB;
    region.SubRegionDisable = 0x87U;
    region.TypeExtField = MPU_TEX_LEVEL0;
    region.AccessPermission = MPU_REGION_NO_ACCESS;
    region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    region.IsShareable = MPU_ACCESS_SHAREABLE;
    region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    region.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&region);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

    // 初始化期间保持中断开启，HAL tick 须正常运行。
    if (HAL_Init() != HAL_OK) { Error_Handler(); }

    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    if (HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY) != HAL_OK) { Error_Handler(); }

    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

    while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

    // HSE=25 MHz，PLL 输出 550 MHz。
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 2;
    RCC_OscInitStruct.PLL.PLLN = 44;
    RCC_OscInitStruct.PLL.PLLP = 1;
    RCC_OscInitStruct.PLL.PLLQ = 3;
    RCC_OscInitStruct.PLL.PLLR = 2;
    RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
    RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    RCC_OscInitStruct.PLL.PLLFRACN = 0;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    // CPU=550 MHz，HCLK=275 MHz，APB=137.5 MHz。
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                                |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                                |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
    RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
    {
      Error_Handler();
    }

    MX_GPIO_Init();
    MX_OCTOSPI1_Init();
    if (HAL_OSPI_DeInit(&hospi1) != HAL_OK) { Error_Handler(); }
    hospi1.Init.DeviceSize = 25U;
    hospi1.Init.ClockPrescaler = 8U;
    hospi1.Init.ChipSelectHighTime = 4U;
    if (HAL_OSPI_Init(&hospi1) != HAL_OK) { Error_Handler(); }
    OSPIM_CfgTypeDef manager = {0};
    manager.ClkPort = 1U;
    manager.NCSPort = 1U;
    manager.IOLowPort = HAL_OSPIM_IOPORT_1_LOW;
    if (HAL_OSPIM_Config(&hospi1, &manager, 100U) != HAL_OK) { Error_Handler(); }
    HAL_Delay(10U);
}

// 板级初始化失败时停机
void Error_Handler(void)
{
    __disable_irq();
    for (;;) { }
}
