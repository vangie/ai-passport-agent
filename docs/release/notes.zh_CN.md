[English](notes.md) · **简体中文**

# Release 文件说明

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
- `SHA256SUMS.txt`：Release 文件校验和。

CI 构建不能代替硬件和音质验收。OpenClaw 插件验证基线为 2026.10.5，
升级前请检查兼容性。安装、网络和插件使用方法见仓库说明。本发布不包含
部署地址、账号密钥、Wi-Fi 密码或设备授权。
