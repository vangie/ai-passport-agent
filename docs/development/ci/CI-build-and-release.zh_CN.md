<p align="right"><strong>简体中文</strong> · <a href="CI-build-and-release.md">English</a></p>

# CI 与 Release

本仓库只发布 AI Passport OpenClaw 一个产品，标签采用 `v<semver>`，如 `v0.4.2`；Release 标题包含产品名。
当前 macOS App、iOS App 与插件的营销版本对齐；请检查包清单及 App 版本，不要把它等同于固件协议版本变化。

## PR 与 main

- `static-checks.yml`：仓库规范、私人地址检查、固定版本 Gitleaks、actionlint、C／Python／Swift 主机测试，以及 macOS 未签名构建。
- `ios.yml`：iOS App 未签名真机／模拟器构建、使用虚构数据的布局／配网 UI 测试、白名单源码包与模拟器 ZIP。
  自动选择已安装的 iPhone 运行时，不写死机型。显式使用 `macos-15-intel` 运行器，
  避开共享 ARM 运行器的容量排队。相关 PR／main 变更触发，Release 复用。
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
5. 构建 Intel／Apple Silicon 通用 macOS App，内置同一次发布校验过的固件和刷写工具，再对完整 App 做 ad-hoc 签名并验证。
6. 复用 iOS App 工作流：无需签名凭据构建真机／模拟器，使用虚构状态运行 Debug UI 测试，
   打包 Release 模拟器 App 与 iOS App 源码包，不发布未签名 IPA。
7. 所有前置任务成功后收集指定产物，生成源码版本／哈希清单与校验和。
8. 创建或复用同标签草稿，上传后下载核验，全部通过才公开为最新版。

`tools/release.py ios-source` 只打包已跟踪的 iOS App 源码／素材、UI 测试、
共享 Swift 代码、双语语言表、权限说明、XcodeGen 配置、MIT 许可证和双语安装说明。
不含生成的 Xcode 工程、签名文件、本地设置、构建目录或仓库其他文件。
提交前仍需检查暂存源码，白名单不能替代密钥扫描。

所有 Action 固定到完整提交 SHA。默认只读，仅最后发布任务获得 `contents: write`。手动触发执行相同构建，但不会发布 Release。不包含远程部署，也不会自动修改任何人的 OpenClaw 安装。

发布身份与产物新增强制门禁：稳定版标签、App／插件运行时与锁文件版本、双语发布说明和带日期日志必须一致。
全部构建通过后，只收集八个指定分发文件，缺失、空文件或不安全路径会中止；
再生成记录源码提交、版本、大小和 SHA-256 的 `release-manifest.json`，
以及覆盖前九个文件的 `SHA256SUMS.txt`，共十个文件。
上传先创建或复用同标签草稿，上传后重新下载并核对文件全集、来源和哈希，全部通过才公开并设为最新版。
中途失败保留草稿；重跑不会覆盖已公开版本。

准备版本时对齐所有版本字段和双语说明／日志，以目标版本执行
`python3 tools/release.py check-version --tag v0.4.2`，完成静态与 App 测试，
合并检查过的源码，再在准确提交上推送不可变版本标签。必须观察 Release 工作流完成，
不能把推送标签或构建成功当成发布完成。草稿上传失败先检查同标签草稿，再重跑失败任务；
不要为了重试移动已公开标签、另建版本或覆盖公开文件。回退选用旧版兼容产物，
而非悄悄改写旧资产；历史标签仍使用它原来的工作流。

## 签名与安装限制

默认 macOS App 产物是 **ad-hoc 签名，未经 Apple 公证**。流水线不包含也不要求开发者证书。未来如增加公证渠道，应使用 GitHub 加密 Secrets 和临时钥匙串，不能把证书与签名密码放进源码。仅购买开发者账户不会自动公证已有产物。

macOS App 已预编译，下载后不需要重新构建或再做临时签名；本地重签不能代替 Apple 公证。

iOS App 源码包需接收者在本地 Xcode 使用自己的团队签名，支持时可使用免费 Personal Team。
此流程不发布 App Store、TestFlight 或可下载的已签名真机 App。
未签名 `iphoneos` 构建只验证能编译，不上传产物。
只有兼容的真机二进制具备有效证书、描述文件与匹配权限，并配合安装工具，才可能免构建重签安装。
模拟器 App 不能重签成真机 App，本版不提供真机重签套件。
模拟器 ZIP 针对构建机架构／SDK，无法连接真实蓝牙，其他环境可能需要从源码构建。
见 [iOS App 安装说明](../../release/ios-install.zh_CN.md)。

如果以后要签名分发 iOS，需要另行确定分发方式、Apple 团队、Bundle 注册、
证书／描述文件和受保护的 CI secrets，不会自动把开发者本机签名身份导入 GitHub。

刷机需要本机 Python 3 和 esptool，例如 `pipx install esptool==4.11.0`。内置工具先检查 SHA、ESP32-C3 身份及边界，再将应用写到 `0x10000`；只有分区布局兼容时保留数据。合并镜像应写到 `0x0`，可能重置配置，不是日常仅应用区更新的路径。

另见[发布说明](../../release/notes.zh_CN.md)、[固件布局](../engineering/firmware-layout.zh_CN.md)和[项目 README](../../../README.zh_CN.md)。
