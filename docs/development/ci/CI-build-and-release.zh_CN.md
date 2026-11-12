<p align="right"><a href="CI-build-and-release.md">English</a> · <strong>简体中文</strong></p>

# CI 与发布

默认交付路径是固件、OpenClaw 插件和网页配置工具。原生 App 仅保留 `mac/`、`ios/`
和 `shared/` 中的源码作为可选方案。CI 不再构建、签名、运行模拟器 UI 测试或打包 App，
其营销版本也不再阻塞发布。本地开发脚本及原生源码单元测试保留。

## PR 与主分支

- `static-checks.yml`：仓库规则、私人地址检查、固定版本 Gitleaks、actionlint
  和 C／Python／Swift 主机测试；不构建 App，不安装 XcodeGen。
- `firmware-checks.yml`：ESP-IDF 5.5.3 构建和合并固件验证。
- `openclaw-plugin.yml`：Node 22／24／26，锁定依赖、插件测试和实际 npm 包检查。
- `web.yml`：协议／UI 测试、静态网站构建和 GitHub Pages 部署，校验已发布固件后同源提供。

任务无需私人网关、模型、Wi-Fi、隧道或 Apple 凭据。CI 成功不代表真机或在线模型验收成功。

## 标签发布

标签使用 `v<semver>`（如 `v0.5.0`）。打标签前检查双语日志、发布说明、隐私和硬件兼容性。
不得上传私人 NVS、配对材料、配置、本机日志或旧的本机构建固件。

`.github/workflows/release.yml` 从推送的标签构建：

1. 仓库、私人地址、密钥和工作流检查。标签、插件包／运行时及锁文件版本、双语说明和
   带日期日志必须一致；原生 App 版本独立。
2. 通过可复用工作流运行主机检查，不构建 App。
3. 全新构建固件并校验布局和哈希，从内容寻址的已验证归档打包；公开产物不含 ELF／MAP。
4. 按锁定依赖构建插件包。
5. 检查、主机测试、固件和插件全部通过后，只收集五个文件：插件 TGZ、固件 TAR.GZ、
   应用和合并 BIN、`LICENSE.txt`。缺失、空文件或不安全路径会中止。
   再生成记录源码提交、版本、大小和 SHA-256 的 `release-manifest.json` 与
   `SHA256SUMS.txt`，合计七个文件。
6. 创建或复用同标签草稿，上传后重新下载，核对文件全集、来源和哈希，全部通过才公开并设为最新版。
   失败保留草稿，重跑不会覆盖已公开版本。

不再要求或发布 macOS ZIP、iOS 源码包、模拟器 ZIP 或 IPA；原生代码可从仓库和 GitHub
自动生成的源码归档取得。历史标签工作流不改写。替代版本验证通过后可删除旧 Release，
保留其标签与 `web/release-builds.json` 中的已验证身份，确保旧设备仍可识别版本。
可选的 `mac-resources`、`ios-source` 只是本地工具，不再由 CI 调用。

对齐插件版本和双语说明／日志，以目标版本执行
`python3 tools/release.py check-version --tag v0.5.0`。完成静态门禁及相关固件／插件／网页测试，
合并检查过的源码，再在准确提交上推送不可变标签。必须观察 Release 完成，不能把标签或构建成功
当成发布完成。草稿失败先检查同标签草稿并重跑失败任务，不移动标签或另建版本重试。
回退应选旧版兼容产物，而非改写公开资产。

Action 固定到完整 SHA，默认只读，仅发布任务获得 `contents: write`。
手动触发执行校验／构建但不发布；独立 Web 工作流部署 Pages，不会修改 OpenClaw 安装。

## 本地原生开发

只有本地开发原生源码时才按需运行 `make sim-mac` 或 `make sim-iphone`。
CI 无需 Apple 签名身份，也不会从开发者电脑导入凭据。iPhone 安装需要 Xcode 和自己的签名团队，
见[iOS 安装说明](../../release/ios-install.zh_CN.md)。本地 App 构建不属于发行产物，
CI 也不再验证完整 App。

网页 USB 刷机无需本地 Python 或 esptool；命令行刷机仍需要这些工具。
本地应用刷写工具检查镜像和 ESP32-C3 后写入 `0x10000`，仅在分区兼容时保留配置。
合并镜像应写到 `0x0`，可能重置配置。

另见[发布说明](../../release/notes.zh_CN.md)、[固件布局](../engineering/firmware-layout.zh_CN.md)
和[项目 README](../../../README.zh_CN.md)。
