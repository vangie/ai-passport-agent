import Foundation

@main struct PairPromptTests {
    static func main() throws {
        let preparationSteps = ["1. 检查并安装插件", "2. 检查助手与语音服务",
                                "3. 检查并建立公网入口", "4. 验证后再生成配对码"]
        var previousStep = PairPrompt.chineseText.startIndex
        for step in preparationSteps {
            guard let match = PairPrompt.chineseText.range(of: step, range: previousStep..<PairPrompt.chineseText.endIndex) else {
                preconditionFailure("Missing or out-of-order setup step: \(step)")
            }
            previousStep = match.upperBound
        }
        for requirement in ["https://github.com/vangie/ai-passport-agent", "SHA256SUMS.txt",
                            "openclaw plugins install --link --accept-capabilities \"$PWD\"",
                            "openclaw gateway call passport.status --json --params '{\"agentId\":\"passport\"}'",
                            "优先查找并复用", "创建持久入口", "/passport/v1", "网关保持回环监听",
                            "缺少凭据时只说明应在网关哪个安全配置入口填写", "修改前备份受影响配置",
                            "保留原生设备签名、配对和鉴权", "不在命令中加 --token", "不输出 gateway.auth.token",
                            "二维码应本地生成", "不把配对码交给第三方二维码或生图服务",
                            "openclaw qr --json --no-ascii --limited --url", "gatewayUrl", "setupCode"] {
            precondition(PairPrompt.chineseText.contains(requirement), "Missing setup requirement: \(requirement)")
        }
        for obsoleteAssumption in ["整站转到网关的外网通道已经有了", "不要新建入口", "不要改网关配置，不要重启网关"] {
            precondition(!PairPrompt.chineseText.contains(obsoleteAssumption), "Obsolete pairing-only assumption")
        }
        func code(_ url: String, token: String = "test-bootstrap") throws -> String {
            let data = try JSONSerialization.data(withJSONObject: ["url": url, "bootstrapToken": token])
            return data.base64EncodedString()
        }
        let first = try code("wss://first.example")
        let second = try code("wss://second.example")
        precondition(PairPrompt.decode(payload: first)?.url == "wss://first.example")
        precondition(PairPrompt.resolve(url: "", code: first)?.code == first)
        precondition(PairPrompt.resolve(url: "wss://first.example/", code: first)?.url == "wss://first.example")
        // The address tested by the app must equal the address the passport decodes.
        precondition(PairPrompt.resolve(url: "wss://first.example", code: second) == nil)
        precondition(PairPrompt.resolve(url: "wss://first.example", code: "not-a-code") == nil)
        let wrapped = String(first.prefix(15)) + "\n \t" + String(first.dropFirst(15))
        precondition(PairPrompt.decode(payload: wrapped)?.code == first)
        precondition(PairPrompt.decode(payload: "oc-pair://" + first)?.code == first)
        let qr = try JSONSerialization.data(withJSONObject: ["url": "wss://first.example", "code": first])
        precondition(PairPrompt.decode(payload: String(decoding: qr, as: UTF8.self))?.code == first)
        let misleadingQR = try JSONSerialization.data(withJSONObject: ["url": "wss://wrong.example", "code": first])
        precondition(PairPrompt.decode(payload: String(decoding: misleadingQR, as: UTF8.self)) == nil)
        let missingToken = Data("{\"url\":\"wss://first.example\"}".utf8).base64EncodedString()
        precondition(PairPrompt.decode(payload: missingToken) == nil)
        let emptyToken = try code("wss://first.example", token: "")
        let wrongScheme = try code("https://first.example")
        let noHost = try code("wss://")
        precondition(PairPrompt.decode(payload: emptyToken) == nil)
        precondition(PairPrompt.decode(payload: wrongScheme) == nil)
        precondition(PairPrompt.decode(payload: noHost) == nil)
        precondition(PairPrompt.decode(payload: "") == nil)
        func link(_ items: [(String, String)]) -> URL {
            var url = URLComponents()
            url.scheme = ConfigurationLink.scheme
            url.host = "pair"
            url.queryItems = items.map { URLQueryItem(name: $0.0, value: $0.1) }
            return url.url!
        }
        let imported = try ConfigurationLink.parse(link([("code", first), ("ssid", "家里 Wi-Fi"),
                                                       ("password", "pass+word & ="), ("name", "家里")]))
        precondition(imported.setupCode == first && imported.gatewayURL == "wss://first.example")
        precondition(imported.ssid == "家里 Wi-Fi" && imported.password == "pass+word & =")
        let legacyLink = link([("code", first)]).absoluteString
            .replacingOccurrences(of: ConfigurationLink.scheme + ":", with: ConfigurationLink.legacyScheme + ":")
        let legacyImported = try ConfigurationLink.parse(URL(string: legacyLink)!)
        precondition(legacyImported.setupCode == first && legacyImported.gatewayURL == "wss://first.example")
        precondition(ConfigurationLink.scheme == "ai-passport-agent")
        let codeOnly = try ConfigurationLink.parse(link([("code", second)]))
        precondition(codeOnly.ssid == nil && codeOnly.password == nil && codeOnly.name == nil)
        for bad in [link([("code", first), ("code", second)]),
                    link([("code", first), ("url", "wss://wrong.example")]),
                    link([("password", "orphan-password")]),
                    link([("code", "invalid")]),
                    link([("code", first), ("save", "true")]),
                    URL(string: "https://pair?code=unused")!,
                    URL(string: "ai-passport-openclaw://pair")!] {
            do { _ = try ConfigurationLink.parse(bad); preconditionFailure("invalid link accepted") }
            catch {}
        }
        print("Setup prompt: plugin, agent, public ingress, validation and credential boundaries PASS")
        print("Pairing: embedded address, mismatch rejection, folded codes and QR validation PASS")
        print("Deep links: validated imports, field preservation, special characters, duplicate and unsafe-action rejection PASS")
    }
}
