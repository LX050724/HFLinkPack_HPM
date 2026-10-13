/*
 * hf_flash_algo.ld - HPM 烧录算法链接脚本（系列共用）
 *
 * 装载域：ILM 基址 0x0 低 64KB（HPM 全系 ILM 起始一致；下载会话期间目标被
 * halt，ILM 内容可被算法占用——与 FLM 占用 RAM 同语义，烧写后复位重载）。
 *
 * 产物为 ELF（FLM 同容器）：几何/入口经符号表自描述（FlashDevice 对象 +
 * Init/UnInit/... 函数符号），无自造头段。
 * 段序：text/rodata(含 FlashDevice) -> data -> bss(NOBITS)。
 * 状态区不落盘：.bss 为 NOBITS，由算法 Init 自清零；装载镜像不含 bss，
 * 主机侧断点桩布局须让开 bss（memsz 尾）。
 * 无栈段：SP 由主机侧 runner 指向工作区高端，不占镜像。
 */

ENTRY(Init)

/* SDK 经 ELF 符号表定位入口与设备描述（无代码引用），--gc-sections 下显式保留 */
EXTERN(Init UnInit EraseChip EraseSector ProgramPage Read FlashDevice)

MEMORY
{
    ILM (rwx) : ORIGIN = 0x00000000, LENGTH = 64K
}

SECTIONS
{
    .text :
    {
        *(.text .text.*)
    } > ILM

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
    } > ILM

    .data :
    {
        . = ALIGN(4);
        *(.data .data.*)
        . = ALIGN(4);
    } > ILM

    .bss (NOLOAD) :
    {
        . = ALIGN(8);
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > ILM

    hf_bss_size = SIZEOF(.bss);

    /DISCARD/ :
    {
        *(.eh_frame .eh_frame_hdr)
        *(.comment)
        *(.note .note.*)
        *(.riscv.attributes)
    }
}
