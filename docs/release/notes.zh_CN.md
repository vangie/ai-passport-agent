[English](notes.md) · **简体中文**

# AI Passport Agent v0.8.0

新增 Muse 直连和助手独立代理线路 · 2026-11-27

## 主要变化

- **新增 Muse 直连。** 工牌集成官方 Muse Gadget SDK 的 Link 与 Noise 通信，在设备上
  保存 SDK token 和配对状态，支持语音提问与文字回复。
- **保留现有网关兼容性。** OpenClaw、Hermes 的配对、设备凭据和现有网关协议继续兼容，
  已有助手无需重新绑定。
- **新增助手独立线路。** 每个助手可单独选择直连或 Shadowsocks，Muse 也能使用代理，
  不影响其他助手。
- **新增网页配置入口。** 配置台提供 Muse token 写入、配对说明和线路编辑，token 只写入设备。

## 安装或升级

电脑用 Chrome 或 Edge 打开 <https://ai-passport-agent.codelife.me/>，USB 连接工牌，
保持「保留配置」并选择「更新固件」。应用区更新会保留已存网络、助手和设备身份。
然后在「新增助手」选择「Muse 直连」，通过 USB 写入个人 Gadget SDK token，
再在工牌「设置 → Muse 配对」中用 Muse App 完成绑定。网络需要代理时，
在 Muse 助手的「连接设置」中单独选择直连或 Shadowsocks。

现有网关插件保持独立版本，不需要替换或重新配对。更新固件不会自动替换或重启网关。

## 验证与限制

固件构建、合并镜像布局、主机测试、网页测试和 Muse 协议测试通过。真实 Muse 账号配对
和第一条回答仍需使用真实 Muse 账号与 App 验证。设备没有 PSRAM，应用分区接近上限，
后续新增功能需要先检查体积。
