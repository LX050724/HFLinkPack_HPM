/**
 * @file    hf_hpm_romapi_min.h
 * @brief   HPM ROM API 表的最小声明（仅算法用到的字段子集）
 *
 * 官方 bootloader_api_table_t 定义在 soc/<series>/<chip>/hpm_romapi.h，但该头
 * 连锁引入 SDP/XPI RAM/外设头全家桶；此处按官方布局声明用到的前 6 个字段
 * （全 HPM 系一致，xpi_nor_driver_if 固定在表 +0x14），保持算法头闭包最小。
 *
 * XPI_Type 在 hpm_romapi_xpi_def.h 中即 uint32_t 的别名，无寄存器头依赖。
 */
#ifndef HF_HPM_ROMAPI_MIN_H
#define HF_HPM_ROMAPI_MIN_H

#include "hpm_common.h"
#include "hpm_romapi_xpi_def.h"
#include "hpm_romapi_xpi_nor_def.h"
#include "hpm_otp_drv.h" /* per-SoC（otp_driver_interface_t，仅依赖 hpm_common.h） */

#ifdef __cplusplus
extern "C" {
#endif

/** ROM API 表根（boot ROM 末尾；HPM 全系列一致） */
#define HF_ROM_API_TABLE_ROOT ((const hf_rom_api_table_t *)0x2001FF00UL)

/** XPI 存储窗口基址（Init 入参的合法取值） */
#define HF_XPI0_MEM_BASE 0x80000000UL
#define HF_XPI1_MEM_BASE 0x90000000UL

#ifndef HF_XPI0_BASE
#define HF_XPI0_BASE 0xF3000000UL /* 默认 HPM5300/6800/6E00/6P00 档位，由构建注入精确值 */
#endif
#ifndef HF_XPI1_BASE
#define HF_XPI1_BASE 0UL /* 0 = 本 SoC 无 XPI1 */
#endif

/**
 * OTP driver interface（全 HPM 系成员序一致，已逐一核对各 SoC 的 hpm_romapi.h；
 * 官方定义位于会拖入 SDP 全家桶的 soc 版 hpm_romapi.h，故在此抄录）。
 * 算法仅使用 read_from_shadow（+0x08）。
 */
typedef struct
{
    void (*init)(void);                                       /* +0x00 */
    void (*deinit)(void);                                     /* +0x04 */
    uint32_t (*read_from_shadow)(uint32_t addr);              /* +0x08 */
    uint32_t (*read_from_ip)(uint32_t addr);                  /* +0x0C */
    hpm_stat_t (*program)(uint32_t addr, const uint32_t *src, uint32_t num_of_words);
    hpm_stat_t (*reload)(otp_region_t region);
    hpm_stat_t (*lock)(uint32_t addr, otp_lock_option_t lock_option);
    hpm_stat_t (*lock_shadow)(uint32_t addr, otp_lock_option_t lock_option);
    hpm_stat_t (*set_configurable_region)(uint32_t start, uint32_t num_of_words);
    hpm_stat_t (*write_shadow_register)(uint32_t addr, uint32_t data);
} hf_otp_driver_interface_t;

/** bootloader_api_table_t 前缀字段子集（偏移与官方 hpm_romapi.h 严格一致） */
typedef struct
{
    const uint32_t version;                                 /* +0x00 */
    const char *copyright;                                  /* +0x04 */
    hpm_stat_t (*run_bootloader)(void *arg);                /* +0x08 */
    const hf_otp_driver_interface_t *otp_driver_if;         /* +0x0C */
    const void *xpi_driver_if;                              /* +0x10 */
    const xpi_nor_driver_interface_t *xpi_nor_driver_if;    /* +0x14 */
} hf_rom_api_table_t;

#ifdef __cplusplus
}
#endif

#endif /* HF_HPM_ROMAPI_MIN_H */
