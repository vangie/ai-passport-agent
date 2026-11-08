<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐、电池参数等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

`fonts/badge_font_16.c` 是本应用使用的 16 px、2 bpp 压缩 Noto Sans SC 字库，覆盖 ASCII、U+00B0（°）、U+00B7（·）、U+2103（℃）、U+3000–3020、U+4E00–9FFF 和 U+FF00–FF60，直接编译进应用并由屏幕标签选用。Noto Sans SC 采用 [`fonts/OFL.txt`](fonts/OFL.txt) 中的 SIL Open Font License；字体源文件由 [Google Fonts](https://github.com/google/fonts/tree/main/ofl/notosanssc) 维护。重新生成命令为 `lv_font_conv@1.5.3 --font PATH_TO_NotoSansSC.ttf --range 0x20-0x7e,0xb0,0xb7,0x2103,0x3000-0x3020,0x4e00-0x9fff,0xff00-0xff60 --size 16 --bpp 2 --format lvgl --no-kerning --lv-include lvgl.h --lv-font-name badge_font_16 -o assets/fonts/badge_font_16.c`。也可运行 `python3 tools/make_font.py PATH_TO_NotoSansSC.ttf`，使用固定版本转换器和上述字符范围重新生成。LVGL 必须启用压缩字库和大字库偏移。范围外字符（包括 emoji）不保证支持。

`fonts/badge_font_20.c` 是 20 px、2 bpp 压缩 Noto Sans SC 标题子集，覆盖 ASCII 和 `tools/make_font.py` 中列出的固定标题。动态回复和名称继续使用应用的完整 16 px 字库。两种字号共用 Noto 的 SIL OFL 许可证，可由该脚本一起重新生成。

16 px 回复字库还包含 `tools/make_font.py` 的完整 `PUNCTUATION` 清单：中文常用句读、直引号／弯引号／直角引号、圆括号／方括号／花括号／方头括号／六角括号、书名号、连字符、破折号、横向省略号、间隔号、项目符号、斜线、分隔线、波浪号、下划线、参照号、百分号／千分号及角分角秒符号，并覆盖全角和小型变体。新增范围包含 U+2010–2017、U+2018/U+2019、U+201C/U+201D、U+2022/U+2025/U+2026/U+2027、U+2030–2037、U+203B、U+22EF、U+3030 以及 U+FE4B–FE5E 中选定的符号。运行 `python3 tools/make_font.py PATH_TO_NotoSansSC.ttf` 可包含全部维护清单，不要仅按基础范围重新生成。`make preview-ui` 通过实际回复标签的字体逐字检查清单，同时验证已知不支持的 emoji 确实缺字，并在中英文界面逐页渲染所有标点。回复分页会让前引号／左括号跟随后文，避免后引号／右括号出现在行首。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |
| [`images/badge-lobster.svg`](images/badge-lobster.svg)、[`images/badge_lobster_22.c`](images/badge_lobster_22.c) | 22 × 22，SVG 源图和 LVGL ARGB8888 | 项目原创图案，按仓库许可证提供。用于固件 OpenClaw 标题旁的小龙虾，不依赖 emoji 字体。运行 `python3 tools/make_lobster.py`（ImageMagick 7）可重新生成 C 资源。1,936 字节像素数据保存在 Flash 中，只使用一个图片控件。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。

## 电池参数（battery）

[`battery/wanli-500mah-profile2-xil40x-20261020.txt`](battery/wanli-500mah-profile2-xil40x-20261020.txt)
保存万锂新能源 500 mAh 电芯的 CW2017 参数（profile2、XIL40X、2026-10-20）。
该文件是项目提供的 `喜梨_智能工卡_万锂新能源500mAh_profile2_XIL40X_20261020.txt`
的原样副本，按维护者要求保留用于本次固件更新；原文件没有附带单独的许可说明。

格式为 80 行两位十六进制数字，每行一个字节，按寄存器顺序排列。
BSP 在 `components/bsp/src/bsp_battery.c` 中嵌入这 80 字节，主机检查会验证
源文件及数组内容完全一致。文本文件 SHA-256：
`7ea96eba4634f4495a37c25ffacd54c1deaa0693b162c892d671a2ba68460919`。
解码后 80 字节 SHA-256：
`fa7365671093ef449c5bdcfb4adb279e3d275f888109f50b6b2f21e9b96220d5`。
这是电量计参数，不是充电配置，也不能通用于所有 500 mAh 电芯；详见
[硬件指南](../docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md#9-cw2017-电池计)。
