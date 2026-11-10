import Foundation

struct PassportAgent: Identifiable, Equatable {
    var id: Int
    var name: String
    var url: String
    var state: String
    var current: Bool
}

struct PassportNetwork: Identifiable, Equatable {
    var id: Int
    var name: String
    var ssid: String
    var current: Bool
}

struct NearbyNetwork: Identifiable, Equatable {
    var id: String { ssid }
    var ssid: String
    var rssi: Int
    var personalHotspot = false
    var symbol: String { personalHotspot ? "personalhotspot" : "wifi" }

    static func receive(_ line: String, into networks: inout [NearbyNetwork]) -> Bool {
        let parts = line.split(separator: " ", maxSplits: 2, omittingEmptySubsequences: false)
        guard parts.count == 3 else { return false }
        if parts[0] == "NET", let rssi = Int(parts[1]), !parts[2].isEmpty {
            let ssid = String(parts[2])
            if let index = networks.firstIndex(where: { $0.ssid == ssid }) {
                networks[index].rssi = max(networks[index].rssi, rssi)
            } else {
                networks.append(NearbyNetwork(ssid: ssid, rssi: rssi))
            }
            return true
        }
        if parts[0] == "NET_KIND", parts[1] == "hotspot",
           let index = networks.firstIndex(where: { $0.ssid == PassportCommand.decode(String(parts[2])) }) {
            networks[index].personalHotspot = true
            return true
        }
        return false
    }
}

enum ProvisioningFailure: Error, LocalizedError {
    case invalidResponse, tooLong

    var errorDescription: String? {
        switch self {
        case .invalidResponse: return "设备返回的数据不完整，请重新连接。"
        case .tooLong: return "设备返回的数据过长，请更新固件后重试。"
        }
    }
}

enum ProvisioningRejection {
    static func message(for line: String) -> String {
        let reason: String
        switch line {
        case "ERR join notfound":
            reason = "Passport 未发现这个 Wi-Fi。若是 iPhone 热点，请开启「允许其他人加入」和「最大兼容性」，核对热点名称。再次保存后，切到系统「个人热点」页并保持亮屏，直到设备连上。"
        case "ERR join pass":
            reason = "Wi-Fi 认证未通过，请核对密码。若是 iPhone 热点，请使用系统「个人热点」页显示的 Wi-Fi 密码。"
        case "ERR join security":
            reason = "Wi-Fi 的安全模式不兼容。若是 iPhone 热点，请开启「最大兼容性」；路由器请使用 WPA2 或 WPA2/WPA3 兼容模式。"
        case "ERR join timeout":
            reason = "Wi-Fi 连接或获取 IP 地址超时。请靠近热点后重试；若是 iPhone 热点，请在连接期间停留于系统「个人热点」页。"
        case "ERR stale":
            reason = "配置已在别处更改，请在「设备操作」中重新读取配置后再保存。"
        case "ERR busy":
            reason = "设备正在通话或处理其他操作，请结束后重试。"
        case "ERR scan":
            reason = "Wi-Fi 扫描未完成，已发现的网络仍可选择，也可以重新扫描。"
        default:
            reason = line.hasPrefix("ERR join")
                ? "Passport 连接 Wi-Fi 失败，请检查热点设置后重试。"
                : "设备未保存配置（\(line.dropFirst(4))）。"
        }
        return reason + " 输入内容仍保留。"
    }
}

enum PassportCommand {
    static func encode(_ text: String) -> String {
        let allowed = CharacterSet(charactersIn: "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.")
        return text.addingPercentEncoding(withAllowedCharacters: allowed) ?? ""
    }

    static func decode(_ text: String) -> String { text.removingPercentEncoding ?? text }
}

struct BLELineBuffer {
    private var bytes = Data()

    mutating func append(_ fragment: Data) throws -> [String] {
        var lines: [String] = []
        for byte in fragment {
            if byte == 10 {
                guard let line = String(data: bytes, encoding: .utf8) else {
                    bytes.removeAll()
                    throw ProvisioningFailure.invalidResponse
                }
                lines.append(line.trimmingCharacters(in: .newlines))
                bytes.removeAll(keepingCapacity: true)
            } else {
                guard bytes.count < 8192 else {
                    bytes.removeAll()
                    throw ProvisioningFailure.tooLong
                }
                bytes.append(byte)
            }
        }
        return lines
    }
}

struct BLECommandWriter {
    private var remaining: Data
    private var inFlight = 0
    var complete: Bool { remaining.isEmpty && inFlight == 0 }

    init(_ command: String) { remaining = Data((command + "\n").utf8) }

    mutating func next(maximum: Int) -> Data? {
        guard inFlight == 0, !remaining.isEmpty, maximum > 0 else { return nil }
        let fragment = Data(remaining.prefix(min(maximum, 256)))
        inFlight = fragment.count
        return fragment
    }

    mutating func acknowledge() {
        guard inFlight > 0 else { return }
        remaining.removeFirst(inFlight)
        inFlight = 0
    }
}

struct PassportSnapshot: Equatable {
    var generation: UInt = 0
    var language = "zh"
    var firmware = "-"
    var secureBLE = false
    var liveScan = false
    var networks: [PassportNetwork] = []
    var agents: [PassportAgent] = []
    private var networkCount = 0
    private var agentCount = 0
    private var connectedNetwork = -1
    private var activeAgent = -1

