<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# AI Passport OpenClaw

把 **FoloToy AI Passport** 变成随身 OpenClaw 语音终端：用 Mac 配置，之后直接通过 Wi-Fi 工作，不需要一直连接手机。面向 ESP32-C3、8 MB Flash、240 × 320 屏幕、麦克风、扬声器和三个按键。

这是基于 [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport) 的独立社区项目，不是 FoloToy 官方固件，也不是 OpenClaw 发行版。

## 项目组成

| 组件 | 源码 | 用途 |
| --- | --- | --- |
| 固件 | `app/`、`main/`、`components/bsp/` | 中英文界面、通话／提问、设备保存配置、Wi-Fi、音频与传输 |
| Mac App | `mac/`、`shared/` | USB 状态、Wi-Fi 和助手配置、配对、固件检查与仅应用区刷写 |
| iPhone App | `ios/`、`shared/` | 蓝牙配置、渐进式 Wi-Fi 搜索、个人热点提示、配对与设备偏好 |
| OpenClaw 插件 | [`tools/openclaw/`](tools/openclaw/README.zh_CN.md) | 可独立升级的原生 Talk 集成、带认证的 `/passport/v1` WebSocket 传输，不修改 OpenClaw 安装文件 |
| 网络指南 | [联网与内网穿透](docs/networking.zh_CN.md) | 局域网、Cloudflare Tunnel、SakuraFrp、TLS、认证与排障 |
| 开发工具 | `tools/`、`tests/`、`sim/` | 主机测试、固件校验、打包和模拟器启动器 |

通话通过 OpenClaw 使用实时语音模型；提问将录音提交给网关的 `passport` 助手，使用前请在 OpenClaw 创建这个专用 Agent 并配置模型和工具权限。多个助手配置目前选择的是不同网关连接，尚不能改变提问使用的 Agent ID。模型密钥留在网关上。配置保存在 Passport；Mac 也会通过系统存储记住常用表单值和已保存的 Wi-Fi 密码。不要公开配置导出、配对码、日志和设备凭据。

### Endpoint 与私有配置

源码及全新固件不内置个人网关、隧道域名、代理节点或模型 API Key。网关地址通过配对或配置输入，与设备凭据一起保存在 Passport 的 NVS；配置端从设备读取这些资料。模型提供方地址和 API Key 留在网关的 OpenClaw 配置中，不放入 Passport 固件。

固定网络地址仅为基础设施默认值：公共 NTP 校时服务器（`ntp.aliyun.com`、`pool.ntp.org`、`time.cloudflare.com`）、连接本机 OpenClaw 网关的回环地址，以及 `192.168.4.1` 配网页面。它们不是个人助手的 endpoint。示例使用保留的 `.example`／`.invalid` 域名。CI 执行公开源码审计和密钥扫描；设备数据导出、私有配置、签名文件、本机构建产物与诊断日志均不应进入 Git。

## 从 Release 开始

