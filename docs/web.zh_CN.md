<p align="right"><strong>简体中文</strong> · <a href="web.md">English</a></p>

# 浏览器配置台

静态网站作为新的主推配置入口，现有 macOS App / iOS App 继续保留。
不需要账户、数据库或配置后台，设备仍是配置的唯一数据源。
语音由 Passport 直接交给 OpenClaw，不经过网页或网站托管服务。

## 本地启动

需要 Node.js 22.12+ 或 24，以及 Python 3：

```sh
make web                 # 构建并启动 http://localhost:4173
make web-dev             # Vite 开发服务 http://localhost:5173
make web-test            # 协议与浏览器测试；本机需要 Chrome
make web-build           # 生成 web/dist 静态文件
```

浏览器将 `http://localhost` 和回环地址视为可信上下文，本机测试不必配置 HTTPS
证书。不支持直接双击打开 `index.html`。手机通过普通局域网 IP 打开电脑的 HTTP
地址不享受这个例外，需要可信 HTTPS。

临时 HTTPS 测试：保持 `make web` 运行，在另一个终端执行：

```sh
make web-tunnel          # 需要 cloudflared；仅转发 localhost:4173
```

Quick Tunnel 地址公开、临时，不保证所有网络都可访问，结束进程即关闭。
仅提供构建后的静态目录，不暴露仓库、环境变量、串口或本机文件系统。
隧道只提供网页，不传输 USB／蓝牙数据，也不代理 OpenClaw。
不要在不可信的网页副本中输入真实凭据。

## 连接和配置

1. 电脑用 Chrome / Edge 打开网页，选择「USB 连接」和设备串口；先关闭占用串口的
   原生应用或监视器。蓝牙配置需要实现 Web Bluetooth 的浏览器，并在空闲设备上开启
   蓝牙。iPhone Safari 和微信内置网页不支持；Bluefy 是待真机验收的候选方案。
2. 页面读取已存 Wi-Fi、助手和设备偏好。让 Passport 扫描附近 2.4 GHz 网络，或手动
   填写名称。只有已存网络或明确无密码网络可以留空密码。测试连接可能切换 Passport
   网络，不会切换电脑的 Wi-Fi。
3. 复制配置提示词给能操作 OpenClaw 网关主机的 Agent，粘贴返回的配对码或二维码
   JSON，填写助手名称。地址不一致时拒绝保存，不静默覆盖。首版不含相机扫码。
4. 修改屏幕语言、音量和所选助手的直连／Shadowsocks 路由，点击「保存配置」。
   当前固件仅支持 `aes-256-cfb`，不回读密码。多组修改依次写入，并非原子事务：
   后一组失败时，前面已成功的组不会回滚。
5. 保存、报错或断线均保留输入；「撤销修改」才主动丢弃草稿。忘记网络、删除助手和
   设为当前立即生效。配置与密钥不上传配置服务器，也不存入浏览器持久存储。

网页不能读取系统钥匙串密码，不能静默开启个人热点或读取热点密码。
若找不到本机 iPhone 热点，请开启「允许其他人加入」和「最大兼容性」，进入系统
「个人热点」页并保持亮屏。切换系统设置时蓝牙连接的后台行为需要真机验证。

## 固件不走运行时 Release 跳转

网页依赖全部本地打包。CI 在构建时下载正式 Release，核对发布清单中的长度和
SHA-256，把两个固件及清单放入网站自己的 `firmware/` 目录。
用户浏览器从网站同源加载，不请求 GitHub Releases、S3 或第三方 CDN。
GitHub Pages 域名本身仍可能慢或不可达，临时 Cloudflare 地址也不能保证可达。

本地可以使用已下载的正式 Release：

```sh
python3 tools/web.py firmware --from-release RELEASE文件目录
make web
```

目录需含 `release-manifest.json`、`FoloToy-AI-Passport.bin` 和
`FoloToy-AI-Passport-full.bin`。生成副本被 Git 忽略。
缺少固件时仍可构建配置页，但不能使用内置固件刷机。
也可选择本地应用 `.bin` 并查看 SHA-256；本地文件的散列不证明作者身份，只使用可信固件。

保留配置的应用升级需先成功通过 USB 读取设备，校验 ESP32-C3 镜像头和设备当前
分区表，仅写 `0x10000..0x34ffff`，保留 NVS、Card ID 和音频数据。
首次安装／完整恢复将内置合并镜像写到零地址，会覆盖配置，需单独确认；两种模式均不
整片擦除。下载文件在设备复位前校验散列，刷完还比对设备 MD5。写入中不要拔线。
网页刷机和蓝牙兼容性仍需真机验收，演示模式通过不代表硬件测试通过。

## GitHub Pages

`.github/workflows/web.yml` 运行协议／浏览器测试，准备最新正式固件，部署静态网站。
PR 只测试和构建；main 的相关推送、手动触发和正式 Release 发布会部署。
成功的标签 Release 工作流完成后也触发部署，覆盖使用 GitHub 工作流令牌发布的情况。
不需要运行时后台或保存部署密钥。Pages 构建来源设为 **GitHub Actions**；
相对资源路径支持仓库子目录。

源码和固件散列处于同一个网站信任边界，散列防损坏，不防发布者被入侵。
不加载第三方脚本、统计，也不把密钥写入 URL。
OpenClaw 公网入口与鉴权独立维护，见[网关连接](networking.zh_CN.md)。
