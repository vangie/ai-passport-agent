<p align="right"><a href="README.md">English</a> · <strong>简体中文</strong></p>

<p align="center"><img src="assets/logo/logo.png" alt="AI Passport OpenClaw 应用图标" width="104" /></p>

<h1 align="center">AI Passport OpenClaw</h1>

<p align="center"><strong>OpenClaw 的轻量随身语音入口</strong></p>
<p align="center">为 FoloToy AI Passport 打造的随身语音终端。<br />语音问答，实时通话，直连你自己的助手。</p>

<p align="center">
  <a href="https://github.com/vangie/ai-passport-openclaw/releases">下载</a> ·
  <a href="#开始使用">开始使用</a> ·
  <a href="#碰一下开启热点">NFC 热点技巧</a> ·
  <a href="docs/configuration.zh_CN.md">配置指南</a>
</p>

![Passport 固件：首页、提问对话、实时通话](assets/images/screenshots/firmware.png)

这是基于 [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport) 的独立社区项目，
不是 FoloToy 官方固件，也不是 OpenClaw 发行版。刷入会替换原有应用，
不承诺保留官方固件的原有功能。

## 语音问答与实时通话

| | 提问 | 通话 |
| --- | --- | --- |
| 适合 | 问一个问题、交代一件事、看文字答复 | 连续来回聊，不必每句都按键 |
| 操作 | 按一下开始录音；再按一下发送，说完停顿也可自动提交 | 连上后直接说，停顿后等待回复 |
| 反馈 | 录音波形、自己的转写、助手流式文字回复 | 麦克风波形、语音回复、自动滚动字幕 |
| 继续 | 接着问，保留最近几轮，长内容可翻页 | 按屏幕旁的三个键静音、打断或结束 |

首页只有 **设置／通话／提问**。每个界面的操作提示都贴近实体按键，随状态变化，
不必背长按、短按组合。支持中英文界面、电量与连接图标，以及扬声器音量调整。

提问接入 OpenClaw 专属的 `passport` agent；通话使用网关配置的实时语音服务。
模型、工具能力和响应速度取决于你的网关、服务商及网络，并非仅由设备决定。

## 两种配置工具，按需选择

**macOS App 和 iOS App 都是配置工具，不中转语音；iOS App 并非必需。**

两个 App 自动跟随系统使用英文或简体中文，包含接入提示词与权限说明；
Passport 的屏幕语言独立设置，不受 App 语言影响。

| 工具 | 什么时候用 | 配好后是否需要继续运行 |
| --- | --- | --- |
| macOS App | 通过 USB 配置 Wi-Fi、助手、语言与音量，检查连接及更新固件；也能提前配置 iPhone 热点 | 不需要，可拔掉 USB |
| iOS App（可选） | 外出没有电脑时，通过蓝牙添加／更换 Wi-Fi 或个人热点、修改助手配置 | 不需要保持 App 或蓝牙连接；若使用手机热点，热点仍需开启 |

