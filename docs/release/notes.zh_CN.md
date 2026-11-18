[English](notes.md) · **简体中文**

# AI Passport Agent v0.7.7

统一网关源码，恢复 TLS 1.3 · 2026-11-18

## 主要变化

- **控制回复渲染的栈用量。** 在 LVGL 锁内复用旋转按键位图，构建拒绝主循环和界面代码超过 768 字节的栈帧。
- **多设备共用一份网关实现。** OpenClaw 与 Hermes 适配器迁为固定的 Valet Gateway
  v0.1.0 submodule。插件 ID、`/passport/v1`、设备签名和配对凭据继续兼容。
- **实际构建恢复 TLS 1.3。** 保留 ESP-IDF 必需依赖，缺少 TLS 1.3 或客户端票据
  支持时构建直接报错，避免静默禁用。日志区分“提供票据”和真正接受会话复用。
- **提供完整安装源码。** 配置提示词使用共享正式版及审查过的准确提交；CI、doctor
  和打包读取同一维护源。新增完整源码包，包含 GitHub 自动源码包缺少的 submodule 文件。

## 安装或升级

电脑 Chrome／Edge 打开 <https://ai-passport-agent.codelife.me/>，USB 连接后选
「更新固件」，兼容升级保持「保留设置」开启。只更新应用区可保留网络、助手和设备身份。
全量替换可能清除数据，先阅读[数据策略](../development/engineering/firmware-layout.zh_CN.md#刷写与存储数据)。

网关适配器独立版本化，两套插件包仍为 0.7.5，由
[Valet Gateway v0.1.0](https://github.com/vangie/valet-gateway/releases/tag/v0.1.0) 提供。
现有兼容插件不必替换或重新配对。确需替换时保留设备授权，通过正常生命周期操作。
更新固件不会自动替换或重启网关。

按 `SHA256SUMS.txt` 和 `release-manifest.json` 验证下载文件；清单也记录共享源码
提交与插件版本。离线源码使用 `ai-passport-agent-source.tar.gz`，或运行
`git submodule update --init --recursive` 初始化 `integrations/valet-gateway`。
macOS／iPhone 原生 App 仍仅提供源码。

## 验证与边界

维护者工牌保留了 3 套 Wi-Fi 和 3 套助手，通过 Shadowsocks 协商到 TLS 1.3，
三次热连接探测和三次录音启停均无分配失败。提问音频已送达原 OpenClaw，并成功
取回最终回复；该轮回复因上游模型 502 重试变慢，与设备传输独立。20 秒合成 Live
测试完成，无内存分配失败。这些结果不等于音质或长期网络稳定性保证。本次样本中
提供 TLS 票据没有显示可测的加速；复用现有 WebSocket 则省去了新握手。

OpenClaw 插件／relay／IMA、浏览器和原生配置端、源码／插件包完整性测试通过；
真实 Hermes v0.23.1 运行时的 31 项隔离回归通过。本版未验证真机重新配对和
E-Ink4 实物，未替换或重启已有网关。

准确测试与产物记录见[发布就绪记录](readiness.zh_CN.md)。
