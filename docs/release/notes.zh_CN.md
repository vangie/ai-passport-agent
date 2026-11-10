[English](notes.md) · **简体中文**

# AI Passport OpenClaw v0.4.0

## 本版亮点

- iPhone 蓝牙配置：渐进式 2.4 GHz 扫描、已存网络、个人热点提示及 OpenClaw 受限配对码。
- 改进语音控制、提问转写／聊天显示、联网待机及 Mac 统一配置／维护流程。
- 产品风格双语 README，含固件、Mac、iPhone 演示数据截图和实测 NFC 热点自动化技巧。
- iPhone 真机／模拟器构建与模拟器 UI 测试失败会阻止发布，CI 不需要 Apple 签名秘密。

## 下载文件

- `ai-passport-openclaw-macos-universal-adhoc.zip`：支持 macOS 14+、Apple Silicon
  和 Intel 的配置 App，内置匹配且经过校验的固件。使用临时签名，**不是**
  Developer ID 签名／Apple 公证，可能需要在“隐私与安全性”中明确允许。
  不要禁用 Gatekeeper。配置不需要 ESP-IDF；USB 刷机需要 Python 3 和
  esptool（`pipx install esptool==4.11.0`）。
- `ai-passport-openclaw-firmware.tar.gz`：ESP32-C3／8 MB 的引导程序、分区表、
  完整镜像、仅应用镜像与校验清单。
- `FoloToy-AI-Passport-full.bin`：从 `0x0` 写入，适合首次安装或主动完整刷新，
  可能清除配置；保留配置的升级不要写此文件。
- `FoloToy-AI-Passport.bin`：仅在分区布局兼容时从 `0x10000` 更新应用。
  Mac App 使用这条路径，保留 NVS、Card ID 和音频分区。
- `ai-passport-openclaw-*.tgz`：独立 OpenClaw 插件源码安装包。
- `ai-passport-openclaw-ios-source.zip`：iPhone 源码包，包含 `ios/`、共享 Swift 代码及
  双语 Xcode 签名安装说明，**需用自己的本地团队签名**，支持时可用 Personal Team；iOS 17+。
- `ai-passport-openclaw-ios-simulator.zip`：供兼容 Mac 架构／运行时使用的模拟器 App，
  **不能装到 iPhone，不能使用真实蓝牙**。
- `SHA256SUMS.txt`：Release 文件校验和。

本版 **没有已签名 iPhone IPA，不发布 TestFlight**。真机请使用源码包；模拟器 ZIP 只用于预览。
Mac、iPhone 和插件版本为 0.4.0，现有线协议与 OpenClaw 测试基线不变。

## 升级说明

请备份需要保留的设置，优先用 Mac App 的兼容仅应用区更新，不要日常升级就写合并镜像。
未发布原型的设备／App 身份不迁移，可能需要重新配网和配对。
不要用共享管理员网关 token 代替设备授权。

蓝牙待机默认开启，音频／网关工作期间暂时释放。熄屏后的第一次按键只唤醒。
固件、网关／插件与配置 App 需要兼容；连接检测通过不等于语音质量验收。

## 验证边界

CI 构建不能代替硬件和音质验收。OpenClaw 插件验证基线为 2026.10.5，
升级前请检查兼容性。安装、网络和插件使用方法见仓库说明。本发布不包含
部署地址、账号密钥、Wi-Fi 密码或设备授权。
