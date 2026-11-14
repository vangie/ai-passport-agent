#!/usr/bin/env python3
"""Regenerate the passport fonts, including reply punctuation and temperature signs."""

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
    "→←↑↓↔↕↩↪✓✔✕✖×÷±≈≠≤≥∞√∑∫★☆⌨"
)
# Every fixed headline the screens show, so no state falls back to 16 px.
HEADLINES = ("想聊点什么？正在听准备录音正在发送等待回复未能发送录音可重试正在结束连接未完成"
             "尚无网络助手蓝牙配置设置聊天写中断已静音量返回")


def clean_generated_header(output: Path, source: Path) -> None:
    text = output.read_text()
    text = text.replace(str(source), source.name)
    text = text.replace(str(output), str(output.relative_to(ROOT)))
    output.write_text(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("font", type=Path, help="Path to the licensed NotoSansSC.ttf source")
    args = parser.parse_args()
    font = args.font.expanduser().resolve()
    if not font.is_file():
        parser.error("The source font does not exist")
    font16 = ROOT / "assets/fonts/passport_font_16.c"
    subprocess.run([
        "npx", "--yes", "lv_font_conv@1.5.3", "--font", str(font),
        "--range", RANGES, "--symbols", PUNCTUATION,
        "--size", "16", "--bpp", "2", "--format", "lvgl",
        "--no-kerning", "--lv-include", "lvgl.h", "--lv-font-name", "passport_font_16",
        "-o", str(font16),
    ], cwd=ROOT, check=True)
    clean_generated_header(font16, font)
    font20 = ROOT / "assets/fonts/passport_font_20.c"
    subprocess.run([
        "npx", "--yes", "lv_font_conv@1.5.3", "--font", str(font),
        "--range", "0x20-0x7e", "--symbols", HEADLINES,
        "--size", "20", "--bpp", "2", "--format", "lvgl", "--no-kerning",
        "--lv-include", "lvgl.h", "--lv-font-name", "passport_font_20",
        "-o", str(font20),
    ], cwd=ROOT, check=True)
    clean_generated_header(font20, font)


if __name__ == "__main__":
    main()
