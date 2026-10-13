#!/usr/bin/env python3
"""HFLink HPM 烧录算法打包器：产芯片包 zip（发布管道唯一动作）。

产物是 HFLink 原生芯片包（GUI PackManager 可直接导入安装的 zip）：
    <zip 根>/pack.json           ← 芯片包描述（Database JSON schema 3）
    <zip 根>/flash/<group>.elf   ← 系列算法（RV32 ELF，FLM 同容器）

芯片包数据（memory/processor/debug 等）的权威来源是本仓库的 pack/<name>/<ver>/
目录（HFLinkSDK 仓库为 private，CI 无法跨仓取数，2026-10-14 起权威由 SDK 侧
迁入本仓库）。系列算法按 socs.json 的 group 聚合（SEGGER 语义：一个系列共用
一个算法）。HPM 全系系列以 family 并入单一 HPM 包（用户裁定 2026-10-13，
不再按系列另开包）。

算法文件内容（ELF 头/符号/FlashDevice 几何）由 SDK 侧 HFLink_Flm_Parse 权威
校验（GUI 导入、烧写、SDK 测试均走该解析器），本脚本不重复解析——快速自检用
工具链命令即可：
    riscv-none-elf-nm build/out/<group>.elf    # 入口/FlashDevice 符号在
    riscv-none-elf-readelf -h build/out/<group>.elf

用法:
    python tools/pack.py --pack    # 读 pack/ 目录产 dist/<vendor>.<name>.<ver>.zip
"""

import argparse
import json
import sys
import zipfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PACK_ROOT = REPO_ROOT / "pack"


def group_elfs(build_dir: Path, socs_meta: dict) -> dict:
    """分组 -> elf 路径（socs.json 的 group 字段聚合，与 CMake 同源）。"""
    groups = {}
    for soc in socs_meta["socs"]:
        groups.setdefault(soc["group"], None)
    for group in groups:
        elf_path = build_dir / "out" / f"{group}.elf"
        if not elf_path.is_file():
            sys.exit(f"错误: 缺少 {elf_path}（先 cmake --build build）")
        if elf_path.stat().st_size == 0:
            sys.exit(f"错误: {elf_path} 为空文件")
        groups[group] = elf_path
    return groups


def pack_dirs() -> list:
    """枚举 pack/<name>/<ver>/ 权威包目录（含 pack.json）。"""
    if not PACK_ROOT.is_dir():
        sys.exit(f"错误: 权威包目录不存在：{PACK_ROOT}")
    dirs = [manifest.parent for manifest in sorted(PACK_ROOT.glob("*/*/pack.json"))]
    if not dirs:
        sys.exit(f"错误: {PACK_ROOT} 下未找到任何 <name>/<ver>/pack.json")
    return dirs


def pack_chip_zip(dist_dir: Path, elfs: dict) -> None:
    """读权威包目录产芯片包 zip：pack.json + 声明的算法文件（elf 取自 build/out）。"""
    dist_dir.mkdir(parents=True, exist_ok=True)
    for pack_dir in pack_dirs():
        meta = json.loads((pack_dir / "pack.json").read_text(encoding="utf-8"))
        zip_name = f"{meta['vendor']}.{meta['name']}.{meta['version']}.zip"

        # 收集 pack.json 声明的算法文件：声明路径为安装布局（Packs/<vendor>/<name>/<ver>/flash/...），
        # 取 marker 后段映射到 build/out 的组 elf（包自洽：只带声明的文件）
        flash_files = []
        for family in meta["documents"][0]["families"].values():
            for algo in family.get("algorithm") or []:
                rel = algo["path"]
                marker = f"{meta['name']}/{meta['version']}/"
                if marker not in rel:
                    sys.exit(f"错误: 算法 path 未按安装布局声明：{rel}")
                local = rel.split(marker, 1)[1]
                if not local.startswith("flash/"):
                    sys.exit(f"错误: 算法 path 非 flash/ 内文件：{rel}")
                group = local[len("flash/"):].rsplit(".", 1)[0]
                if group not in elfs:
                    sys.exit(f"错误: 算法声明的组缺少构建产物：{group}（{rel}）")
                flash_files.append((f"flash/{Path(local).name}", elfs[group]))

        zip_path = dist_dir / zip_name
        with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
            zf.write(pack_dir / "pack.json", "pack.json")
            for arcname, source in sorted(flash_files):
                zf.write(source, arcname)
        print(f"芯片包 {zip_path}")


def main() -> int:
    parser = argparse.ArgumentParser(description="打包 HPM 烧录算法（芯片包格式，ELF 算法）")
    parser.add_argument("--build-dir", default=str(REPO_ROOT / "build"))
    parser.add_argument("--dist-dir", default=str(REPO_ROOT / "dist"))
    parser.add_argument("--pack", action="store_true", help="产芯片包 zip（读 pack/ 权威目录）")
    args = parser.parse_args()

    socs_meta = json.loads((REPO_ROOT / "soc" / "socs.json").read_text(encoding="utf-8"))
    elfs = group_elfs(Path(args.build_dir), socs_meta)
    for group, path in sorted(elfs.items()):
        members = [s["soc"] for s in socs_meta["socs"] if s["group"] == group]
        print(f"组 {group}: {', '.join(members)} {path.name} ({path.stat().st_size} 字节)")

    if args.pack:
        pack_chip_zip(Path(args.dist_dir), elfs)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
