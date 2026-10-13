#!/usr/bin/env python3
"""从 hpm_sdk 导入构建算法所需的最小头文件闭包。

闭包原则：只拷贝算法源码 #include 的传递依赖，不拖入 hpm_soc.h / hpm_sdp_drv.h
全家桶（soc 版 hpm_romapi.h 会连锁引入 SDP/XPI RAM/外设头，对算法无用）。

  hpm_headers/common/   <- drivers/inc: hpm_common.h, hpm_romapi_xpi_def.h, hpm_romapi_xpi_nor_def.h
  hpm_headers/<SOC>/    <- soc/<series>/<soc>: hpm_romapi_xpi_soc_def.h, hpm_otp_drv.h

用法:
    python tools/import_headers.py --hpm-sdk <本地 hpm_sdk 根目录>
"""

import argparse
import json
import shutil
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

COMMON_HEADERS = [
    "hpm_common.h",
    "hpm_romapi_xpi_def.h",
    "hpm_romapi_xpi_nor_def.h",
]

SOC_HEADERS = [
    "hpm_romapi_xpi_soc_def.h",
    "hpm_otp_drv.h",
]


def main() -> int:
    parser = argparse.ArgumentParser(description="Import minimal hpm_sdk header closure")
    parser.add_argument("--hpm-sdk", required=True, help="hpm_sdk 根目录")
    args = parser.parse_args()

    sdk = Path(args.hpm_sdk)
    if not (sdk / "drivers" / "inc" / "hpm_common.h").is_file():
        print(f"错误: {sdk} 不是有效的 hpm_sdk 根目录", file=sys.stderr)
        return 1

    socs_file = REPO_ROOT / "soc" / "socs.json"
    socs = json.loads(socs_file.read_text(encoding="utf-8"))["socs"]

    out_root = REPO_ROOT / "hpm_headers"
    common_out = out_root / "common"
    common_out.mkdir(parents=True, exist_ok=True)
    for name in COMMON_HEADERS:
        src = sdk / "drivers" / "inc" / name
        shutil.copyfile(src, common_out / name)
        print(f"common/{name}")

    for soc in socs:
        soc_dir = sdk / "soc" / soc["sdk_soc_dir"]
        soc_out = out_root / soc["soc"]
        soc_out.mkdir(parents=True, exist_ok=True)
        for name in SOC_HEADERS:
            src = soc_dir / name
            if not src.is_file():
                print(f"错误: {soc['soc']} 缺少 {name}", file=sys.stderr)
                return 1
            shutil.copyfile(src, soc_out / name)
        print(f"{soc['soc']}/ ({', '.join(SOC_HEADERS)})")

    notice = out_root / "README.md"
    notice.write_text(
        "# hpm_headers\n\n"
        "本目录由 `tools/import_headers.py` 从 hpm_sdk（Apache-2.0）生成，请勿手改。\n"
        f"来源: hpm_sdk {sdk}\n\n"
        "- `common/`: drivers/inc 的 3 个共享 romapi 头\n"
        "- `<SOC>/`: 各 SoC 的 hpm_romapi_xpi_soc_def.h / hpm_otp_drv.h\n",
        encoding="utf-8",
    )
    print(f"写入 {notice}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
