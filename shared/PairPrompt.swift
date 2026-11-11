import Foundation
#if os(macOS)
import AppKit
#else
import UIKit
#endif

/// 贴给 OpenClaw 的那一段。界面上不展示，只放进剪贴板。
enum PairPrompt {
    static var text: String { text(for: Locale.preferredLanguages) }

    static func text(for preferences: [String]) -> String {
        L10n.language(for: preferences) == "zh-Hans" ? chineseText : englishText
    }

    static let chineseText = """
    请帮我完成 AI Passport 接入 OpenClaw 的准备与配对，不要只给操作教程，请检查当前状态并实际执行缺少的步骤。AI Passport 是独立连接 Wi-Fi 的随身语音设备，通过网关提问或连续通话；macOS App / iOS App 只负责配置，不中转语音。你应在运行 OpenClaw 网关的主机上操作，不能把插件装到仅用于配置设备的电脑上；无法操作网关主机时，先说明这一项阻碍。

    我授权你按需安装本项目插件、补齐专属助手与模型配置、建立安全公网入口，并生成受限配对码。先检查，已有且正常的部分直接复用；重复执行不要重复安装插件、创建隧道或无故重启。修改前备份受影响配置并保留回退方法；仅在安装或配置确实需要时，通过现有服务管理方式重载或重启网关，并提前说明连接会短暂中断。不要自动升级 OpenClaw、修改核心安装文件、删除现有隧道或影响其他助手。购买服务、产生新费用、改动现有公网路由或删除资源前需另行确认。

    1. 检查并安装插件
    项目唯一源码仓库：https://github.com/vangie/ai-passport-openclaw 。先阅读 README、tools/openclaw/README.zh_CN.md 和 docs/networking.zh_CN.md，按当前 OpenClaw 版本支持的 CLI 操作。插件 ID 为 ai-passport-openclaw。选择与设备固件协议兼容的正式 Release，安装前核对该 Release 的说明与 SHA256SUMS.txt；不要直接运行 main 分支或未经核验的远程安装脚本，也不要盲目追最新版本。
    检查插件是否已安装、启用并加载；仅在未安装时下载该仓库正式 Release 的插件包，核验后解压到网关软件包以外的固定目录，按照该版本文档安装依赖，再在插件目录执行 openclaw plugins install --link --accept-capabilities "$PWD"。已有兼容插件直接复用；不兼容时先说明并保留旧版本，再做必要更新。Node 必须解析现有宿主的 openclaw/plugin-sdk，不要另装第二套网关。

    2. 检查助手与语音服务
    提问使用固定 ID 为 passport 的专属 agent。已有则保留其设定；没有则按当前版本支持的配置方式创建，优先复用网关已有的可用模型、语音转写与实时语音服务。只开放需要的低风险工具，不默认开放执行命令、删除、支付、对外发布等高风险能力，不复制其他助手的私人提示词和聊天记录，不修改其他客户端的默认提供方。通话需要可用的实时语音服务；缺少凭据时只说明应在网关哪个安全配置入口填写，不要索要密钥明文，不要假装已经就绪。若提问已就绪而通话缺少服务，可以继续配对，但必须明确通话尚未配置。

    3. 检查并建立公网入口
    优先查找并复用已有、稳定、证书有效的 wss:// 公网入口，保留真实端口和必要路径。没有入口时，按项目网络指南，优先利用这台主机已登录的隧道服务、已有域名和凭据创建持久入口：例如 Cloudflare 命名隧道，或 SakuraFrp 配合有效 TLS 证书的反向代理。不要假设已配好，也不要停在“请自行配置”；在已有授权和资源范围内直接完成。缺登录、域名、证书或配额时，只请求最少的必要操作；不要擅自购买资源，或用临时随机地址充当稳定入口。
    整个专用主机名应转发到本机网关的实际端口，包括原生网关 WebSocket 和插件的 /passport/v1；仅转发 hooks 的入口不够。网关保持回环监听，不要为了隧道改为局域网或所有网卡监听，不直接暴露明文网关端口。公网必须用证书有效的 wss://，普通 TCP 隧道不能仅改前缀冒充 TLS。保留原生设备签名、配对和鉴权，不关闭证书校验，不放宽成匿名访问，不依赖浏览器交互登录或设备不支持的认证方式。确需调整可信代理时，只信任实际的本地转发节点，不配置任意来源通配。

    4. 验证后再生成配对码
    先运行插件的 node doctor.mjs，以及 openclaw gateway call passport.status --json --params '{"agentId":"passport"}'，区分插件加载、提问模型、实时语音提供方和凭据就绪状态。doctor 不会连接模型，passport.status 也不等于实际语音验收；提问配置需另外核对，不要把探测结果说成实机测试通过。
    再检查公网入口的 DNS、TLS 证书、WebSocket 升级与鉴权挑战，覆盖原生网关及 /passport/v1；网页能打开或普通 HTTP 返回成功不算验证。不能从独立外网测试时如实说明，不能据此声称设备一定可达。失败时先定位插件、路由、TLS 或鉴权问题，不要退回局域网、127.0.0.1 或 ws:// 地址冒充公网入口。
    插件与入口就绪后，用已验证的完整公网 WSS 地址替换占位符运行：

    openclaw qr --json --no-ascii --limited --url '<已验证的公网 WSS 地址>'

    gatewayUrl 和 setupCode 必须取自这次命令成功返回的 JSON。不要编造配对码，不在命令中加 --token，不输出 gateway.auth.token；模型 API key、隧道令牌、证书私钥和配置备份只留在网关的安全位置且仅所有者可读，不能写入回复、二维码、设备或公开仓库。配对码本身也是敏感的一次性凭据，只在当前私有对话交给我；二维码应本地生成，不把配对码交给第三方二维码或生图服务。遵循命令返回的有效期，过期后重新生成，不硬编码有效期。

    成功后简短回复：
    1. 准备结果：插件已安装或已复用、公网入口已建立或已复用、提问与通话分别是否就绪，以及尚未实测的部分。
    2. 一张二维码，内容是一行 JSON：{"url":"gatewayUrl 的实际值","code":"setupCode 的实际值"}。无法生成图片时提供这行 JSON，不伪造二维码图片链接。
    3. 地址：gatewayUrl 的实际值
    4. 配对码：setupCode 的实际值
    两个值都不加尖括号，便于我在 App 粘贴或扫码，再保存到设备。必要条件未满足时，不输出假的成功结果，只说明已完成项、具体阻碍和我需要做的最少一步。
    """