例如，出门前用 macOS App 配好家中 Wi-Fi 和 iPhone 热点，路上打开热点，再在 Passport
的「设置 → 网络」选择已存热点，就能继续使用，**不必安装 iOS App**。
当前固件保存新网络前会试连，因此首次配置热点时，手机需在附近且热点已开启；
不是只填入一个暂时不可用的热点名称就能保存。具体见[提前配置热点](docs/configuration.zh_CN.md#不安装-ios-app也能使用手机热点)。

### macOS App：配置与刷机，一页完成

![macOS App 演示：Wi-Fi、助手配对和设备偏好](assets/images/screenshots/macos.png)

- **统一保存。** Wi-Fi、助手、语言和音量改好后一起保存；撤销可恢复设备上次状态，
  写入失败也不会清空输入。
- **连接看得见。** 底栏显示 USB、设备 Wi-Fi、助手及电量；连接检测走设备的真实网络。
- **需要时才更新固件。** 在「设备维护」中通过 USB 更新兼容的应用固件。
  保存配置和刷固件是两件事，不会因改了设置就重新刷机。
- **少输几次密码。** 在本机记住 Wi-Fi 密码，也能逐项忘记。
  详见[本地存储说明](docs/configuration.zh_CN.md#配置保存在哪里)。

### iOS App（可选）：蓝牙配置，外出也能改

<p align="center"><img src="assets/images/screenshots/ios.png" alt="iOS App 演示：设备连接、Wi-Fi、助手和语言设置" width="320" /></p>

- 用 **蓝牙** 连接 Passport，不需要手机反过来连接临时配网热点。
- 由 **Passport 扫描** 附近的 2.4 GHz 网络；兼容固件会边扫描边显示，
  能识别的 iPhone 热点用链环图标标记。
- 复用已存网络、添加个人热点、扫描配对信息，把修改保存到设备。
  配好后可以退出 App、断开蓝牙。若 Passport 使用这台手机的热点上网，热点仍须开启。

以上截图均来自真实固件渲染器或原生 App，使用 **演示数据**：虚构的网络、
助手名称和 `.example` 地址。它们是界面预览，不是实机照片，也不代表硬件连接验收。

## 直连你的助手，不把手机当中继

```text
macOS App ── USB ─────┐
                   ├── 配置 ── AI Passport
iOS App ── 蓝牙 ─┘                │
                                  Wi-Fi／个人热点
                                     │
                              你的 OpenClaw 网关
                              + AI Passport 插件
                                     │
                                  模型与工具
```

Passport 通过 Wi-Fi **直接与网关对话**。macOS App 和 iOS App 只负责配置，不转发语音。
可以使用局域网地址，或者已经打通并有鉴权的公网 WSS 入口。
项目不内置任何共享托管网关。

连接手机热点时，手机仅提供上网通道；**语音不会经过配置 App**，
但手机需要留在附近并保持热点可用。

设备最多保存 **8 套 Wi-Fi** 和 **4 套助手连接配置**，在设置里就能切换，无需重新烧录。
助手配置选择不同网关连接；提问当前仍使用固定的 `passport` agent ID。

## 外出也能连接家里的 OpenClaw

OpenClaw 在内网时，同一局域网可直接连接；换到手机热点或其他网络后，
需要一个从该网络可达的入口。仅把热点配置给 Passport，不会自动打通家中的网关。

[网络接入与安全指南](docs/networking.zh_CN.md)介绍 Cloudflare Tunnel、SakuraFrp
及 TLS、鉴权与受限配对。基本原则是：**网关保持本机监听，公网使用 WSS，
保留设备认证，不把管理员 token 写进设备**。隧道解决可达性，不自动赋予访问控制；
还应限制专属 `passport` agent 的工具权限，设备遗失后撤销授权。

## 开始使用

准备好 FoloToy AI Passport、可传数据的 USB 线、2.4 GHz Wi-Fi 或兼容的手机热点，
以及已运行、已有可用模型的 OpenClaw 网关。首次刷机需要电脑；macOS App 支持 macOS 14+，
iOS App 可选，需自行签名安装。当前 OpenClaw 测试基线为 **2026.10.5**。

**不必先手动安装插件、创建 `passport` 助手或打通公网。** App 的提示词会让能操作
网关主机的 agent 检查并补齐。建立公网入口仍需隧道服务授权、可用域名／证书及配额，
已有则复用，缺少时按提示完成必要操作。通话另需实时语音服务，未配置时可先使用提问；
模型密钥留在网关。

1. **安装固件，连接设备。** 从 [Releases](https://github.com/vangie/ai-passport-openclaw/releases)
   下载固件和 macOS App，通过 USB 连接。空白设备按 Release 说明安装合并镜像；
   兼容固件可在 App「设备维护」中更新。先阅读
   [刷机与数据策略](docs/development/engineering/firmware-layout.zh_CN.md#烧录与已存数据)，
   保存需要保留的配置；完整合并镜像可能重置配置。
2. **让 OpenClaw 准备接入。** App 的 OpenClaw 区域点「复制提示词」，发给能操作
   网关主机的 agent。它会补齐插件、专属助手与网络入口，验证后返回二维码、地址和受限配对码。
   网关可以在另一台电脑上，不需要与配置 App 同机。
3. **选择设备网络。** macOS App 用 USB 连接；可选 iOS App 在设备「设置 → 蓝牙」开启后
   连接 `AI Passport XXXX`。搜索／填写 Wi-Fi 和密码。手机热点也能提前用 macOS App 配置，
   首次保存时须开启且可连接；iPhone 打开「允许其他人加入」「最大兼容性」，
   搜不到时停留系统「个人热点」页，再重新扫描。
4. **填回配对信息并保存。** 粘贴地址和配对码，或在 iOS App 扫码，点「保存配置」。
   出现 Wi-Fi 图标后即可测试提问；macOS App 也可运行设备连接检测。失败则修正后重试，
   不必重新刷机；配对码失效时再请 agent 生成。配好后可退出 App，热点上网时保持热点开启。
5. **开始问答或通话。** 按「提问」录音，再按「发送」或停顿自动提交；回复可翻页、继续追问。
   按「通话」后等待「正在听」，按提示静音、打断或结束。换网在设备「设置 → 网络」
   选已存网络，或用 App 添加。

详细步骤与边界见[agent 辅助接入](docs/configuration.zh_CN.md#让-openclaw-完成网关接入准备)。
只在局域网使用时不必建立公网入口，可按[插件指南](tools/openclaw/README.zh_CN.md)与
[网络接入指南](docs/networking.zh_CN.md)手动配置。保存配置与刷固件是独立操作。

### Release 里有什么？

| 文件 | 用途 |
| --- | --- |
| `ai-passport-openclaw-macos-universal-adhoc.zip` | macOS 14+，兼容 Apple Silicon 与 Intel，内含匹配固件 |
| `ai-passport-openclaw-firmware.tar.gz` | 应用／合并镜像、清单、引导程序、分区表和烧录参数 |
| `ai-passport-openclaw-<plugin-version>.tgz` | 安装在网关上的插件 |
| `ai-passport-openclaw-ios-source.zip` | iOS App 源码安装包，含共享代码与签名说明，需用自己的 Xcode 团队构建 |
| `ai-passport-openclaw-ios-simulator.zip` | 仅供 macOS App 模拟器预览，不能安装到 iPhone，也没有真实蓝牙 |

请核对发行包的 `SHA256SUMS.txt`。macOS App 当前为 **临时签名，尚无 Developer ID 签名或公证**，
macOS 可能要求手动安全确认。USB 刷机需要 Python 3 与 `esptool`
（例如 `pipx install esptool==4.11.0`），App 不会静默安装系统工具。
当前 **不提供已签名的 iOS App 下载**，不在 App Store 或 TestFlight 分发。
请下载源码包，按[安装说明](docs/release/ios-install.zh_CN.md)使用自己的 Xcode 团队构建／签名；
也可用模拟器 ZIP 在 Mac 上预览。

## 碰一下，开启热点

Passport 的无源 NFC 标签可以触发 **iPhone 快捷指令自动化**，无需安装本项目 iOS App：

1. 先用 macOS App（USB）或 iOS App（蓝牙）把 iPhone 热点配置给 Passport。在系统设置中开启
   **「允许其他人加入」** 和 **「最大兼容性」**。
2. 打开 **快捷指令 → 自动化 → NFC**，扫描 Passport，系统支持时选择 **「立即运行」**。
3. 添加 **「设定个人热点 → 打开」**，也可接上 **「打开 App → AI Passport」**。
   建议设为打开而不是切换，避免多碰一次反而关掉热点。

**总是弹出原有网址？** 原厂 NFC 可能存有 URL 记录。
用 NFC 写入工具备份内容，再把 URL **替换为普通文本记录**，例如 `AI Passport`，
不要只在原网址后追加文字。维护者的 iPhone 已实测：这样移除了网页提示，热点自动化可以正常工作。
不要改写 UID、锁定位或配置区。NFC 标签独立于固件，USB 刷机不会重写它。

这是可选的 iOS 自动化，不是设备认证，也不保证所有 iOS 版本都能静默执行。
打开 App 不等于自动完成蓝牙配对或下发配置。不要把密码和配对凭据写入标签。
详见 [NFC 操作与排障](docs/configuration.zh_CN.md#nfc-与个人热点)。

## 隐私与使用边界

- **干净固件不含私人地址和密钥。** 网关、Wi-Fi 及设备凭据来自你的配置，
  模型密钥留在 OpenClaw。截图直接使用虚构数据，不使用私人配置截图再遮盖。
- **配对设备，不分发管理员 token。** 已配对的 Passport 拥有所配置的权限。
  遗失时请在 OpenClaw 撤销授权，并合理限制 agent 工具权限。
- **蓝牙不是所有权认证。** 当前 Just Works 配对加密传输，但不防中间人攻击，
  也不验证持有者身份。在不可信环境且不需配置时，可从设备设置关闭蓝牙。
- **硬件有边界。** ESP32-C3、8 MB Flash、无 PSRAM、240 × 320 屏幕、2.4 GHz Wi-Fi。
  音频与网关工作期间暂时释放蓝牙，结束后恢复。界面模拟不等于射频共存或音质验收。

## 开发与参与

| 组成 | 源码／指南 |
| --- | --- |
| 固件与板级支持 | `app/`、`main/`、`components/bsp/` · [构建与测试](docs/development/engineering/build-and-test.zh_CN.md) |
| macOS App 与 iOS App | `mac/`、`ios/`、`shared/` · [配置指南](docs/configuration.zh_CN.md) |
| 可升级 OpenClaw 插件 | [`tools/openclaw/`](tools/openclaw/README.zh_CN.md) |
| 网络接入 | [局域网、隧道、代理与排障](docs/networking.zh_CN.md) |
| 项目文档 | [文档索引](docs/README.zh_CN.md) · [贡献指南](.github/CONTRIBUTING.zh_CN.md) |

安装[所需工具](docs/development/engineering/environment-setup.zh_CN.md)后：

```sh
make preview-ui       # 本地渲染实际 LVGL 界面
make sim-passport     # 需先安装 PassportSim 及其依赖
make sim-mac          # 构建并启动 macOS App
make sim-iphone       # 构建并启动 iPhone 模拟器，不提供真实蓝牙
make plugin-test
make plugin-package
./tools/validate.sh   # 完整检查，需先激活 ESP-IDF 5.5.3
```

CI 覆盖公开源码／密钥检查、主机测试、macOS App 构建、ESP-IDF 固件、
iOS App 真机／模拟器构建与 UI 测试，以及 Node 22／24／26 插件矩阵。
版本标签需通过全部必要任务才发布；手动 Release 仅生成产物，不创建 GitHub Release。
见 [CI 与发布](docs/development/ci/CI-build-and-release.zh_CN.md)。

## 许可与致谢

采用 [MIT](LICENSE)，保留 FoloToy 署名。硬件／BSP 与开发文档源于上游项目；
字体及第三方素材保留各自声明，见[素材来源](assets/README.zh_CN.md)。
敏感问题请按[安全政策](.github/SECURITY.zh_CN.md)私下报告。
