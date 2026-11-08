[English](networking.md) · **简体中文**

# 网关连接与内网穿透

Passport 通过 Wi-Fi 直接连接自己的 OpenClaw 网关。Mac／iPhone App 负责
配置，不中转对话。先在网关主机安装[本项目插件](../tools/openclaw/README.zh_CN.md)。
以下均使用保留的示例地址，不提供共享网关或私人部署参数。

## 选择入口

| 方式 | 设备填写的地址 | 适用情况 |
| --- | --- | --- |
| 同一局域网 | `ws://gateway-host:18789` | 可信局域网调试，无 TLS |
| Cloudflare Tunnel | `wss://passport.example` | 公网 TLS，不需路由器映射端口 |
| SakuraFrp 等 TCP 隧道 | `wss://assigned-host:assigned-port` | 选择区域节点，仍需解决 TLS |
| 私有 VPN | 私网网关地址 | 设备或路由器必须真正加入 VPN |

HTTPS 代理或已支持的 Shadowsocks 路线只是设备的可选出站路径，不代替网关
认证；按助手配置，必要时才开启。区域入口稳定时优先直连。不要关闭证书
校验，不要开放无认证网关，不要发布凭据。

## Cloudflare Tunnel

1. 在 OpenClaw 主机安装 `cloudflared`，登录自己的 Cloudflare 账户，在自己
   的域名区域创建命名隧道和 DNS 路由。
2. OpenClaw 保持监听回环地址，把整个主机名转发到
   `http://127.0.0.1:18789`，包括原生网关 WebSocket 和 `/passport/v1`。
   只转发 hooks 的 HTTP 路由无法承载 Passport 会话。
3. 隧道凭据放在仓库以外，仅文件所有者可读。自己的私有配置示例：

```yaml
tunnel: YOUR_TUNNEL_ID
credentials-file: /private/path/to/YOUR_TUNNEL_ID.json
ingress:
  - hostname: passport.example
    service: http://127.0.0.1:18789
  - service: http_status:404
```

4. 通过主机现有服务管理器运行隧道，在设备实际使用的网络验证公网地址与
   原生 WebSocket 升级。
5. 保留 OpenClaw 设备配对／令牌认证。如果额外使用 Cloudflare Access，
   设备必须配置其支持的服务令牌请求头；浏览器交互登录页不能代替
   WebSocket 认证。策略应尽量窄，凭据应可单独撤销。

插件经回环转发到原生 OpenClaw，这个本地节点需符合已有可信代理策略。
不要宽泛信任任意转发头。网关公网暴露范围应单独评审：隧道隐藏源站地址，
不等于公网服务自动变成了私有服务。

## SakuraFrp／区域 TCP 隧道

1. 在网关主机安装官方启动器／客户端，登录自己的账号，通过提供方当前的
   控制台管理隧道和节点。
2. 创建指向本机 **TLS 反向代理**的 TCP 隧道，不要直接公开未加密的网关
   套接字。代理需使用设备所填主机名匹配、公共信任的证书，支持 WebSocket，
   并转发到 `127.0.0.1:18789`，包括 `/passport/v1`。
3. 提供方分配的域名／端口只有真正支持兼容 TLS 和有效证书时，才能省略
   自有域名。普通 TCP 隧道不会自动加 TLS，不能仅把前缀改成 `wss://`。
   免费套餐若不提供这些能力，需要自备域名与 TLS 代理，或选择能终结 TLS
   的入口。
4. 将最终 `wss://assigned-host:assigned-port` 填入 App。直连时关闭该助手的
   代理，隧道访问密钥与账号文件留在本机，不放进固件或 Release。

免费配额、端口和节点质量可能变化。排障应比较不同网络的 DNS、TCP／TLS、
WebSocket 握手、首音频及长通话丢包，不应无证据归因运营商或盲目换端口。
不要把删除工作中的隧道或轮换账户凭据作为默认排障动作。

## 配对与排障

公网通道已经存在后，在网关执行
`openclaw qr --json --no-ascii --limited --url wss://passport.example`，
通过 App 的字段、二维码或深链导入返回的地址和配对码。设备授权及撤销由
网关管理，不要把 `gateway.auth.token` 贴进公开 Issue，也不要在固件内置
共享管理员令牌。

失败时分清 Wi-Fi、DNS、时间同步、TCP／TLS、WebSocket、原生配对、插件
协议和模型就绪等阶段。在网关运行插件 doctor 与 `passport.status`。
分享日志前删掉域名、IP、令牌、配对码、Wi-Fi 密码和用户对话。
