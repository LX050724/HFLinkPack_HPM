/**
 * @file    hf_flash_algo.c
 * @brief   HPM XPI NOR 烧录算法（系列共用一个 ELF，FLM 同构）
 *
 * 模型：RAM stub + HPM ROM API。算法经 ROM API 表根（0x2001FF00）的
 * xpi_nor_driver_if 完成擦写；XPI 配置全自动探测（SEGGER Open Flashloader
 * 同语义）：OTP word7 bit24 判 SiP 片内 flash -> auto_config(SFDP, 104MHz 档)
 * -> 依次尝试三组 pin group/connection，SiP 失败即弃、非 SiP 换组重试。
 *
 * 无启动代码：全局状态位于 .bss（NOLOAD），Init 首次进入时自清零；
 * gp 相对寻址禁用（-msmall-data-limit=0），不依赖 gp/tp 初始化。
 */

#include "hf_flash_algo.h"
#include "hf_hpm_romapi_min.h"

/* ------------------------------------------------------------------ */
/* freestanding 运行时：编译器可能对结构体清零/拷贝生成隐式调用，自带最小实现 */
/* ------------------------------------------------------------------ */

void *memset(void *dst, int value, size_t count)
{
    unsigned char *p = (unsigned char *)dst;
    while (count-- != 0U)
        *p++ = (unsigned char)value;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t count)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (count-- != 0U)
        *d++ = *s++;
    return dst;
}

/* ------------------------------------------------------------------ */
/* 烧录会话状态（.bss，Init 自清零）                                     */
/* ------------------------------------------------------------------ */

#define HF_ALGO_SESSION_MAGIC 0xAA5A55A5UL

static xpi_nor_config_t hf_nor_cfg;       /* ROM auto_config 填充的完整 XPI NOR 配置 */
static xpi_nor_config_option_t hf_cfg_opt; /* 探测用 option（header/opt0/opt1） */
static uint32_t hf_session_state;         /* HF_ALGO_SESSION_MAGIC = 已初始化 */
static XPI_Type hf_xpi_base;              /* XPI 控制器基址 */
static xpi_xfer_channel_t hf_xfer_channel; /* auto_config 探测出的传输通道 */
static uint32_t hf_flash_mem_base;        /* 本次会话的存储窗口基址 */

/* ------------------------------------------------------------------ */
/* XPI 全自动探测（SEGGER Open Flashloader flash_auto_probe 同语义）      */
/* ------------------------------------------------------------------ */

/* option1 候选：bit12 pin_group_sel + bit8 connection_sel（0=PORTA,1=PORTB） */
#define HF_OPT1_PIN_GROUP1_PORTA 0x1000UL
#define HF_OPT1_PIN_GROUP0_PORTA 0x0000UL
#define HF_OPT1_PIN_GROUP0_PORTB 0x0100UL
#define HF_OPT1_PORT_B_MASK 0x0100UL
/* OTP word7 bit24：片内 SiP flash 标志（配置固定，探测失败不换口重试） */
#define HF_OTP_SIP_FLASH_MASK (1UL << 24)

static void hf_refresh_device_size(void)
{
    /* 通道 B 场景 auto_config 后设备大小寄存器可能未刷新，按 SEGGER 口径
     * 裸写 XPI 基址 +0x60 起的两个设备大小寄存器清零（触发重建） */
    if ((hf_cfg_opt.header.U & 0xFU) > 1U &&
        (hf_cfg_opt.option1.U & 0xF00U) == 0x100U) /* connection_sel == PORTB */
    {
        volatile uint32_t *dev_size = (volatile uint32_t *)(uint32_t)hf_xpi_base + 0x60U / 4U;
        dev_size[0] = 0U;
        dev_size[1] = 0U;
    }
}

