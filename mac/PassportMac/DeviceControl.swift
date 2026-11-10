import Foundation

struct PassportDeviceStatus: Equatable {
    let volume: Int
    let battery: Int?
    let wifi: Bool
    let busy: Bool
    let sleeping: Bool
    let usbHost: Bool
    let idleSeconds: Int
    let generation: UInt?
    let network: Int?
    let agent: Int?

    init?(line: String) {
        guard line.hasPrefix("DEVICE ") else { return nil }
        let fields = Self.fields(line)
        guard let volume = fields["volume"].flatMap(Int.init), (0...100).contains(volume),
              let battery = fields["battery"].flatMap(Int.init), (-1...100).contains(battery),
              let wifi = Self.flag(fields["wifi"]), let busy = Self.flag(fields["busy"]),
              let sleeping = Self.flag(fields["sleeping"]), let usb = Self.flag(fields["usb_host"]),
              let idle = fields["idle_s"].flatMap(Int.init), (1...3600).contains(idle) else { return nil }
        self.volume = volume
        self.battery = battery < 0 ? nil : battery
        self.wifi = wifi
        self.busy = busy
        self.sleeping = sleeping
        usbHost = usb
        idleSeconds = idle
        generation = fields["generation"].flatMap(UInt.init)
        network = fields["network"].flatMap(Int.init)
        agent = fields["agent"].flatMap(Int.init)
    }

    static func fields(_ line: String) -> [String: String] {
        var result: [String: String] = [:]
        for item in line.split(separator: " ").dropFirst() {
            let pair = item.split(separator: "=", maxSplits: 1)
            if pair.count == 2 { result[String(pair[0])] = String(pair[1]) }
        }
        return result
    }

    private static func flag(_ text: String?) -> Bool? {
        switch text { case "1": return true; case "0": return false; default: return nil }
    }
}

struct PassportCheckReport: Equatable {
    let gateway: Bool
    let ask: Bool
    let plugin: Int
    let call: Int

    init?(line: String) {
        guard line.hasPrefix("CHECK done ") || line.hasPrefix("CHECK failed ") else { return nil }
        let fields = PassportDeviceStatus.fields(line)
        guard let gateway = fields["gateway"], ["0", "1"].contains(gateway),
              let ask = fields["ask"], ["0", "1"].contains(ask),
              let plugin = fields["plugin"].flatMap(Int.init), (0...4).contains(plugin),
              let call = fields["call"].flatMap(Int.init), (0...2).contains(call) else { return nil }
        self.gateway = gateway == "1"
        self.ask = ask == "1"
        self.plugin = plugin
        self.call = call
    }

    var summary: String {
        guard gateway else { return "设备未能连接并认证网关，请检查设备网络、连接方式和配对状态。" }
        let askText = ask ? "提问会话可访问" : "提问会话检查未通过"
        let callText: String
        switch plugin {
        case 1: callText = call == 1 ? "通话服务已配置" : "通话服务尚未就绪，请检查网关的实时语音模型配置"
        case 2: callText = "插件状态不可读取，请检查插件安装和设备权限"
        case 3: callText = "插件协议不兼容，请更新插件"
        default: callText = "插件状态请求未完成，请检查网关连接"
        }
        return "设备网关认证通过 · \(askText)；\(callText)。"
    }
}

enum PassportDeviceCheck: Equatable {
    case checking
    case cancelling
    case report(PassportCheckReport)
    case failed(String)
    case cancelled

    var running: Bool { self == .checking || self == .cancelling }
}
