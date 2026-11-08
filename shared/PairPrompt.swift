import Foundation
#if os(macOS)
import AppKit
#else
import UIKit
#endif

/// 贴给 OpenClaw 的那一段。界面上不展示，只放进剪贴板。
enum PairPrompt {
    static let text = """
    请给一块 AI Passport 做一次配对。你在运行 OpenClaw 网关的这台电脑上。不要改网关配置，不要重启网关，不要把 gateway.auth.token 写进回复，也不要在命令里加 --token。

    AI Passport 是挂在身上的小设备。它自己连 Wi-Fi，再从外面连这台网关。整站转到网关的外网通道已经有了。不要新建入口，不要改网关配置，不要把网关改成局域网监听。

    找出这条通道的公网主机名，地址用 wss://那个主机名。只转发某一个路径的主机名不算。不要用 ws://，不要用 127.0.0.1，也不要交局域网地址。

    运行：

    openclaw qr --json --no-ascii --limited --url wss://找到的主机名

    失败就只说明原因。不要换地址，不要改用网关总口令，不要编造配对码。配对码是一长串；只有几位，说明命令没有跑成功。

    成功后只回复下面三样，各一次。不要加解释，不要加尖括号，两个值必须来自 JSON 里的 gatewayUrl 和 setupCode：

    1. 一张二维码。内容是一行 JSON，url 填 gatewayUrl 的值，code 填 setupCode 的值。
    2. 地址：后面跟 gatewayUrl 的值
    3. 配对码：后面跟 setupCode 的值
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