    static let englishText = """
    Prepare and pair my AI Passport with my OpenClaw gateway. You can operate the gateway host. This is an independent community project, not a built-in OpenClaw feature. Repository: https://github.com/vangie/ai-passport-openclaw. Read the project's README, tools/openclaw/README.md and docs/networking.md first, and follow the installed version's supported configuration format and commands. Do not invent plugin configuration fields or gateway commands.

    The goal is for Passport to connect directly to my OpenClaw over Wi-Fi. The macOS App and optional iOS App only configure it; neither relays audio. Do not assume that the plugin, a dedicated assistant or public access is already configured. Check what exists and prepare anything missing within your existing authorization and resources, then return pairing details. Keep normal question-and-answer mode (Ask) and realtime voice calls (Call) separate when reporting readiness.

    Before changing anything, back up affected configuration and record the current state. Reload or restart the gateway through its existing service manager only when installation or configuration requires it, and explain the brief interruption beforehand. Do not automatically upgrade OpenClaw, modify its core installation, delete existing tunnels or disrupt other assistants. Ask separately before purchasing services, incurring new charges, changing existing public routes or deleting resources.

    1. Check and install the plugin
    Check the running OpenClaw version, Node version and installed plugins. Use this project's official Releases and their installation requirements to select a compatible plugin version. Verify the downloaded release against SHA256SUMS.txt. Do not run scripts from main or unknown sources, and do not blindly choose the newest version.
    Check whether ai-passport-openclaw is installed, enabled and loaded. If missing, download the official Release plugin package from this repository, verify it, extract it into a persistent directory outside the gateway package, install dependencies according to that version's documentation, and run this command from the plugin directory:
    openclaw plugins install --link --accept-capabilities "$PWD"
    Reuse a compatible installed plugin. If incompatible, explain the issue, preserve the old version and make only the necessary update. Node must resolve the existing host's openclaw/plugin-sdk; do not install a second gateway.

    2. Check the assistant and voice services
    Ask uses a dedicated agent with the fixed ID passport. Preserve its settings if it exists; otherwise create it using the installed version's supported configuration, preferring the gateway's existing working model, transcription and realtime voice services. Allow only necessary low-risk tools. Do not enable command execution, deletion, payments or external publishing by default. Do not copy another assistant's private prompts or conversation history, or change default providers used by other clients.
    Call requires a working realtime voice service. If credentials are missing, identify the secure gateway configuration entry where I should supply them. Do not ask me to paste secrets into chat or pretend the service is ready. Pairing can continue when Ask is ready but Call lacks a service; clearly state that Call is not yet configured.

    3. Check and establish public access
    Prefer reusing an existing stable public wss:// endpoint with a valid certificate, retaining its actual port and required path. If none exists, follow the project networking guide and create a persistent endpoint using tunnel services already signed in on this host, existing domains and credentials. Examples are a named Cloudflare tunnel, or SakuraFrp together with a reverse proxy and valid TLS certificate. Do not assume access exists or merely tell me to configure it myself: complete the work allowed by existing authorization and resources. If login, a domain, a certificate or quota is missing, request only the minimum necessary action. Do not purchase resources without approval or substitute a temporary random URL for a stable endpoint.
    Forward the entire dedicated hostname to the gateway's actual local port, including its native WebSocket and the plugin's /passport/v1 endpoints. A hooks-only route is insufficient. Keep the gateway bound to loopback; do not switch to LAN or all-interface listening for a tunnel, and do not expose a plaintext gateway port directly. Public access requires wss:// with a valid certificate; changing a plain TCP tunnel's URL prefix does not provide TLS. Preserve native device signatures, pairing and authentication. Do not disable certificate validation, allow anonymous access, or depend on browser-interactive login or authentication unsupported by the device. If trusted proxy settings must change, trust only the actual local forwarding peers, not arbitrary wildcard sources.

    4. Validate before generating a pairing code
    Run the plugin's node doctor.mjs and:
    openclaw gateway call passport.status --json --params '{"agentId":"passport"}'
    Distinguish plugin loading, the Ask model, realtime voice provider and credential readiness. doctor does not connect to the model, and passport.status is not a real voice acceptance test. Check Ask configuration separately and do not call these probes an on-device test.
    Verify public DNS, TLS certificates, WebSocket upgrades and authentication challenges for both the native gateway and /passport/v1. A working web page or ordinary HTTP success is not sufficient. If an independent external-network test is unavailable, say so; do not claim the device is guaranteed to connect. Investigate plugin, routing, TLS or authentication failures rather than presenting a LAN, 127.0.0.1 or ws:// address as public access.
    Once the plugin and endpoint are ready, substitute the verified full public WSS address and run:

    openclaw qr --json --no-ascii --limited --url '<verified public WSS address>'

    Take gatewayUrl and setupCode from this command's successful JSON output. Never invent a pairing code, add --token to a command, or reveal gateway.auth.token. Model API keys, tunnel tokens, certificate private keys and configuration backups must stay in secure, owner-readable locations on the gateway; never include them in the reply, QR code, device or public repository. The pairing code is itself a sensitive one-time credential: share it only in this private conversation. Generate the QR code locally; never send the pairing code to third-party QR or image generation services. Follow the command's reported validity period and generate a new code when it expires; do not hardcode an expiry.

    On success, reply briefly with:
    1. Readiness: whether the plugin and public endpoint were installed/created or reused, whether Ask and Call are each ready, and what has not been tested.
    2. A QR code containing one line of JSON: {"url":"actual gatewayUrl value","code":"actual setupCode value"}. If you cannot create an image, provide this JSON line instead; do not fabricate an image link.
    3. Address: the actual gatewayUrl value
    4. Pairing code: the actual setupCode value
    Do not surround the actual values with angle brackets, so I can paste them into the app or scan the code and save to the device. If requirements are not met, do not fake success: report completed steps, the specific blocker and the minimum action I need to take.
    """

