import Foundation

/// Links import form values only. They never run a test, flash firmware, or save a device.
struct ConfigurationLink: Equatable {
    static let scheme = "ai-passport-agent"
    static let legacyScheme = "ai-passport-openclaw"
    let gatewayURL: String?
    let setupCode: String?
    let ssid: String?
    let password: String?
    let name: String?

    enum Failure: Error, LocalizedError {
        case invalidLink, invalidCode, addressMismatch, invalidNetwork, invalidName
        var errorDescription: String? {
            switch self {
            case .invalidLink: return L10n.string("无法识别这个配置链接。")
            case .invalidCode: return L10n.string("链接中的配对码不完整或格式不对。")
            case .addressMismatch: return L10n.string("链接中的地址与配对码不一致。")
            case .invalidNetwork: return L10n.string("Wi-Fi 名称或密码不符合设备要求。")
            case .invalidName: return L10n.string("OpenClaw 名称过长。")
            }
        }
    }

    static func parse(_ url: URL) throws -> Self {
        guard url.absoluteString.utf8.count <= 16000,
              let components = URLComponents(url: url, resolvingAgainstBaseURL: false),
              [scheme, legacyScheme].contains(components.scheme?.lowercased() ?? ""),
              components.host == "pair" || components.host == "configure",
              components.path.isEmpty || components.path == "/",
              components.user == nil, components.password == nil,
              components.port == nil, components.fragment == nil else { throw Failure.invalidLink }
        let allowed: Set<String> = ["code", "setupCode", "url", "gatewayUrl", "ssid", "password", "name"]
        var values: [String: String] = [:]
        for item in components.queryItems ?? [] {
            guard allowed.contains(item.name), values[item.name] == nil,
                  let value = item.value, !value.isEmpty else { throw Failure.invalidLink }
            values[item.name] = value
        }
        guard !values.isEmpty,
              !(values["code"] != nil && values["setupCode"] != nil),
              !(values["url"] != nil && values["gatewayUrl"] != nil) else { throw Failure.invalidLink }
        let rawCode = values["code"] ?? values["setupCode"]
        let address = values["url"] ?? values["gatewayUrl"]
        var pair: (url: String, code: String)?
        if let rawCode {
            guard let decoded = PairPrompt.decode(payload: rawCode) else { throw Failure.invalidCode }
            guard PairPrompt.resolve(url: address ?? "", code: rawCode) != nil else { throw Failure.addressMismatch }
            pair = decoded
        } else if let address, !PairPrompt.validGateway(address) || address.utf8.count >= 160 {
            throw Failure.invalidLink
        }
        if let ssid = values["ssid"], ssid.utf8.count > 32 { throw Failure.invalidNetwork }
        if let password = values["password"], values["ssid"] == nil || password.utf8.count > 64 {
            throw Failure.invalidNetwork
        }
        if let name = values["name"], name.utf8.count >= 24 { throw Failure.invalidName }
        return Self(gatewayURL: pair?.url ?? address, setupCode: pair?.code,
                    ssid: values["ssid"], password: values["password"], name: values["name"])
    }
}