    mutating func receive(_ line: String) throws {
        let parts = line.split(separator: " ").map(String.init)
        guard let head = parts.first else { return }
        switch head {
        case "HELLO":
            guard parts.count >= 7, parts[1] == "ai-passport-openclaw",
                  let generation = UInt(parts[2]), ["zh", "en"].contains(parts[5]) else {
                throw ProvisioningFailure.invalidResponse
            }
            self.generation = generation
            language = parts[5]
            firmware = parts[6]
            secureBLE = parts.contains("ble-secure")
            liveScan = parts.contains("scan-live")
        case "NETS" where parts.count == 4:
            guard UInt(parts[1]) == generation, let count = Int(parts[2]), (0...8).contains(count),
                  let current = Int(parts[3]), current >= -1, current < count else {
                throw ProvisioningFailure.invalidResponse
            }
            networkCount = count
            connectedNetwork = current
            networks = []
        case "SAVED":
            guard parts.count == 4, let index = Int(parts[1]), index == networks.count,
                  networks.count < networkCount else { throw ProvisioningFailure.invalidResponse }
            networks.append(PassportNetwork(id: index, name: PassportCommand.decode(parts[2]),
                                         ssid: PassportCommand.decode(parts[3]), current: index == connectedNetwork))
        case "NETS":
            guard line == "NETS END", networks.count == networkCount else { throw ProvisioningFailure.invalidResponse }
        case "AGENTS" where parts.count == 4:
            guard UInt(parts[1]) == generation, let count = Int(parts[2]), (0...4).contains(count),
                  let current = Int(parts[3]), current >= -1, current < count else {
                throw ProvisioningFailure.invalidResponse
            }
            agentCount = count
            activeAgent = current
            agents = []
        case "AGENT":
            guard parts.count >= 5, let index = Int(parts[1]), index == agents.count,
                  agents.count < agentCount else { throw ProvisioningFailure.invalidResponse }
            agents.append(PassportAgent(id: index, name: PassportCommand.decode(parts[2]),
                                     url: PassportCommand.decode(parts[3]), state: parts[4], current: index == activeAgent))
        case "AGENTS":
            guard line == "AGENTS END", agents.count == agentCount else { throw ProvisioningFailure.invalidResponse }
        default: break
        }
    }
}

struct PhoneWiFi: Equatable {
    var ssid = ""
    var password = ""
    var open = false

    mutating func selectNetwork(_ ssid: String) {
        guard self.ssid != ssid else { return }
        self = PhoneWiFi(ssid: ssid)
    }

    func validation(saved: [PassportNetwork]) -> String? {
        guard !ssid.isEmpty, ssid.utf8.count <= 32, !ssid.contains(where: { $0.isNewline || $0 == "\0" }) else {
            return "Wi-Fi 名称需为 1–32 字节。"
        }
        if password.isEmpty {
            return open || saved.contains(where: { $0.ssid == ssid }) ? nil : "请输入密码，或明确选择无密码网络。"
        }
        let isHex = password.utf8.count == 64 && password.allSatisfy { $0.isASCII && $0.isHexDigit }
        guard (8...63).contains(password.utf8.count) || isHex,
              !password.contains(where: { $0.isNewline || $0 == "\0" }) else {
            return "Wi-Fi 密码需为 8–63 字节，或 64 位十六进制密钥。"
        }
        return nil
    }

    func command(generation: UInt) -> String {
        "SAVE \(generation) \(PassportCommand.encode(ssid)) \(PassportCommand.encode(password))"
    }
}

struct PhonePair: Equatable {
    var name = ""
    var url = ""
    var code = ""
}

struct PhoneForm: Equatable {
    var wifi = PhoneWiFi()
    var pair = PhonePair()
    var language = "zh"
}

struct PhoneDraft {
    var value = PhoneForm()
    var saved = PhoneForm()
    var dirty: Bool { value != saved }

    mutating func receive(_ snapshot: PassportSnapshot) {
        let ssid = snapshot.networks.first(where: \.current)?.ssid
            ?? snapshot.networks.first(where: { $0.ssid == saved.wifi.ssid })?.ssid ?? ""
        let wifi = ssid == saved.wifi.ssid ? saved.wifi : PhoneWiFi(ssid: ssid)
        if value.wifi == saved.wifi { value.wifi = wifi }
        saved.wifi = wifi
        if value.language == saved.language { value.language = snapshot.language }
        saved.language = snapshot.language
    }

    mutating func discard() { value = saved }
}

enum PhoneMutation {
    case wifi(PhoneWiFi), language(String), pairing(PhonePair, name: String, code: String)

    func command(generation: UInt) -> String {
        switch self {
        case .wifi(let value): return value.command(generation: generation)
        case .language(let value): return "LANG \(generation) \(value)"
        case .pairing(_, let name, let code):
            return "AGENT \(generation) \(PassportCommand.encode(name)) \(PassportCommand.encode(code))"
        }
    }

    func accept(into draft: inout PhoneDraft) {
        switch self {
        case .wifi(let value): draft.saved.wifi = value
        case .language(let value): draft.saved.language = value
        case .pairing(let value, _, _): draft.saved.pair = value
        }
    }
}
