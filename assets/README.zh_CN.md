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

`fonts/passport_font_16.c` 是本应用使用的 16 px、2 bpp 压缩 Noto Sans SC 字库，覆盖 ASCII、U+00B0（°）、U+00B7（·）、U+2103（℃）、U+3000–3020、U+4E00–9FFF 和 U+FF00–FF60，直接编译进应用并由屏幕标签选用。Noto Sans SC 采用 [`fonts/OFL.txt`](fonts/OFL.txt) 中的 SIL Open Font License；字体源文件由 [Google Fonts](https://github.com/google/fonts/tree/main/ofl/notosanssc) 维护。重新生成命令为 `lv_font_conv@1.5.3 --font PATH_TO_NotoSansSC.ttf --range 0x20-0x7e,0xb0,0xb7,0x2103,0x3000-0x3020,0x4e00-0x9fff,0xff00-0xff60 --size 16 --bpp 2 --format lvgl --no-kerning --lv-include lvgl.h --lv-font-name passport_font_16 -o assets/fonts/passport_font_16.c`。也可运行 `python3 tools/make_font.py PATH_TO_NotoSansSC.ttf`，使用固定版本转换器和上述字符范围重新生成。LVGL 必须启用压缩字库和大字库偏移。范围外字符（包括 emoji）不保证支持。

`fonts/passport_font_20.c` 是 20 px、2 bpp 压缩 Noto Sans SC 标题子集，覆盖 ASCII 和 `tools/make_font.py` 中列出的固定标题。动态回复和名称继续使用应用的完整 16 px 字库。两种字号共用 Noto 的 SIL OFL 许可证，可由该脚本一起重新生成。

16 px 回复字库还包含 `tools/make_font.py` 的完整 `PUNCTUATION` 清单：中文常用句读、直引号／弯引号／直角引号、圆括号／方括号／花括号／方头括号／六角括号、书名号、连字符、破折号、横向省略号、间隔号、项目符号、斜线、分隔线、波浪号、下划线、参照号、百分号／千分号及角分角秒符号，并覆盖全角和小型变体。新增范围包含 U+2010–2017、U+2018/U+2019、U+201C/U+201D、U+2022/U+2025/U+2026/U+2027、U+2030–2037、U+203B、U+22EF、U+3030 以及 U+FE4B–FE5E 中选定的符号。运行 `python3 tools/make_font.py PATH_TO_NotoSansSC.ttf` 可包含全部维护清单，不要仅按基础范围重新生成。`make preview-ui` 通过实际回复标签的字体逐字检查清单，同时验证已知不支持的 emoji 确实缺字，并在中英文界面逐页渲染所有标点。回复分页会让前引号／左括号跟随后文，避免后引号／右括号出现在行首。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 保留在中英文 `docs/README` 中的上游硬件主图，不用于 OpenClaw 产品首页。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标，用于硬件概览浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 字标的白色版本，由硬件概览的 `<picture>` 元素在深色主题使用。 |
| [`images/passport-lobster.svg`](images/passport-lobster.svg)、[`images/passport_lobster_22.c`](images/passport_lobster_22.c) | 22 × 22，SVG 源图和 LVGL ARGB8888 | 项目原创图案，按仓库许可证提供。用于固件 OpenClaw 标题旁的小龙虾，不依赖 emoji 字体。运行 `python3 tools/make_lobster.py`（ImageMagick 7）可重新生成 C 资源。1,936 字节像素数据保存在 Flash 中，只使用一个图片控件。 |
| [`logo/logo.png`](logo/logo.png) | 1024 × 1024，PNG | 既有项目／App 图标，用于 OpenClaw README 标题、网页页头和浏览器标签页。网页通过 Vite 直接引用源素材，不维护重复副本。沿用项目素材，并非新增硬件实拍。 |
| [`images/screenshots/firmware.png`](images/screenshots/firmware.png) | 1600 × 804，PNG RGBA，8 位 | 首页／提问／通话总览，由下述真实渲染图以最近邻放大 2 倍后排版。项目 UI／图案按仓库许可证提供，Noto 字体声明见上文。 |
| [`images/screenshots/firmware-home.png`](images/screenshots/firmware-home.png)、[`images/screenshots/firmware-ask.png`](images/screenshots/firmware-ask.png)、[`images/screenshots/firmware-call.png`](images/screenshots/firmware-call.png) | 各 240 × 320，PNG 索引 RGB，8 位 | 使用 `sim/ui_preview/` 的主机 LVGL 环境，以虚构设备状态渲染实际 `main/passport_screen.c`。原始分辨率截图，不是生成的 UI 概念图。 |
| [`images/screenshots/macos.png`](images/screenshots/macos.png) | 1408 × 1024，PNG RGB，8 位 | 隔离演示构建中的原生 SwiftUI macOS App，使用浅色外观，通过 AppKit 视图捕获。1760 × 1344 原图去掉顶部 64 像素窗口留白，再缩至 80%。项目 UI 按仓库许可证提供，系统控件保留平台外观。 |
| [`images/screenshots/ios.png`](images/screenshots/ios.png) | 804 × 1748，PNG RGBA，8 位 | iPhone 17 模拟器中的原生 App，1206 × 2622 截图缩至三分之二。项目 UI 按仓库许可证提供，系统控件保留平台外观。 |

### 产品截图来源与隐私

网页截图及三张固件产品图于 2026-11-12 使用当前界面实现、虚构设备／浏览器传输状态刷新。
`web-desktop.png` 与 `web-mobile.png` 展示网站，不是原生 App；移动图是浏览器布局演示，非 iPhone 实拍。
`tools/community_materials.mjs` 用这些图生成经检查的 3:4 社区海报，项目画面使用仓库许可。
旧原生客户端截图仅存档，产品 README 不再引用。

截图更新于 2026-11-11。原生 App 使用临时隔离构建、虚构的传输／网络状态和独立示例
Bundle ID，不连接维护者的设备，也不读取其 Wi-Fi 缓存。
固件使用实际界面实现、字体、图标及 RGB565 渲染，填入演示状态。
`make preview-ui` 运行维护中的界面检查；这些产品展示图使用同一渲染器和统一演示数据。

演示网络为 `Passport Home`、`Passport Office`、`Demo iPhone`，
助手为虚构名称，网关为 `wss://gateway.example`。
密码仅为演示值、掩码或留空，不包含真实配对码、代理节点、入口域名或设备身份。
图片已剥离元数据并逐张检查，只用于展示界面，不代表硬件验收。
请勿替换为未经检查的个人设备配置截图。

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

## 语音检测测试素材

[`audio/vad-speech.wav`](audio/vad-speech.wav) 原样取自
[libfvad](https://github.com/dpirch/libfvad/tree/532ab666c20d3cfda38bca63abbb0f152706c369)
的 `tests/data/audio_tiny16.wav`，与 `components/passport_vad` 中的 WebRTC VAD
核心一起固定在提交 `532ab666c20d3cfda38bca63abbb0f152706c369`。
格式为 16 kHz、16 位、单声道 PCM，180,590 字节，未转换。SHA-256：
`551d3f0282c5d5e1e32e76da97bd3532c38e6f4bb02a32510caf562d7c06ae9c`。
保留上游 [BSD 许可证](../components/passport_vad/LICENSE)、
[专利授权](../components/passport_vad/PATENTS) 和
[作者名单](../components/passport_vad/AUTHORS)，固件发布包也包含这些声明。
此素材仅用于主机测试，不嵌入固件。采样率适配、确认、停顿提交和通话过滤
由项目包装层实现，上游核心不变。参见[语音过滤](../docs/configuration.zh_CN.md#语音过滤)。

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
