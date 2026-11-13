#!/usr/bin/env python3
"""从 assets/logo/source.jpg 生成 AI Passport Agent 的图标。

源图可以是满版的正方形，也可以自带黑色圆角。
Mac 图标裁成 macOS 的圆角，圆角外透明；iPhone 会自己裁圆角，
所以给它满版的正方形，源图的黑色圆角用相邻的画面颜色补上。

Mac 窗口顶部那张图另用 assets/logo/header.jpg，裁成圆角存进 Mac 的图片资源。

输出：
  assets/logo/logo.png                      1024，透明圆角，README 用
  mac/PassportMac/Assets.xcassets/Passport...  Mac 窗口顶部的图
  mac/PassportMac/Assets.xcassets/AppIcon...   Mac 各尺寸
  ios/Passport/Assets.xcassets/AppIcon...      iPhone 1024，满版无透明
"""

import json
from pathlib import Path

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/logo/source.jpg"
HEADER = ROOT / "assets/logo/header.jpg"
HEADER_PT = 88
S = 1024
# Mac 图标在 1024 画布里留边，和系统其他图标一样大
MAC_INNER = 824


def has_black_corners(src):
    height, width, _ = src.shape
    corners = [src[2, 2], src[2, width - 3], src[height - 3, 2], src[height - 3, width - 3]]
    return all(int(c.sum()) < 40 for c in corners)


def painted_square():
    """返回裁成正方形的画面和它的形状（255 是画面）。"""
    src = np.array(Image.open(SOURCE).convert("RGB"))
    height, width, _ = src.shape
    if not has_black_corners(src):
        side = min(height, width)
        y0, x0 = (height - side) // 2, (width - side) // 2
        return src[y0:y0 + side, x0:x0 + side], np.full((side, side), 255, np.uint8)
    bright = (src.astype(np.int32).sum(axis=2) > 40).astype(np.uint8) * 255
    count, labels, stats, _ = cv2.connectedComponentsWithStats(bright)
    biggest = 1 + np.argmax(stats[1:, cv2.CC_STAT_AREA])
    blob = (labels == biggest).astype(np.uint8) * 255
    # 画面中间也有暗处，只取外轮廓再填满
    contours, _ = cv2.findContours(blob, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
    shape = np.zeros_like(blob)
    cv2.drawContours(shape, contours, -1, 255, thickness=cv2.FILLED)
    # 往里收几像素，去掉 JPEG 压在黑边上的毛刺
    shape = cv2.erode(shape, np.ones((3, 3), np.uint8), iterations=3)

    x, y, w, h = cv2.boundingRect(shape)
    side = max(w, h)
    x0, y0 = x + w // 2 - side // 2, y + h // 2 - side // 2
    pad = max(0, -x0, -y0, x0 + side - width, y0 + side - height)
    src = cv2.copyMakeBorder(src, pad, pad, pad, pad, cv2.BORDER_CONSTANT, value=0)
    shape = cv2.copyMakeBorder(shape, pad, pad, pad, pad, cv2.BORDER_CONSTANT, value=0)
    window = (slice(y0 + pad, y0 + pad + side), slice(x0 + pad, x0 + pad + side))
    return src[window], shape[window]


def squircle(side):
    """macOS 图标的连续圆角，用超椭圆近似。"""
    big = side * 4
    r = (np.arange(big) + 0.5) / big * 2 - 1
    inside = (np.abs(r)[None, :] ** 5 + np.abs(r)[:, None] ** 5) <= 1
    return Image.fromarray(inside.astype(np.uint8) * 255).resize((side, side), Image.LANCZOS)


def rounded(art, shape):
    image = Image.fromarray(art).convert("RGBA")
    own = Image.fromarray(shape).filter(ImageFilter.GaussianBlur(1.2))
    # 满版的源图没有自己的圆角，套 macOS 的
    if shape.min() == 255:
        own = squircle(art.shape[0])
    image.putalpha(own)
    return image


def full_bleed(art, shape):
    """圆角外每个像素取离它最近的画面像素的颜色。"""
    side = art.shape[0]
    outside = (shape == 0).astype(np.uint8)
    if not outside.any():
        return Image.fromarray(art)
    _, nearest = cv2.distanceTransformWithLabels(outside, cv2.DIST_L2, 5,
                                                 labelType=cv2.DIST_LABEL_PIXEL)
    ys, xs = np.where(outside == 0)
    owner = np.zeros(int(nearest.max()) + 1, dtype=np.int64)
    owner[nearest[ys, xs]] = ys * side + xs
    filled = art.reshape(-1, 3)[owner[nearest].reshape(-1)].reshape(side, side, 3)
    filled[outside == 0] = art[outside == 0]
    return Image.fromarray(filled)


def mac_icon(art_rounded):
    out = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    body = art_rounded.resize((MAC_INNER, MAC_INNER), Image.LANCZOS)
    offset = (S - MAC_INNER) // 2
    shadow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    shadow.paste((0, 0, 0, 110), (offset, offset + 12), body.getchannel("A"))
    out.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(14)))
    out.alpha_composite(body, (offset, offset))
    return out


