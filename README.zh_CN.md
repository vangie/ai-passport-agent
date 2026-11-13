<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

<p align="center"><img src="assets/logo/logo.png" alt="AI Passport Agent" width="104" /></p>
<h1 align="center">AI Passport Agent</h1>
<p align="center"><strong>个人 Agent 的随身语音终端</strong><br />语音问答与实时通话，当前支持 OpenClaw</p>
<p align="center">
  <a href="https://ai-passport-agent.codelife.me/">打开网页配置</a> ·
  <a href="#开始使用">开始使用</a> ·
  <a href="https://github.com/vangie/ai-passport-agent/releases/latest">最新版本</a> ·
  <a href="README.md">English</a>
</p>

![首页、语音问答与实时通话界面](assets/images/screenshots/firmware.png)

把 FoloToy AI Passport 变成个人 Agent 的随身语音终端。提问、交办任务、阅读回复，
或直接进行语音对话。设备通过 Wi-Fi 连接网关，手机不充当语音中继。

**当前支持 OpenClaw，尚未提供其他 Agent 接入。** 项目原名为 `ai-passport-openclaw`，
更名不影响已有设备和 OpenClaw 插件；配置网页已迁移至 <https://ai-passport-agent.codelife.me/>。
原地址 `ai-passport-openclaw.codelife.me` 保留并自动跳转，无需重新刷机。
参见[命名与兼容性](docs/configuration.zh_CN.md#项目名称与兼容性)。

这是基于 [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport) 的独立社区项目，
不是 FoloToy 官方固件或 OpenClaw 发行版。安装后会替换原厂应用，不承诺保留原有全部功能。

## 语音问答与实时通话

| | 提问 | 通话 |
| --- | --- | --- |
| 适合 | 问问题、交办任务、阅读回答 | 连续的语音交流 |
| 开始 | 按一下录音，再按发送；说完停顿也可自动提交 | 连接后等待「正在听」，直接说话 |
| 反馈 | 录音波形、你的转写与逐步显示的回复 | 波形、语音应答与滚动字幕 |
| 继续或退出 | 接着追问、翻页阅读，或返回 | 按屏幕提示静音、打断或结束 |

首页只有 **设置／通话／提问**。屏幕右侧提示与实体按键对应，随当前页面变化。
支持中英文、音量调节、已存网络切换和连接状态显示。
提问使用专属 `passport` agent；通话需要在网关配置实时语音服务。
响应速度、可用工具和语音效果取决于模型服务、网关及网络。

## 在浏览器里配置

**[打开网页配置 →](https://ai-passport-agent.codelife.me/)** 无需安装应用、Python 或命令行刷机工具。

![网页配置界面，网络与网关均为虚构演示数据](assets/images/screenshots/web-desktop.png)

| 使用环境 | 连接方式 | 可以做什么 |
| --- | --- | --- |
| 电脑 · Chrome 或 Edge | USB | 安装／更新固件，配置 Wi-Fi 和助手，调整语言与音量 |
| 电脑 · 支持 Web Bluetooth 的浏览器 | 蓝牙 | 读取与保存配置；更新固件仍需 USB |
| iPhone · [Bluefy](https://apps.apple.com/app/bluefy-web-ble-browser/id1492822055) | 蓝牙 | 添加 Wi-Fi／个人热点、修改助手配置；不支持 USB 刷机 |

蓝牙能力受浏览器和系统限制。iPhone 的 Safari 与 Chrome 不提供所需蓝牙接口。
维护者已使用 Bluefy 完成配置，其他浏览器／系统组合仍需实机验证。
参见[浏览器兼容性](docs/web.zh_CN.md)。

- **改完统一保存。** Wi-Fi、助手和偏好先保留为草稿；失败不清空输入，撤销恢复最近读取或确认的状态。
  多项写入不是原子操作，后续失败不会撤销前面已成功的写入。
- **配置跟着设备走。** 最多保存 8 套网络和 4 套助手连接；切换已存 Wi-Fi 不需要重新配对助手。
- **配置与刷机分开。** 保存配置不刷固件。默认勾选「保留配置」；首次安装或完整替换需取消勾选并确认覆盖。
- **没有应用后台。** 配置直接通过 USB／蓝牙写入设备，不上传到本项目服务器。
  草稿中的密钥只在页面内存中，不写浏览器存储；操作结束可关闭页面。

## 开始使用

准备 AI Passport、USB 数据线、可连接的 **2.4 GHz Wi-Fi** 或兼容热点，以及已运行、
有可用模型的 OpenClaw。首次刷机需要电脑上的 Chrome／Edge。
插件已验证基线为 **OpenClaw 2026.10.5**，升级前请检查兼容性；通话另需实时语音服务凭据。

1. **安装固件。** USB 连接设备，打开[配置网页](https://ai-passport-agent.codelife.me/)，选择串口。
   首次安装取消「保留配置」，点击「安装固件」并确认；兼容更新保留勾选，点击「更新固件」。
   替换其他固件前请阅读[数据策略](docs/development/engineering/firmware-layout.zh_CN.md)，没有自动还原原厂固件的功能。
2. **准备 OpenClaw。** 在网页 OpenClaw 区域点击「复制提示词」，发给能够操作网关主机的 Agent。
   它检查／安装项目插件、准备专属助手和安全可达入口，再返回地址与受限配对码。
   优先复用已有资源；缺少账号、授权或凭据时，仍需你完成必要操作。
3. **配网并保存。** 扫描或输入 Wi-Fi 名称与密码，填入网关地址、配对码，点击「保存配置」。
   等设备显示 Wi-Fi 图标，可先测试设备连接再提问。失败修改后重试即可，无需重新刷机；配对码过期需重新生成。
4. **开始对话。** 在设备选择「提问」或「通话」，按屏幕提示操作；配置网页可以关闭。
   换网在设备「设置 → Wi-Fi」中选择；新增网络或改密码，再连接网页配置。

使用 iPhone 热点时，打开 **「允许其他人加入」** 和 **「最大兼容性」**。
如果扫描不到，请停留在「个人热点」设置页再扫描。新网络首次保存时必须可达。
以后开启相同热点，从设备已存网络中选择即可；可以提前用电脑 USB 配好，也可用 Bluefy 蓝牙配置，
不需要把手机 Wi-Fi 切到设备热点。

完整操作参见[配置与日常使用](docs/configuration.zh_CN.md)。

## 自己的网关，自己的权限

```text
配置网页 ── USB／蓝牙 ──> Passport 配置
Passport ── Wi-Fi／WSS ──> OpenClaw＋项目插件 ──> 你的模型服务
```

网页不是云端助手，也不中转对话。模型 API Key 留在网关。
插件通过 OpenClaw 原生扩展接入，不修改已安装的网关文件。
参见[插件安装与升级](tools/openclaw/README.zh_CN.md)。

外网访问的[网络与安全指南](docs/networking.zh_CN.md)介绍了 **Cloudflare Tunnel**、
**SakuraFrp 配合 TLS**、局域网、代理、配对与撤销。
保持网关监听回环地址，公网使用 **WSS**，保留设备认证，不向设备分发管理员 token。
隧道只解决可达性，不自动提供访问控制。仅在局域网使用也可以，前提是设备实际能访问该地址。

持有已配对设备就拥有它的配置权限。请限制专属 agent 工具，遗失后撤销授权。
蓝牙使用 Just Works 加密配对，不代表所有权认证，也不防主动中间人攻击；
请在可信环境配置，不需要时可在设备设置关闭。录音／通话期间会临时释放蓝牙内存，结束后恢复。

## 碰一下，开启热点

利用 Passport 的无源 NFC 标签，在 **iPhone「快捷指令 → 自动化 → NFC」** 中设置：

1. 先通过网页把热点保存到设备，并测试连通。
2. 扫描 NFC 标签，系统支持时选择「立即运行」。
3. 添加「设定个人热点 → 打开」。用「打开」而非「切换」，避免再碰一次反而关闭热点。

如果仍弹出原有网址，用 NFC 写入工具先备份，再把 URL 记录 **替换为普通文本记录**，
例如 `AI Passport`，不要只追加文本。维护者已在 iPhone 实测移除网页提示后，热点自动化正常工作。
不要改写 UID、锁定位或配置区；USB 刷机不会改写 NFC。不要把密码或配对凭据写进标签。
iOS 自动化表现随系统版本变化，这不是设备认证。参见[NFC 详细说明](docs/configuration.zh_CN.md#nfc-与个人热点)。

## 版本与开发

Release 包含应用／完整固件、固件打包文件、OpenClaw 插件、`release-manifest.json`、
`SHA256SUMS.txt` 与许可说明。网页从同一站点提供验证过的固件，浏览器不跳转到 GitHub Release
下载地址；GitHub Pages 本身仍可能受网络影响。哈希能发现传输损坏，不能防止发布者被入侵。

| 组成 | 源码／指南 |
| --- | --- |
| 配置网页 | `web/` · [浏览器指南](docs/web.zh_CN.md) |
| 固件 | `app/`、`main/`、`components/bsp/` · [构建与测试](docs/development/engineering/build-and-test.zh_CN.md) |
| OpenClaw 插件 | [`tools/openclaw/`](tools/openclaw/README.zh_CN.md) |
| 网络接入 | [网络与安全](docs/networking.zh_CN.md) |
| 发布机制 | [CI 与 Release](docs/development/ci/CI-build-and-release.zh_CN.md) |

```sh
make web             # 可重入本地预览，可选 cloudflared HTTPS 通道
make preview-ui      # 使用虚构数据渲染实际固件界面
make plugin-test
make plugin-package
./tools/validate.sh   # 完整检查，需先激活 ESP-IDF 5.5.3
```

CI 检查源码／密钥、主机测试、固件、浏览器 UI 与插件包。
`mac/`、`ios/`、`shared/` 中保留原生客户端源码供开发者参考，不属于推荐上手路径、构建流水线或发布下载。
自动检查不能替代实机音质、无线网络和电池验收。截图采用虚构网络、助手名与保留示例域名。

## 许可与致谢

[MIT](LICENSE)，保留 FoloToy 署名；字体与第三方资源保留各自声明，参见[素材来源](assets/README.zh_CN.md)。
敏感问题请按[安全政策](.github/SECURITY.zh_CN.md)私下反馈。
