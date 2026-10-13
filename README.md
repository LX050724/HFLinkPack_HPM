# HFLinkPack_HPM

HFLink 调试器的 HPM（先楫半导体）Flash 烧录算法仓库。RV32 位置无关 stub 经
HPM boot ROM API 完成片上/外挂 XPI NOR 的擦写；产物以 zip 打包为 HFLink 原生
芯片包，经 GitHub Release 发布，供 PackManager 自建源在线安装。

## 算法模型

- **RAM stub + ROM API**（OpenOCD `hpm_xpi` / probe-rs / SEGGER Open Flashloader
  三方同构路线）：算法装入 ILM@0 低 64KB，经 ROM API 表根（`0x2001FF00`，全
  HPM 系一致）的 `xpi_nor_driver_if`（表 +0x14）执行擦写。
- **XPI 配置全自动探测**（SEGGER `flash_auto_probe` 同语义）：OTP word7 bit24
  判 SiP 片内 flash → `auto_config`（header `0xFCF90002`，option0=5 即 104MHz
  档 + SFDP/SDR 探测）→ 依次尝试 option1 ∈ {0x1000, 0x0000, 0x0100}；SiP 探测
  失败即放弃，非 SiP 换组重试，三组全败 Init 失败。不含任何板级写死配置。
- **入口 ABI 与 CMSIS FLM / SEGGER OFL 同构**：
  `Init(adr, clk, func)` / `UnInit(func)` / `EraseChip()` / `EraseSector(adr)` /
  `ProgramPage(adr, sz, buf)` / `Read(adr, sz, buf)`（可选），返回 0 成功；
  RV32 psABI 传参（a0-a2 / a0-a3），ra 指向主机预置 ebreak 桩返回。
- **ELF 容器，FLM 同构**（与 ARM 下载算法同一容器）：产物为 RV32 ELF，
  镜像内自描述——全局对象 `FlashDevice`（FLM 同布局设备描述：几何/超时/
  均匀扇区表）+ FLM 同名入口符号 `Init`/`UnInit`/`EraseChip`/`EraseSector`/
  `ProgramPage`（可选 `Read`）。SDK 侧 `HFLink_Flm_Parse` 按 ELF `e_machine`
  分发调用约定（EM_ARM → Cortex-M、EM_RISCV → RV32 ebreak），**不产 bin、
  无自造头格式**。ELF 链接基址恒 0x0；状态区（.bss）为 NOBITS 不落盘，
  Init 自清零（无启动代码，编译 `-msmall-data-limit=0` 不依赖 gp）；
  链接脚本 `EXTERN` 声明防 `--gc-sections` 裁掉无引用的入口/描述符号。
- **系列共用一个 ELF**（SEGGER 语义）：`socs.json` 的 `group` 字段按
  (xpi0_base, xpi1_base) 元组聚合（同窗口基址在不同族映射不同 XPI 控制器，
  运行时不可推导，只能按组链接），每组一个 `build/out/<group>.elf`。

## 仓库布局

```
algo/                 全系共用算法源码（hf_flash_algo.c/.h/.ld，FLM 同构契约头）
hpm_headers/          hpm_sdk 最小头闭包（tools/import_headers.py 生成，勿手改）
soc/socs.json         SoC 清单单一来源（CMake 与 pack.py 共读）
pack/HPM/<ver>/       芯片包权威数据（pack.json，zip 按其声明自洽打包）
tools/import_headers.py  从 hpm_sdk 同步头闭包
tools/pack.py         芯片包 zip 打包（不做 ELF 内容校验——消费侧 HFLink_Flm_Parse
                      是权威解析器）
```

## 构建

依赖 riscv32 交叉工具链（自动探测 PATH 中已知前缀；或
`-DHFALGO_TOOLCHAIN_BIN=<bin目录>` 显式指定）：

```bash
cmake -S . -B build -G Ninja
cmake --build build          # 按组出 build/out/<group>.elf（系列共用）
python tools/pack.py --pack  # dist/HFLink.HPM.<ver>.zip
```

## SoC 覆盖

HPM5300/5E00/6200/6300/6700/6800/6E00/6P00 各一个代表 SoC（清单见
`soc/socs.json`）。HPM6360/HPM6750 同一 bin 支持 XPI0（0x80000000）与 XPI1
（0x90000000）双口（Init 按 flash 基址运行时分派）。**仅 HPM5361 经实机烧写
验证**，其余为 build-only（manifest `tested_hw` 标注）。

## 代码来源与许可

- 算法实现参考 [hpmicro/segger_open_flashloader](https://github.com/hpmicro/segger_open_flashloader)
- 整体许可：[LICENSE](LICENSE)（BSD 3-Clause，自 hpm_sdk 继承，HPMicro 版权）