def write_contents(folder, images):
    (folder / "Contents.json").write_text(
        json.dumps({"images": images, "info": {"author": "xcode", "version": 1}}, indent=2) + "\n")
    (folder.parent / "Contents.json").write_text(
        json.dumps({"info": {"author": "xcode", "version": 1}}, indent=2) + "\n")


def write_mac(icon):
    folder = ROOT / "mac/PassportMac/Assets.xcassets/AppIcon.appiconset"
    folder.mkdir(parents=True, exist_ok=True)
    images = []
    for pt in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            name = f"icon_{pt}x{pt}{'@2x' if scale == 2 else ''}.png"
            icon.resize((pt * scale, pt * scale), Image.LANCZOS).save(folder / name)
            images.append({"idiom": "mac", "size": f"{pt}x{pt}", "scale": f"{scale}x", "filename": name})
    write_contents(folder, images)


def write_ios(full):
    folder = ROOT / "ios/Passport/Assets.xcassets/AppIcon.appiconset"
    folder.mkdir(parents=True, exist_ok=True)
    full.resize((S, S), Image.LANCZOS).convert("RGB").save(folder / "icon-1024.png")
    write_contents(folder, [{"idiom": "universal", "platform": "ios", "size": "1024x1024",
                             "filename": "icon-1024.png"}])


def write_header():
    """窗口顶部的图：裁成圆角，1x 和 2x 两份。"""
    if not HEADER.exists():
        return
    art = Image.open(HEADER).convert("RGB")
    side = min(art.size)
    left, top = (art.width - side) // 2, (art.height - side) // 2
    art = art.crop((left, top, left + side, top + side)).convert("RGBA")
    big = side * 2
    mask = Image.new("L", (big, big), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, big - 1, big - 1], radius=round(big * 0.2), fill=255)
    art.putalpha(mask.resize((side, side), Image.LANCZOS))
    folder = ROOT / "mac/PassportMac/Assets.xcassets/Passport.imageset"
    folder.mkdir(parents=True, exist_ok=True)
    images = []
    for scale in (1, 2):
        name = f"passport{'@2x' if scale == 2 else ''}.png"
        art.resize((HEADER_PT * scale, HEADER_PT * scale), Image.LANCZOS).save(folder / name)
        images.append({"idiom": "universal", "scale": f"{scale}x", "filename": name})
    (folder / "Contents.json").write_text(
        json.dumps({"images": images, "info": {"author": "xcode", "version": 1}}, indent=2) + "\n")


def main():
    art, shape = painted_square()
    icon = mac_icon(rounded(art, shape))
    icon.save(ROOT / "assets/logo/logo.png")
    write_mac(icon)
    write_ios(full_bleed(art, shape))
    write_header()
    print(ROOT / "assets/logo/logo.png")


if __name__ == "__main__":
    main()
