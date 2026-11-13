#!/usr/bin/env python3
"""把合并镜像和应用 ELF 打成 PassportSim 页面能直接载入的 .pebundle。"""

import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FULL = ROOT / "build" / "FoloToy-AI-Passport-full.bin"
OUT = ROOT / "build" / "passport.pebundle"


def main() -> int:
    # The gate archives a matching ELF; the root incremental build may be old.
    if not FULL.exists():
        print("先运行 tools/validate.sh 编译并验证固件", file=sys.stderr)
        return 1
    digest = hashlib.sha256(FULL.read_bytes()).hexdigest()
    archive = ROOT / "build" / "firmware" / digest
    from archive_firmware import verify_archive
    try:
        verify_archive(archive)
        flash_path = archive / FULL.name
        elf_path = archive / "FoloToy-AI-Passport.elf"
        flash, elf = flash_path.read_bytes(), elf_path.read_bytes()
    except (OSError, ValueError) as error:
        print(f"没有匹配的固件和 ELF：{error}", file=sys.stderr)
        return 1
    (ROOT / "build" / "passport-flash.bin").write_bytes(flash)
    manifest = "\n".join([
        "[bundle]",
        'id = "ai-passport-agent"',
        'name = "AI Passport Agent"',
        "",
        "[[file]]",
        'role = "flash"',
        f'name = "{flash_path.name}"',
        f"len = {len(flash)}",
        f'sha256 = "{hashlib.sha256(flash).hexdigest()}"',
        "",
        "[[file]]",
        'role = "app_elf"',
        f'name = "{elf_path.name}"',
        f"len = {len(elf)}",
        f'sha256 = "{hashlib.sha256(elf).hexdigest()}"',
        "",
    ]).encode()
    blob = b"PEBUNDL1" + len(manifest).to_bytes(4, "little") + manifest + flash + elf
    OUT.write_bytes(blob)
    print(f"{OUT} (firmware {digest[:12]})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