从 [Releases](https://github.com/vangie/ai-passport-openclaw/releases) 下载：

- `ai-passport-openclaw-macos-universal-adhoc.zip`：macOS 14+，兼容 Apple Silicon 与 Intel，内置匹配固件。目前为 **ad-hoc 签名，未经 Developer ID 签名及 Apple 公证**；首次运行可能需要本机安全设置明确批准，不分发 Apple 签名凭据。
- `ai-passport-openclaw-firmware.tar.gz`：合并／应用固件、校验清单、引导程序、分区表和刷写参数。下载后核对 `SHA256SUMS.txt`。
- `ai-passport-openclaw-<插件版本>.tgz`：安装到 OpenClaw 所在电脑，见[插件说明](tools/openclaw/README.zh_CN.md)。

1. 在网关安装插件。**已验证基线为 OpenClaw 2026.10.5**，不承诺未来所有 SDK 版本直接兼容。在网关配置模型凭据，通话需要支持实时语音的提供方。
2. 使用已配置好的局域网地址或公网 WSS 地址访问网关。暴露服务前先读[网络指南](docs/networking.zh_CN.md)。
3. 打开 Mac App，用数据线连接 Passport。刷机需要 Mac 上有 Python 3 和 `esptool`，例如通过 `pipx install esptool==4.11.0` 安装；App 不会悄悄安装系统工具。
4. 配置 Wi-Fi、选择助手，使用 OpenClaw 的限权配对码绑定。App 的复制提示词流程可以帮助你向自己的助手获取配对信息。
5. 用底部的“保存配置”统一保存 Wi-Fi、所有改过的助手、屏幕语言和音量。有可保存的修改时按钮点亮；“撤销修改”恢复设备最新设置，不执行写入。保存配置不会刷机；固件更新在独立的“设备维护”中操作。日常仅应用区更新写入 `0x10000`，**只有分区布局兼容时**才保留配置。空白设备先将合并固件写入 `0x0`；这可能重置已有配置。

Mac 单页界面还提供屏幕语言、与设备同步的扬声器音量、独立的设备维护区和设备端连接检测。
语言、音量等设置均为草稿，点击“保存配置”后才生效。Wi-Fi 列表随行数收缩，展开较多网络时可滚动；配对、连接方式和检测默认折叠。
各项设置串行保存，每步确认读回设备后才继续，Wi-Fi 最后保存。这不是原子事务：中途失败保留已完成步骤和其余未保存的修改，重试只保存剩余修改。保存不会清空表单或重复提交已确认的配对码。
切换助手自动保留各套草稿；全局保存和撤销涵盖这些草稿。
音量保存在 Passport，不另存一份 Mac 偏好；匹配的固件不会重复刷写。
通话、录音或设备检测期间，影响连接的控件不可用；写入成功或失败都保留表单。
底栏显示 USB、设备 Wi-Fi、当前助手和可读取的电量。
闲置 60 秒后息屏而非关机；连接电脑 USB 时保持唤醒，不把 USB 连接当作正在充电。

“测试地址”从 Mac 发起；“检测当前助手”使用 Passport 自己的 Wi-Fi、连接方式和设备凭据，
检查网关认证、提问会话访问权限和插件的实时语音模型配置。它不会录音、发送提问，
也不代表端到端语音质量已验收；检测中可以停止。
旧固件仍可使用基础配置，更新后才支持音量同步和设备端检测。模型密钥仍留在网关。
协议增加 `DEVICE`、`VOLUME <expected> <desired>`、`CHECK` 和 `CHECK_CANCEL`，
通过 HELLO 的 `device-control` 能力标记识别。音量写入会拒绝过期读数和忙碌设备，
持久化成功后才确认保存。

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

### 产品标识

产品源码文件、C API／宏、Swift 类型、Xcode 目标、素材与模拟工具统一使用
`Passport`／`passport`／`PASSPORT`。配置热点名为 `Passport-Setup`，密码为 `passport-setup`。
iOS Bundle ID 为 `ai.mirasim.passport`；NVS 配置命名空间为 `passport`，
设备身份命名空间为 `passport_id`；配置序列化标识为 `PSP1`。
专用 OpenClaw Agent ID 为 `passport`，设备会话为
`agent:passport:passport-<device-id>`。BLE 服务 UUID 不变。
不迁移未发布原型的身份和配置：开发设备需重新配网和网关配对，新 iOS 身份会与
早期开发版分别安装。使用前需在网关配置专用 `passport` 助手的模型和工具权限。

### iPhone 蓝牙配网

Wi-Fi 区域使用紧凑的已保存网络列表，名称和密码采用单行布局，操作区域至少为 44 点。较大字号时，字段标题自动移到输入框上方。

已保存的 Wi-Fi 也可直接在 Passport 上切换：进入**设置 → 网络**，用「切换」
选中网络，「打开」查看详情，再点「连接」，沿用设备保存的密码。连接在后台进行，
「返回」只离开页面，不取消切换。切换期间暂停通话、提问和配置写入。
连接成功后保存为重连／开机使用的网络，不删除或重排其他网络；失败时尝试恢复原
Wi-Fi 并显示原因。新增网络和修改密码仍通过手机或 Mac App 完成。

蓝牙设备显示为 `AI Passport XXXX`，其中 `XXXX` 为设备蓝牙地址的末四位十六进制字符。名称重启后保持不变，配置服务标识不变。

iPhone App 通过蓝牙配置工牌，无需把手机切到临时 Wi-Fi。在工牌设置中开启蓝牙，在 App 里选择设备，再由**工牌扫描附近网络**，或手动输入 2.4 GHz Wi-Fi 名称和密码。iOS 不提供已存 Wi-Fi 密码；本版不申请定位权限，也不读取手机当前 SSID。

只保留**「搜索 Wi-Fi」**一个入口，普通网络和个人热点在同一列表中展示，工牌最多返回 32 个网络名称。识别到的 iPhone 个人热点显示链环图标，其他网络显示 Wi-Fi 图标。识别依据是已观察到的 Apple 厂商广播信息，不猜测网络名称；它只是尽力识别的提示，不验证网络所有权或安全性，也不能识别所有热点。未识别的热点仍可作为普通 Wi-Fi 选择；不提供 `NET_KIND` 扩展信息的旧固件显示普通图标。

声明 `scan-live` 的固件按允许的 2.4 GHz 信道逐个扫描，发现即发送，最多返回 32 个网络名称。App 在扫描过程中持续更新列表，已有行不重新排序；可提前选中网络，保存配置需等待扫描结束。旧固件仍兼容，但只能在整轮扫描后返回结果。

使用个人热点时，在系统设置中开启「允许其他人加入」和「最大兼容性」，然后从同一 Wi-Fi 列表选择其名称，或手动填写。选中后更新主页草稿，不立即连接或保存；填好密码后用全局**「保存配置」**通过蓝牙发送给工牌。取消搜索保留原有草稿。重复选中相同名称保留已填密码，改选其他网络时清空密码，避免误用。扫描为空时可重新扫描；扫描失败或蓝牙断开后仍可手动输入。iOS 不允许 App 自动开启热点或读取热点密码。工牌已保存的热点可以留空密码沿用。

热点名称就是 iPhone 名称，不能单独改名。表单填写的是现有名称，用于告诉工牌连接哪个网络，并不是给手机改名。本 App 没有 Apple 限制申请的真实设备名称读取权限，可扫描选择热点，或从「设置 → 通用 → 关于本机 → 名称」复制完整名称，不猜测系统返回的通用名称。更改 iPhone 名称后，热点名称也会变化，需要同步更新工牌的 Wi-Fi 配置。

如果 Passport 提示未发现热点，连接期间请停留在系统「设置 → 个人热点」页并保持亮屏；部分 iPhone 离开此页后不再向非 Apple 设备广播热点。App 支持在后台接收蓝牙配网结果。设备拒绝配置时不再主动断开蓝牙，保留草稿供修改重试，并分别提示未发现网络、认证失败、安全模式不兼容或获取 IP 超时。

App 从工牌读取已保存网络（最多八套）；选择已有网络时，密码留空会沿用设备已存密码。全局「保存配置」先保存修改过的配对和语言，再保存 Wi-Fi，每步等待设备确认并重新读取。保存成功、失败及重连均保留输入；「撤销修改」恢复最后读取或确认的值。「忘记网络」是单独确认的操作。保存配置不刷固件，也不修改手机的网络设置。

iPhone 配置页按设备连接、Wi-Fi、OpenClaw 和设备偏好分组。附近网络在独立弹层中选择，已保存网络和更多选项按需展开。「保存配置」和「撤销修改」随表单滚动，不再固定底栏遮挡内容。较大的辅助功能字号会将并排操作改为纵向排列。

在设备「设置」菜单选中「蓝牙」，按中间键即可直接开启或关闭，不进入二级页面；菜单行显示当前状态，切换或保存配置期间禁用操作。关闭会断开手机连接，但不清除蓝牙绑定或已存 Wi-Fi。蓝牙在待机时默认开启，不再因两分钟未连接或退出配网页面而关闭。开机后优先给已存 Wi-Fi 最多十秒的连接时间，再开启蓝牙；手动开启不必等待。录音、通话及网关操作期间临时释放蓝牙内存并断开手机，音频和网关资源释放完毕后自动恢复。手动关闭后，保持关闭直到再次开启或设备重启。仅广播不阻止原有待机休眠。空闲网关连接会给蓝牙让出内存，因此下一次提问可能需要重新连接网关。手机配置连接不会因保存配置或切换 Wi-Fi 而主动关闭。

手机端要求固件声明 `ble-secure`：蓝牙写入必须在加密连接上进行，回复按 MTU 分包并逐包确认。旧固件需先通过 Mac App 更新，才能发送密码。固件在 NVS 中保存最多三台配置端的蓝牙绑定，正常重连及重启不必重复配对；首次连接、系统遗忘设备或清除绑定后仍需配对。当前 Just Works 配对提供传输加密，但**不提供中间人攻击防护或设备所有权验证**。在不可信环境使用时，不需要配置就从「设置」关闭蓝牙。

运行 `cd ios && xcodegen generate` 生成工程，打开 `ios/Passport.xcodeproj`，选择本机签名团队并运行到真实 iPhone。无签名的模拟器构建可检查界面，但不能通过蓝牙配置真实工牌。iPhone 真机配网及个人热点与蓝牙并用仍需硬件验收。

生成工程后可运行模拟器布局测试：

```bash
xcodebuild -project ios/Passport.xcodeproj -scheme Passport \
  -destination 'platform=iOS Simulator,name=iPhone 17' test CODE_SIGNING_ALLOWED=NO
```

## CI 与发布

PR 和 main 推送运行仓库／隐私检查、C／Python／Swift 主机测试、Mac 构建、ESP-IDF 固件构建，以及 Node 22／24／26 插件矩阵。版本标签构建全部发行产物；主机测试、固件、插件和通用 Mac App 全部成功后才发布。手动触发 Release 只生成产物，不创建 GitHub Release。见 [CI 与发布](docs/development/ci/CI-build-and-release.zh_CN.md)。

所有 Action 固定到提交 SHA，只有发布任务获得仓库写权限。源码扫描只能预防，不能证明未来任意修改都没有秘密；发布前仍应检查差异和生成产物。不要上传本机 NVS 转储和私人配置。

## 许可与致谢

采用 [MIT](LICENSE)，保留 FoloToy 署名。硬件／BSP 和开发文档源自上游项目；字体和其他第三方素材保留各自许可，见[素材说明](assets/README.zh_CN.md)。敏感问题请按[安全政策](.github/SECURITY.zh_CN.md)私下报告。
