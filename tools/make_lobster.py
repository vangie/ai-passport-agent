#!/usr/bin/env python3
"""Export backend header artwork as compact Flash-resident LVGL images."""

from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
SIZE = 22


def export(name):
    source = ROOT / f"assets/images/passport-{name}.svg"
    output = ROOT / f"assets/images/passport_{name}_22.c"
    pixels = subprocess.check_output([
        "magick", "-background", "none", str(source), "-resize", f"{SIZE}x{SIZE}!",
        "+dither", "-colors", "16", "-depth", "8", "BGRA:-",
    ])
    if len(pixels) != SIZE * SIZE * 4:
        raise ValueError("The header artwork must remain 22 × 22 pixels")
    colors = sorted(set(pixels[index:index + 4] for index in range(0, len(pixels), 4)))
    if len(colors) > 16:
        raise ValueError("The header palette must fit in four bits")
    indices = [colors.index(pixels[index:index + 4]) for index in range(0, len(pixels), 4)]
    data = b"".join(colors).ljust(64, b"\0") + bytes(
        (indices[index] << 4) | indices[index + 1] for index in range(0, len(indices), 2)
    )
    rows = [
        "    " + ", ".join(f"0x{byte:02x}" for byte in data[index:index + 16]) + ","
        for index in range(0, len(data), 16)
    ]
    output.write_text(
        '#include "lvgl.h"\n\n'
        'static const uint8_t pixels[] = {\n' + "\n".join(rows) + '\n};\n\n'
        f'const lv_image_dsc_t passport_{name}_22 = {{\n'
        '    .header = {\n'
        '        .magic = LV_IMAGE_HEADER_MAGIC,\n'
        '        .cf = LV_COLOR_FORMAT_I4,\n'
        f'        .w = {SIZE}, .h = {SIZE}, .stride = {SIZE // 2},\n'
        '    },\n'
        '    .data_size = sizeof(pixels),\n'
        '    .data = pixels,\n'
        '};\n', encoding="utf-8",
    )
    print(f"Generated {output.relative_to(ROOT)} ({len(data)} bytes of image data)")


def main():
    for name in ("lobster", "hermes"):
        export(name)


if __name__ == "__main__":
    main()
