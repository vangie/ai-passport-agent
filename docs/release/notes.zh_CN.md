**简体中文** · [English](notes.md)

# AI Passport Agent v0.6.0

## 本版重点

- **新名称，已有设备继续用。** AI Passport OpenClaw 更名为 **AI Passport Agent**，
  定位为个人 Agent 的随身语音终端。**本版仍仅支持 OpenClaw。**
- **减少把噪声当成一句话发送。** 提问和通话加入端侧人声检测。未检测到人声的提问不发送，
  已完成聊天不清空；检测到说话后，安静约两秒可自动提交。
- **尽量保住开头的音节。** 通话保留短时、有界预录，语音打断结合人声确认，仍可按键打断和静音。
  类人声噪声可能通过，轻声或过短的人声也可能漏检；这不是声纹认证。
- **继续以网页为主入口。** 在网页中配置网络、手机热点和 OpenClaw，需要时更新固件。
  网页没有应用后台，通过 USB／蓝牙直接写入；对话由用户配置的网关与服务处理。

## 安装与升级

从 <https://vangie.github.io/ai-passport-agent/> 和[上手步骤](../../README.zh_CN.md#开始使用)开始。
域名迁移期间，该固定 Pages 链接会跳转到当前生效的自定义域名。

兼容 USB 更新保持勾选「保留配置」。首次安装或完整替换会覆盖设备配置，先阅读
[数据策略](../development/engineering/firmware-layout.zh_CN.md#刷写与已存数据)。
设备身份、已存网络、配对、通信协议和 `ai-passport-openclaw` 插件 ID 保持不变，
不需要仅因更名重新配对。原生客户端仅保留源码，不随 Release 发布。

`ai-passport-openclaw-0.6.0.tgz` 安装在 OpenClaw 网关主机；固件压缩包为
`ai-passport-agent-firmware.tar.gz`。用 `SHA256SUMS.txt` 和 `release-manifest.json`
核对下载完整性与源码来源。

## 验证与边界

发布检查包含主机测试、全新固件构建、合并镜像与布局校验、浏览器界面和插件包检查。
维护者已反馈语音过滤固件在实机测试中有效，不代表所有麦克风、噪声、语音服务和网络均已验收。
通话仍需配置实时语音服务，模型与服务商费用另计。
