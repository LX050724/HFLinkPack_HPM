/**
 * @file    hf_flash_algo.h
 * @brief   HPM XPI NOR 烧录算法——FLM 同构契约（ELF 容器）
 *
 * 算法文件为 RV32 ELF（与 ARM FLM 同一容器、同一分发路径）：SDK 侧
 * HFLink_Flm_Parse 解析 ELF，按 e_machine 区分调用约定（EM_ARM=40 走
 * Cortex-M，EM_RISCV=243 走 RV32 ebreak 调用约定），几何与入口全部从
 * ELF 内自描述，pack 条目不做任何算法类型声明：
 *  - 全局对象 FlashDevice：CMSIS FLM 同布局的设备描述（几何/超时/扇区表）；
 *  - 全局函数 Init/UnInit/EraseChip/EraseSector/ProgramPage/Read：
 *    FLM 同名入口符号（Read 可选），参数语义与 ARM 完全一致。
 *
 * ABI（RV32 psABI，参数语义与 FLM/OFL 同构）：
 *  - Init(adr, clk, fcn) / UnInit(fcn) / EraseChip() / EraseSector(adr)
 *    / ProgramPage(adr, sz, buf) / Read(adr, sz, buf)，返回 0 成功；
 *  - 调用现场由主机侧 runner 建立：ra = ebreak 桩地址（返回即停机）、
 *    sp = 工作区栈顶；算法不依赖 gp/tp（-msmall-data-limit=0）；
 *  - 状态区（.bss）由 Init 自清零，主机无须初始化（无启动代码）；
 *    ELF 的 NOBITS bss 不在装载镜像内，主机侧断点桩布局须让开 bss。
 *
 * 镜像链接基址恒 0x0（装载域 ILM@0 低 64KB）。
 */
#ifndef HF_FLASH_ALGO_H
#define HF_FLASH_ALGO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 设备描述块（CMSIS FLM 同构字节布局，SDK HFLink_Flm_ParseDeviceBlock
 * 按偏移解析：132=start / 136=size / 140=page / 148=erased / 152,156=超时
 * / 160 起为 (size,address) 扇区表，0xFFFFFFFF,0xFFFFFFFF 终止）。
 * RV32 自然对齐下各成员偏移与上述契约一致，勿加 packed/改字段序。
 */
typedef struct
{
    uint16_t version;           /**< 0: 描述块版本（0x0100，仅诊断） */
    char name[128];             /**< 2: 设备名（NUL 结尾） */
    uint16_t device_type;       /**< 130: CMSIS DevType 位 */
    uint32_t start;             /**< 132: 存储窗口基地址（XPI0 = 0x80000000） */
    uint32_t size;              /**< 136: 可烧写容量（字节） */
    uint32_t page_size;         /**< 140: 页编程字节数 */
    uint32_t reserved;          /**< 144: FLM Res 字段，恒 0 */
    uint8_t erased_value;       /**< 148: 擦除态值（NOR = 0xFF） */
    uint8_t padding[3];         /**< 149..151: 对齐填充 */
    uint32_t program_timeout_ms; /**< 152: 页编程超时 */
    uint32_t erase_timeout_ms;  /**< 156: 扇区擦除超时 */
    struct
    {
        uint32_t size;   /**< 扇区字节数 */
        uint32_t address; /**< 扇区起始偏移 */
    } sectors[2];        /**< 160 起：均匀扇区一项 + 0xFFFFFFFF 终止符 */
} hf_flash_device_t;

/** 入口五件套 + 可选 Read（FLM 同名符号，返回 0 成功）。
 *  - Init(flash_base, clock_hz, function): flash_base 为 XPI 存储窗口基址
 *    （XPI0=0x80000000，有 XPI1 的 SoC 另支持 0x90000000）；clock_hz/function
 *    当前不参与配置（兼容 FLM 三参签名）；XPI 配置全自动探测（OTP SiP 判定 +
 *    ROM auto_config SFDP 探测 + pin/connection 三组尝试）。
 *  - EraseSector(address) / ProgramPage(address, size, buffer) 的 address
 *    为存储窗口绝对地址，算法内部转换为 flash 内偏移。
 */
int Init(uint32_t flash_base, uint32_t clock_hz, uint32_t function);
int UnInit(uint32_t function);
int EraseChip(void);
int EraseSector(uint32_t address);
int ProgramPage(uint32_t address, uint32_t size, const uint8_t *buffer);
int Read(uint32_t address, uint32_t size, uint8_t *buffer);

/** 全局设备描述块（FLM 同名对象符号，SDK 经 FindSymbol("FlashDevice") 定位）。 */
extern const hf_flash_device_t FlashDevice;

#ifdef __cplusplus
}
#endif

#endif /* HF_FLASH_ALGO_H */
