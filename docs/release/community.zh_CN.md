<p align="right"><strong>简体中文</strong> · <a href="community.md">English</a></p>

# 社区发布材料 · v0.5.1

已准备供作者预览，**尚未提交审核**。简介、使用方法、更新日志分别填写。
不复用已过期授权，不为更新而另建重复玩法；分类由服务端判断。

## 标题

AI Passport OpenClaw · 随身语音助手

## 简介

你的 OpenClaw，轻量的随身语音入口。「提问」按键录音，回显转写和逐步呈现的文字回复，
读完接着追问；「通话」直接说、听语音回复，配合滚动字幕，按屏幕提示静音、打断或结束。

配置与刷机在浏览器完成：电脑 Chrome／Edge 通过 USB 安装和更新，兼容蓝牙浏览器修改 Wi-Fi、
个人热点和助手连接；iPhone 可用 Bluefy。配置保存在 Passport 上，网页没有应用后台、不中转对话。
配置后设备通过 Wi-Fi 直连网关，不需要配置应用常驻。

需自备已运行、有可用模型的 OpenClaw。网页内复制提示词，交给能操作网关主机的 Agent，
由它检查插件、专属助手与安全入口，再生成受限配对信息。公网接入可能需要自己的隧道资源与授权；
通话另需实时语音服务，服务商费用另计。这是独立社区开源项目。配图为实际界面的虚构数据演示，非实机照片。

## 使用方法

1. 电脑 Chrome／Edge 打开 https://ai-passport-openclaw.codelife.me/，USB 连接设备。
   首次取消「保留配置」并安装确认；兼容更新可保留配置。
2. 在 OpenClaw 区域复制提示词，交给能操作已运行网关的 Agent；完成插件与可达入口准备，
   将返回的地址、配对码填回网页。通话另需实时语音服务。
3. 扫描选择 2.4 GHz Wi-Fi，输入密码并保存，等设备出现 Wi-Fi 图标。失败修改重试，不必刷机；
   配对码过期需重取。iPhone 热点打开「允许其他人加入／最大兼容性」，扫描不到时停留在个人热点设置页。
4. 「提问」按一下录音，再按发送或停顿自动提交，读完可追问／翻页；「通话」等「正在听」后说话，
   按提示静音、打断或结束。换网到「设置 → Wi-Fi」，新增网络用 USB 或 Bluefy 等蓝牙浏览器。

## 更新日志

- 网页成为主入口，支持电脑 USB 刷机、蓝牙配置和 iPhone Bluefy。
- 统一保存／撤销、渐进式 Wi-Fi 扫描、助手独立线路及同站点固件。
- 改进加密蓝牙接管，优化连接状态和侧边按键提示。
- 移除原生客户端发布包，保留固件、插件与完整性校验文件。

## 文件与来源

- 源码：https://github.com/vangie/ai-passport-openclaw
- 版本：https://github.com/vangie/ai-passport-openclaw/releases/tag/v0.5.1
- 固件使用该 Release 经验证的 `FoloToy-AI-Passport-full.bin`，不使用旧本地构建；先核对发布回执与哈希。
- 封面 `00-cover.png`，附图 `01-ask-and-call.png`、`02-web-studio.png`、`03-mobile-setup.png`。
  均为 1152 × 1536 竖版，使用虚构 Wi-Fi／助手及 `wss://gateway.example`。
- 准备网页依赖后运行 `node tools/community_materials.mjs` 可复现。输出与独立 UTF-8 提交字段放在
  忽略目录 `build/community-market/v0.5.1/materials/`，脚本不上传。
- 检查四张最终本地 PNG 与元数据预览，作者认可后才提交社区；GitHub 发布不等于社区审核通过。