static int hf_flash_auto_probe(void)
{
    static const uint32_t opt1_candidates[3] = {
        HF_OPT1_PIN_GROUP1_PORTA, /* 引脚组 1 + 通道 A（EVK 常见） */
        HF_OPT1_PIN_GROUP0_PORTA, /* 引脚组 0 + 通道 A */
        HF_OPT1_PIN_GROUP0_PORTB, /* 引脚组 0 + 通道 B */
    };
    const uint32_t otp = HF_ROM_API_TABLE_ROOT->otp_driver_if->read_from_shadow(7);
    const xpi_nor_driver_interface_t *drv = HF_ROM_API_TABLE_ROOT->xpi_nor_driver_if;

    /* header：tag 0xfcf90 + 2 个 option 字；option0：freq_opt=5（104MHz 档）、
     * probe_type=0（SFDP + SDR 自动探测），其余字段 0 = ROM 自动决定 */
    hf_cfg_opt.header.U = 0xFCF90002UL;
    hf_cfg_opt.option0.U = 0x5UL;
    hf_cfg_opt.option2.U = 0U;

    for (size_t i = 0; i < sizeof(opt1_candidates) / sizeof(opt1_candidates[0]); ++i)
    {
        hf_cfg_opt.option1.U = opt1_candidates[i];
        hf_xfer_channel =
            (opt1_candidates[i] & HF_OPT1_PORT_B_MASK) != 0U ? xpi_xfer_channel_b1 : xpi_xfer_channel_a1;
        if (drv->auto_config((XPI_Type *)(uint32_t)hf_xpi_base, &hf_nor_cfg, &hf_cfg_opt) == 0U)
        {
            hf_refresh_device_size();
            return 0;
        }
        if ((otp & HF_OTP_SIP_FLASH_MASK) != 0U)
            return 1; /* SiP 片内 flash 配置固定，换口重试无意义 */
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* 设备描述块（FLM 同构：SDK 按 FindSymbol("FlashDevice") 定位并解析几何） */
/* ------------------------------------------------------------------ */

const hf_flash_device_t FlashDevice = {
    .version = 0x0100u,
    .name = "HPM XPI NOR (auto probe)",
    .device_type = 0u,
    .start = (uint32_t)HF_XPI0_MEM_BASE, /* 窗口基址由 romapi 头按组注入 */
    .size = HFALGO_FLASH_SIZE,
    .page_size = HFALGO_PAGE_SIZE,
    .reserved = 0u,
    .erased_value = 0xFFu,
    .padding = {0u, 0u, 0u},
    .program_timeout_ms = 1000u,
    .erase_timeout_ms = 5000u,
    .sectors = {{HFALGO_SECTOR_SIZE, 0u}, {0xFFFFFFFFu, 0xFFFFFFFFu}},
};

/* ------------------------------------------------------------------ */
/* 入口五件套 + Read（FLM 同名符号，返回 0 成功）                         */
/* ------------------------------------------------------------------ */

int Init(uint32_t flash_base, uint32_t clock_hz, uint32_t function)
{
    (void)clock_hz; /* XPI 频率由 auto_config 档位决定，不取主机时钟 */
    (void)function;
    if (hf_session_state == HF_ALGO_SESSION_MAGIC)
        return 0; /* 幂等：同一烧录会话内多次 Init（Erase/Program 阶段各调一次） */

    if (flash_base == HF_XPI0_MEM_BASE)
        hf_xpi_base = (XPI_Type)HF_XPI0_BASE;
#if HF_XPI1_BASE != 0
    else if (flash_base == HF_XPI1_MEM_BASE)
        hf_xpi_base = (XPI_Type)HF_XPI1_BASE;
#endif
    else
        return 1;

    /* 无启动代码：状态区自清零 */
    memset(&hf_nor_cfg, 0, sizeof(hf_nor_cfg));
    memset(&hf_cfg_opt, 0, sizeof(hf_cfg_opt));
    hf_flash_mem_base = flash_base;
    hf_xfer_channel = xpi_xfer_channel_a1;
    hf_session_state = 0;

    if (hf_flash_auto_probe() != 0)
        return 1;
    /* 擦写等非读命令走安全低频（ROM 按此字段选择命令时序） */
    hf_nor_cfg.device_info.clk_freq_for_non_read_cmd = 0U;

    hf_session_state = HF_ALGO_SESSION_MAGIC;
    return 0;
}

int UnInit(uint32_t function)
{
    (void)function;
    hf_session_state = 0;
    return 0;
}

int EraseChip(void)
{
    if (hf_session_state != HF_ALGO_SESSION_MAGIC)
        return 1;
    const xpi_nor_driver_interface_t *drv = HF_ROM_API_TABLE_ROOT->xpi_nor_driver_if;
    /* 与 SDK inline 包装 rom_xpi_nor_erase_chip 同序：先 wait_busy 再整片擦除 */
    if (drv->wait_busy((XPI_Type *)(uint32_t)hf_xpi_base, hf_xfer_channel, &hf_nor_cfg, 0U) != 0U)
        return 1;
    return drv->erase_chip((XPI_Type *)(uint32_t)hf_xpi_base, hf_xfer_channel, &hf_nor_cfg) == 0U ? 0 : 1;
}

int EraseSector(uint32_t address)
{
    if (hf_session_state != HF_ALGO_SESSION_MAGIC)
        return 1;
    const xpi_nor_driver_interface_t *drv = HF_ROM_API_TABLE_ROOT->xpi_nor_driver_if;
    return drv->erase_sector((XPI_Type *)(uint32_t)hf_xpi_base, hf_xfer_channel, &hf_nor_cfg,
                             address - hf_flash_mem_base) == 0U
               ? 0
               : 1;
}

int ProgramPage(uint32_t address, uint32_t size, const uint8_t *buffer)
{
    if (hf_session_state != HF_ALGO_SESSION_MAGIC)
        return 1;
    const xpi_nor_driver_interface_t *drv = HF_ROM_API_TABLE_ROOT->xpi_nor_driver_if;
    const uint32_t offset = address - hf_flash_mem_base;
    /* 与 SDK inline 包装 rom_api_xpi_nor_page_program 同序：先开写再编程 */
    if (drv->enable_write((XPI_Type *)(uint32_t)hf_xpi_base, hf_xfer_channel, &hf_nor_cfg, offset) != 0U)
        return 1;
    return drv->program((XPI_Type *)(uint32_t)hf_xpi_base, hf_xfer_channel, &hf_nor_cfg,
                        (const uint32_t *)(const void *)buffer, offset, size) == 0U
               ? 0
               : 1;
}

int Read(uint32_t address, uint32_t size, uint8_t *buffer)
{
    if (hf_session_state != HF_ALGO_SESSION_MAGIC)
        return 1;
    const xpi_nor_driver_interface_t *drv = HF_ROM_API_TABLE_ROOT->xpi_nor_driver_if;
    return drv->read((XPI_Type *)(uint32_t)hf_xpi_base, hf_xfer_channel, &hf_nor_cfg, (uint32_t *)(void *)buffer,
                     address - hf_flash_mem_base, size) == 0U
               ? 0
               : 1;
}
