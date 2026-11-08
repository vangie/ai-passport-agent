<p align="right"><strong>简体中文</strong> · <a href="CI-build-and-release.md">English</a></p>

# CI 与 Release

本仓库只发布 AI Passport OpenClaw 一个产品，标签采用 `v<semver>`，如 `v0.3.0`；Release 标题包含产品名。插件和 App 的内部版本可以不同，插件版本以包清单为准。

## PR 与 main

- `static-checks.yml`：仓库规范、私人地址检查、固定版本 Gitleaks、actionlint、C／Python／Swift 主机测试，以及 macOS 未签名构建。
- `firmware-checks.yml`：ESP-IDF 5.5.3 构建并校验合并固件。
- `openclaw-plugin.yml`：Node 22／24／26，锁定依赖安装、插件测试和实际 npm 压缩包检查。

任务不访问私人电脑，不需要网关、模型、Wi-Fi、隧道或 Apple 凭据。CI 成功不代表真机或在线模型验收成功。

## 标签发布

打标签前检查双语变更记录、发布说明、源码／隐私检查与硬件兼容性。不要上传私人 NVS、配置、配对材料、本机日志或以前本地构建的固件。

`.github/workflows/release.yml` 从推送的标签构建：

1. 仓库、私人地址、秘密和工作流检查。
2. 通过可复用工作流运行完整主机检查。
3. 全新 ESP-IDF 构建、布局和哈希校验；从内容寻址的已验证归档打包固件，公开产物不包含 ELF／MAP。
4. 按锁定依赖构建插件包。
5. 构建 Intel／Apple Silicon 通用 Mac App，内置同一次发布校验过的固件和刷写工具，再对完整 App 做 ad-hoc 签名并验证。
6. 所有前置任务成功后，创建 GitHub Release，上传固件、插件、Mac ZIP 和 `SHA256SUMS.txt`。

所有 Action 固定到完整提交 SHA。默认只读，仅最后发布任务获得 `contents: write`。手动触发执行相同构建，但不会发布 Release。不包含远程部署，也不会自动修改任何人的 OpenClaw 安装。

## 签名与安装限制

默认 Mac 产物是 **ad-hoc 签名，未经 Apple 公证**。流水线不包含也不要求开发者证书。未来如增加公证渠道，应使用 GitHub 加密 Secrets 和临时钥匙串，不能把证书与签名密码放进源码。仅购买开发者账户不会自动公证已有产物。

刷机需要本机 Python 3 和 esptool，例如 `pipx install esptool==4.11.0`。内置工具先检查 SHA、ESP32-C3 身份及边界，再将应用写到 `0x10000`；只有分区布局兼容时保留数据。合并镜像应写到 `0x0`，可能重置配置，不是日常仅应用区更新的路径。

另见[发布说明](../../release/notes.zh_CN.md)、[固件布局](../engineering/firmware-layout.zh_CN.md)和[项目 README](../../../README.zh_CN.md)。
