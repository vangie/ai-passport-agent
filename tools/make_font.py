#!/usr/bin/env python3
"""Regenerate the badge fonts, including reply punctuation and temperature signs."""

import argparse
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
RANGES = "0x20-0x7e,0xb0,0xb7,0x2103,0x3000-0x3020,0x4e00-0x9fff,0xff00-0xff60"
PUNCTUATION = (
    "，。！？；：、﹐﹑﹒﹔﹕﹖﹗"
    "“”‘’「」『』〝〞〟"
    "（）［］｛｝｟｠〔〕【】〖〗〘〙〚〛《》〈〉﹙﹚﹛﹜﹝﹞"
    "—―–‐‑‒…‥⋯·・•‧"
    "／＼｜～〜〰﹏＿﹋﹌※＊＃＠＆％‰‱′″‵‶‴‷§¶†‡‖‗"
)
HEADLINES = "想聊点什么？正在听准备录音正在发送等待回复未能发送录音可重试正在结束连接未完成尚无网络助手蓝牙配置设置聊天写"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("font", type=Path, help="Path to the licensed NotoSansSC.ttf source")
    args = parser.parse_args()
    font = args.font.expanduser().resolve()
    if not font.is_file():
        parser.error("The source font does not exist")
    subprocess.run([
        "npx", "--yes", "lv_font_conv@1.5.3", "--font", str(font),
        "--range", RANGES, "--symbols", PUNCTUATION,
        "--size", "16", "--bpp", "2", "--format", "lvgl",
        "--no-kerning", "--lv-include", "lvgl.h", "--lv-font-name", "badge_font_16",
        "-o", "assets/fonts/badge_font_16.c",
    ], cwd=ROOT, check=True)
    subprocess.run([
        "npx", "--yes", "lv_font_conv@1.5.3", "--font", str(font),
        "--range", "0x20-0x7e", "--symbols", HEADLINES,
        "--size", "20", "--bpp", "2", "--format", "lvgl", "--no-kerning",
        "--lv-include", "lvgl.h", "--lv-font-name", "badge_font_20",
        "-o", "assets/fonts/badge_font_20.c",
    ], cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
