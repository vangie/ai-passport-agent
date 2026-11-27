<p align="right"><strong>简体中文</strong> · <a href="community.md">English</a></p>

# 社区发布材料 · v0.8.1

为玩法 **2236** 准备的 v0.8.1 社区发布文案与海报素材说明（上一版本 v0.7.5 提交修订为 **5782**）。

## 标题

随身搭档

## 简介

随身搭档是连接你自己的个人 Agent 的语音入口，支持 Muse、OpenClaw 与 Hermes。使用「提问」处理问题和任务：查看自己的转写，阅读逐步出现的回复，并在同一段对话中继续追问；在 OpenClaw 或 Hermes 上还可使用「通话」进行连续语音交流，通过字幕和屏幕按键静音、打断或结束。

它连接的是你自己的 Agent 或 Muse 账号，而不是固定演示聊天机器人。「提问」可调用 OpenClaw 与 Hermes 已授权的工具和上下文，或通过官方 Gadget SDK 直连 Muse 获取文字回复；「通话」由实时语音模型处理日常交流，需要任务、实时信息或私人上下文时再咨询对应网关 Agent。

在网页中完成配置。电脑使用 Chrome／Edge 和 USB 安装或更新固件；需要移动配置时，可用支持网页蓝牙的浏览器（iPhone 使用 Bluefy）。保存家庭 Wi-Fi、办公室 Wi-Fi 和手机热点后，可在工牌上随时切换已保存的网络与助手，每个助手可独立配置直连或 Shadowsocks 代理。配置网页没有应用后台，也不转发对话。

准备好 Muse 账号（个人 Gadget SDK token 与 Muse App）或自己的 OpenClaw／Hermes 后，设备就能成为一个轻量的随身语音入口：提问时看文字回复，通话时听语音应答。项目以开源方式提供，模型和语音服务由用户自己的账号或网关配置。

## 使用方法

1. 准备可用的 Muse 账号或 OpenClaw／Hermes。电脑 Chrome／Edge 打开 https://ai-passport-agent.codelife.me/，USB 连接并更新固件；首次安装取消「保留配置」，兼容升级保留它。
2. 在「新增助手」中选择 Muse，粘贴个人 Gadget SDK token 并点击「保存配置」，再在工牌「设置 → 助手 → Muse 配对」配合 Muse App 完成绑定；或选择 OpenClaw／Hermes，把对应提示词交给网关主机上的 Agent，回填地址和配对码。扫描 2.4 GHz Wi-Fi，填写密码并保存，需要代理时在助手「连接设置」中配置直连或 Shadowsocks。
3. 「提问」按键开始录音，再按一次或停顿后发送，回复后可继续追问。「通话」（OpenClaw／Hermes）等待显示「正在听」后说话，可按屏幕提示静音、打断或结束。设备闲置 1 分钟息屏、30 分钟自动关机，按任意功能键唤醒或开机；两个时间可在设备「设置」里的「息屏」「关机」调整。
4. 在「设置 → 助手」切换已存助手，在「设置 → Wi-Fi」切换已存网络。新增网络使用网页 USB／蓝牙配置；先在设备设置中开启蓝牙。iPhone 使用 Bluefy；配置个人热点时开启「允许其他人加入」和「最大兼容性」，找不到热点时先进入系统个人热点页面再回到网页扫描。

## 更新日志

- 新增 Muse 支持：设备保存 SDK token、配合 Muse App 蓝牙配对、状态栏专属 22 × 22 图标，以及语音提问与文字回复。
- 新增助手独立直连／Shadowsocks 代理线路；Muse 未单独配置线路时自动继承设备上已有的 Shadowsocks 代理。
- 优化 ESP32-C3 上的 Muse 语音问答与录音质量：支持 14 KB Noise 单帧原地解析与多段回复聚合，录音增量擦除改为单扇区配合 100 ms I2S DMA 缓冲，并在上传前执行 80 Hz 高通滤波与人声增益归一化。

## 文件与来源

- 源码：https://github.com/vangie/ai-passport-agent
- 版本：v0.8.1。只使用 GitHub 正式发布并核验过的 `agent-talk-full.bin`。
- Release：https://github.com/vangie/ai-passport-agent/releases/tag/v0.8.1
- 使用对应 `release-manifest.json` 中的完整固件哈希，并上传同一份文件，不使用本地候选镜像。
- 保留中英文名称，不修改其他玩法。
- 使用 `node tools/community_materials.mjs` 生成材料，输出目录为 `build/community-market/v0.8.1/materials/`。
- 上传前逐张检查最终封面和附图，演示数据必须是虚构的。
