<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# AI Passport OpenClaw

把 **FoloToy AI Passport** 变成随身 OpenClaw 语音终端：用 Mac 配置，之后直接通过 Wi-Fi 工作，不需要一直连接手机。面向 ESP32-C3、8 MB Flash、240 × 240 屏幕、麦克风、扬声器和三个按键。

这是基于 [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport) 的独立社区项目，不是 FoloToy 官方固件，也不是 OpenClaw 发行版。

## 项目组成

| 组件 | 源码 | 用途 |
| --- | --- | --- |
| 固件 | `app/`、`main/`、`components/bsp/` | 中英文界面、通话／提问、设备保存配置、Wi-Fi、音频与传输 |
| Mac App | `mac/`、`shared/` | USB 状态、Wi-Fi 和助手配置、配对、固件检查与仅应用区刷写 |
| OpenClaw 插件 | [`tools/openclaw/`](tools/openclaw/README.zh_CN.md) | 可独立升级的原生 Talk 集成、带认证的 `/passport/v1` WebSocket 传输，不修改 OpenClaw 安装文件 |
| 网络指南 | [联网与内网穿透](docs/networking.zh_CN.md) | 局域网、Cloudflare Tunnel、SakuraFrp、TLS、认证与排障 |
| 开发工具 | `tools/`、`tests/`、`sim/` | 主机测试、固件校验、打包和模拟器启动器 |

通话通过 OpenClaw 使用实时语音模型；提问将录音提交给网关的 `badge` 助手，使用前请在 OpenClaw 创建这个专用 Agent 并配置模型和工具权限。多个助手配置目前选择的是不同网关连接，尚不能改变提问使用的 Agent ID。模型密钥留在网关上。配置保存在 Passport；Mac 也会通过系统存储记住常用表单值和已保存的 Wi-Fi 密码。不要公开配置导出、配对码、日志和设备凭据。

## 从 Release 开始

从 [Releases](https://github.com/vangie/ai-passport-openclaw/releases) 下载：

- `ai-passport-openclaw-macos-universal-adhoc.zip`：macOS 14+，兼容 Apple Silicon 与 Intel，内置匹配固件。目前为 **ad-hoc 签名，未经 Developer ID 签名及 Apple 公证**；首次运行可能需要本机安全设置明确批准，不分发 Apple 签名凭据。
- `ai-passport-openclaw-firmware.tar.gz`：合并／应用固件、校验清单、引导程序、分区表和刷写参数。下载后核对 `SHA256SUMS.txt`。
- `ai-passport-openclaw-<插件版本>.tgz`：安装到 OpenClaw 所在电脑，见[插件说明](tools/openclaw/README.zh_CN.md)。

1. 在网关安装插件。**已验证基线为 OpenClaw 2026.10.5**，不承诺未来所有 SDK 版本直接兼容。在网关配置模型凭据，通话需要支持实时语音的提供方。
2. 使用已配置好的局域网地址或公网 WSS 地址访问网关。暴露服务前先读[网络指南](docs/networking.zh_CN.md)。
3. 打开 Mac App，用数据线连接 Passport。刷机需要 Mac 上有 Python 3 和 `esptool`，例如通过 `pipx install esptool==4.11.0` 安装；App 不会悄悄安装系统工具。
4. 配置 Wi-Fi、选择助手，使用 OpenClaw 的限权配对码绑定。App 的复制提示词流程可以帮助你向自己的助手获取配对信息。
5. 写入配置。日常仅应用区更新写入 `0x10000`，**只有分区布局兼容时**才保留配置。空白设备先将合并固件写入 `0x0`；这可能重置已有配置。

仓库不内置私人网关地址、隧道账户、Wi-Fi 密码或 API 密钥。设备通过 OpenClaw 原生设备配对授权，不使用管理员网关令牌。持有已配对设备就拥有该设备的权限，丢失后应在 OpenClaw 撤销授权。

## 构建与测试

固件使用 **ESP-IDF 5.5.3**，激活本机环境后运行：

```sh
./tools/validate.sh --firmware
python3 tools/release.py firmware build/release/firmware
```

校验流程从仓库默认配置构建，验证分区和合并镜像，并在本机保留匹配的调试产物；公开 Release 不包含 ELF／MAP。详见[构建测试指南](docs/development/engineering/build-and-test.zh_CN.md)。本应用保留 `cardid` 和 `audio` 分区，从其他固件迁移前检查 `partitions.csv`。

macOS 主机测试需要 Xcode 命令行工具、Node.js 22+、Python 3、`mbedtls@3`、`pkg-config` 和 `xcodegen`。把 `PKG_CONFIG_PATH` 指向 mbedTLS 的 pkg-config 目录，把 `CJSON_SOURCE_DIR` 指向 ESP-IDF 的 `components/json/cJSON` 或兼容 cJSON 源码。CI 中有固定版本的安装示例。

```sh
./tools/validate.sh --static
make plugin-test
make plugin-package
python3 tools/check_public.py
bash tools/check-secrets.sh
```

使用 `tools/mac.sh` 本机构建 Mac App，可在本地环境设置 `DEVELOPMENT_TEAM`。装好对应模拟器依赖后，使用 `make sim-passport`／`make sim-iphone`。模拟运行不代表蓝牙和真机验收通过。`ios/` 也提供 iOS 配置端，但目前不作为已签名的 Release 产物分发。

## CI 与发布

PR 和 main 推送运行仓库／隐私检查、C／Python／Swift 主机测试、Mac 构建、ESP-IDF 固件构建，以及 Node 22／24／26 插件矩阵。版本标签构建全部发行产物；主机测试、固件、插件和通用 Mac App 全部成功后才发布。手动触发 Release 只生成产物，不创建 GitHub Release。见 [CI 与发布](docs/development/ci/CI-build-and-release.zh_CN.md)。

所有 Action 固定到提交 SHA，只有发布任务获得仓库写权限。源码扫描只能预防，不能证明未来任意修改都没有秘密；发布前仍应检查差异和生成产物。不要上传本机 NVS 转储和私人配置。

## 许可与致谢

采用 [MIT](LICENSE)，保留 FoloToy 署名。硬件／BSP 和开发文档源自上游项目；字体和其他第三方素材保留各自许可，见[素材说明](assets/README.zh_CN.md)。敏感问题请按[安全政策](.github/SECURITY.zh_CN.md)私下报告。