    static func copyToPasteboard() {
        #if os(macOS)
        let board = NSPasteboard.general
        board.clearContents()
        board.setString(text, forType: .string)
        #else
        UIPasteboard.general.string = text
        #endif
    }

    /// 扫码或手填之后，得到要写入设备的地址和配对码。
    static func resolve(url: String, code: String) -> (url: String, code: String)? {
        let typedURL = url.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let decoded = decode(payload: code),
              typedURL.isEmpty || sameGateway(typedURL, decoded.url) else { return nil }
        // Firmware reads the URL embedded in this code. Tests must use that same URL.
        return decoded
    }

    /// 二维码内容，或单独一串配对码。
    static func decode(payload: String) -> (url: String, code: String)? {
        let raw = payload.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !raw.isEmpty else { return nil }
        if let data = raw.data(using: .utf8),
           let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
            let foundURL = (obj["url"] as? String) ?? (obj["gatewayUrl"] as? String) ?? ""
            let foundCode = (obj["code"] as? String) ?? (obj["setupCode"] as? String) ?? ""
            guard let decoded = decodeSetupCode(foundCode),
                  foundURL.isEmpty || sameGateway(foundURL, decoded.url) else { return nil }
            return decoded
        }
        return decodeSetupCode(raw)
    }

    private static func decodeSetupCode(_ raw: String) -> (url: String, code: String)? {
        var code = raw.unicodeScalars.filter { !$0.properties.isWhitespace }.map(String.init).joined()
        if code.hasPrefix("oc-pair://") { code = String(code.dropFirst("oc-pair://".count)) }
        guard !code.isEmpty, code.utf8.count < 1400,
              let inside = setupJSON(code),
              let foundURL = inside["url"] as? String,
              validGateway(foundURL), foundURL.utf8.count < 160,
              let token = inside["bootstrapToken"] as? String,
              !token.isEmpty, token.utf8.count < 128 else { return nil }
        return (foundURL, code)
    }

    static func validGateway(_ text: String) -> Bool {
        guard let url = URLComponents(string: text),
              url.scheme == "ws" || url.scheme == "wss",
              let host = url.host, !host.isEmpty,
              url.user == nil, url.password == nil, url.fragment == nil,
              !text.unicodeScalars.contains(where: { $0.properties.isWhitespace }) else { return false }
        return true
    }

    private static func sameGateway(_ first: String, _ second: String) -> Bool {
        guard validGateway(first), validGateway(second) else { return false }
        func normalized(_ text: String) -> String? {
            guard var url = URLComponents(string: text) else { return nil }
            url.host = url.host?.lowercased()
            if url.path.isEmpty { url.path = "/" }
            return url.string
        }
        return normalized(first) == normalized(second)
    }

    private static func setupJSON(_ raw: String) -> [String: Any]? {
        var body = raw
        if body.hasPrefix("oc-pair://") {
            body = String(body.dropFirst("oc-pair://".count))
        }
        body = body.replacingOccurrences(of: "-", with: "+").replacingOccurrences(of: "_", with: "/")
        let pad = (4 - body.count % 4) % 4
        body += String(repeating: "=", count: pad)
        guard let data = Data(base64Encoded: body) else { return nil }
        return try? JSONSerialization.jsonObject(with: data) as? [String: Any]
    }
}
