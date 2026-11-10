import Darwin
import Foundation

struct MacAgent: Identifiable, Equatable {
    var id: Int
    var name: String
    var url: String
    /// ready 已拿到自己的凭据，pending 还拿着一次性配对码，none 只有地址。
    var state: String
    var current: Bool
    /// Shadowsocks route to this gateway; disabled means direct.
    var route = ProxySettings()
}

struct PassportSees: Identifiable, Equatable {
    var id: String { ssid }
    var ssid: String
    var rssi: Int
}

enum WriteResult: Equatable {
    case writing
    case saved
    case failed(String)
}

enum WriteSection { case wifi, openClaw, route, volume, language, firmware }

enum TryResult: Equatable {
    case trying
    case ok
    case failed(String)
}

/// 和 AI Passport 说同一套配置命令。配置只存在设备上，这里只读出来、写回去。
final class PassportSocket: ObservableObject {
    @Published var connected = false
    /// usb 是工牌插在这台 Mac 上，socket 是本机替身。
    @Published var linkKind = "none"
    /// 工牌自己的 USB 口已经出现。配置还没通时也亮。
    @Published var usbPresent = false
    @Published var generation: UInt = 0
    @Published var language = "zh"
    @Published var agents: [MacAgent] = []
    @Published var saved: [String] = []
    @Published var savedCurrent: String?
    @Published var supportsRoute = false
    @Published var supportsDeviceControl = false
    @Published var device: PassportDeviceStatus?
    @Published var deviceCheck: PassportDeviceCheck?
    @Published var loaded = false
    @Published var configurationRevision: UInt = 0
    @Published var build = "-"
    @Published var sees: [PassportSees] = []
    @Published var result: WriteResult?
    @Published var writeSection: WriteSection?
    @Published var tried: TryResult?
    @Published var writeStatus = ""
    @Published var usbPort: String?

    private let path: String
    private var connection: ConfigConnection?
    private var connectionID = UUID()
    private let transportQueue = DispatchQueue(label: "ai.passport.config.transport")
    private var operationTimer: Timer?
    private var checkTimer: Timer?
    private var activeAgent = -1
    private var connectedNet = -1
    private var netsDone = false
    private var agentsDone = false
    private var savedRows: [String] = []
    private var agentRows: [MacAgent] = []
    private var seesRows: [PassportSees] = []
    private var retry: Timer?
    private var probing = false
    /// 现在连的是本机替身。工牌的 USB 口出现后要换过去。
    private var usingSocket = false
    /// 正在刷固件。这段时间不能再打开 USB 口。
    private var flashing = false

    init(path: String) {
        self.path = path
        connect()
        retry = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { [weak self] _ in
            guard let self else { return }
            self.usbPresent = SerialPort.candidates().contains { $0.contains("usbmodem") }
            if !self.flashing && (!self.connected || self.usingSocket) {
                self.connect()
            }
            if self.connected && self.supportsDeviceControl && !self.flashing && self.result != .writing &&
                self.tried != .trying && self.deviceCheck?.running != true { self.send("DEVICE") }
        }
    }

    var currentAgent: MacAgent? { agents.first { $0.current } }
    var configurationBusy: Bool { device?.busy == true || deviceCheck?.running == true }

    func setVolume(_ percent: Int) {
        guard loaded, supportsDeviceControl, !configurationBusy, result != .writing,
              tried != .trying, let device, (0...100).contains(percent) else { return }
        writeSection = .volume
        result = .writing
        writeStatus = "正在保存音量…"
        armTimeout()
        send("VOLUME \(device.volume) \(percent)")
    }

    func checkDevice() {
        guard loaded, supportsDeviceControl, !configurationBusy, result != .writing, tried != .trying else { return }
        deviceCheck = .checking
        checkTimer?.invalidate()
        checkTimer = Timer.scheduledTimer(withTimeInterval: 120, repeats: false) { [weak self] _ in
            self?.cancelDeviceCheck()
        }
        send("CHECK")
    }

    func cancelDeviceCheck() {
        guard deviceCheck?.running == true else { return }
        deviceCheck = .cancelling
        send("CHECK_CANCEL")
        checkTimer?.invalidate()
        checkTimer = Timer.scheduledTimer(withTimeInterval: 10, repeats: false) { [weak self] _ in
            self?.deviceCheck = .failed("设备尚未确认检测已停止，正在重新读取状态。")
            self?.send("DEVICE")
        }
    }

    /// 刷固件前放开 USB 口。刷完再连。
    func pauseForFlash(_ completion: @escaping () -> Void) {
        flashing = true
        writeStatus = "正在更新固件…"
        dropCurrent()
        // A pending probe is already ahead of this block on the same queue.
        transportQueue.async { DispatchQueue.main.async(execute: completion) }
    }

    func resumeAfterFlash(_ completion: @escaping (Bool) -> Void = { _ in }) {
        flashing = false
        writeStatus = "固件已更新，正在重新连接…"
        // esptool 的 reset 后，USB JTAG 口要等固件重新启动才会再次应答。
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) { [weak self] in
            guard let self else { completion(false); return }
            self.connect()
            self.waitForUSB(completion, attempts: 0)
        }
    }

    private func waitForUSB(_ completion: @escaping (Bool) -> Void, attempts: Int) {
        if connected && linkKind == "usb" && loaded && build.count == 16 {
            completion(true)
            return
        }
        if attempts >= 120 {
            completion(false)
            return
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.25) { [weak self] in
            guard let self else { completion(false); return }
            self.connect()
            self.waitForUSB(completion, attempts: attempts + 1)
        }
    }

    func connect() {
        guard !flashing, !probing, !connected || usingSocket else { return }
        probing = true
        let attempt = connectionID
        transportQueue.async {
            let serial = SerialPort.openResponding()
            DispatchQueue.main.async {
                self.probing = false
                guard !self.flashing, attempt == self.connectionID else {
                    if let serial { Darwin.close(serial.0) }
                    return
                }
                if let serial {
                    self.dropCurrent()
                    self.usingSocket = false
                    self.linkKind = "usb"
                    self.usbPort = serial.1
                    self.use(serial.0)
                    self.handle(serial.2)
                    self.reload()
                    self.scan()
                    return
                }
                if !self.connected {
                    self.connectSocket()
                }
            }
        }
    }

    private func use(_ handle: Int32) {
        let link = ConfigConnection(fd: handle)
        connection = link
        let id = connectionID
        connected = true
        link.start(line: { [weak self] line in
            DispatchQueue.main.async {
                guard let self, self.connectionID == id else { return }
                self.handle(line)
            }
        }, ended: { [weak self] in
            DispatchQueue.main.async {
                guard let self, self.connectionID == id else { return }
                self.dropCurrent()
                self.failPending("USB 连接已断开，请重新连接后再试")
            }
        })
    }

    private func dropCurrent() {
        connectionID = UUID()
        if let link = connection { transportQueue.async { link.stop() } }
        connection = nil
        connected = false
        loaded = false
        netsDone = false
        agentsDone = false
        build = "-"
        supportsRoute = false
        supportsDeviceControl = false
        device = nil
        if deviceCheck?.running == true { deviceCheck = .failed("USB 连接已断开，请重新连接后检测。") }
        checkTimer?.invalidate()
        usbPort = nil
        linkKind = "none"
    }

    private func connectSocket() {
        guard !connected, !flashing else { return }
        let handle = socket(AF_UNIX, SOCK_STREAM, 0)
        var addr = sockaddr_un()
        addr.sun_family = sa_family_t(AF_UNIX)
        path.withCString { bytes in
            withUnsafeMutablePointer(to: &addr.sun_path) { dest in
                _ = strcpy(dest, bytes)
            }
        }
        let size = socklen_t(MemoryLayout<sockaddr_un>.size)
        let ok = withUnsafePointer(to: &addr) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { Darwin.connect(handle, $0, size) }
        }
        if ok != 0 {
            close(handle)
            return
        }
        usingSocket = true
        linkKind = "socket"
        use(handle)
        reload()
        send("SCAN")
    }

    func reload() {
        send("HELLO")
        send("NETS")
        send("AGENTS")
    }

    func scan() {
        guard !configurationBusy else { return }
        send("SCAN")
    }

    func setLanguage(_ lang: String) {
        guard loaded, !configurationBusy, result != .writing, tried != .trying else { return }
        writeSection = .language
        result = .writing
        writeStatus = "正在保存屏幕语言…"
        armTimeout()
        send("LANG \(generation) \(lang)")
    }

    func useAgent(_ agent: MacAgent) {
        guard loaded, !configurationBusy, result != .writing, tried != .trying else { return }
        writeSection = .openClaw
        result = .writing
        writeStatus = "正在切换 OpenClaw…"
        armTimeout()
        send("USE \(generation) \(agent.id)")
    }

    func dropAgent(_ agent: MacAgent) {
        guard !configurationBusy else { return }
        send("DROP \(generation) \(agent.id)")
    }

    /// saved 的顺序就是设备上的序号。
    func forget(ssid: String) {
        guard !configurationBusy else { return }
        guard let index = saved.firstIndex(of: ssid) else { return }
        send("FORGET \(generation) \(index)")
    }

    /// 让 AI Passport 试连一下，试完回到原来的网络，什么都不保存。
    func tryWifi(ssid: String, password: String) {
        guard loaded, !configurationBusy, result != .writing, tried != .trying else { return }
        tried = .trying
        armTimeout()
        send(["TRY", enc(ssid), enc(password)].joined(separator: " "))
    }

    /// 只写入 Wi-Fi。密码为空时，设备沿用已经保存的密码。
    func saveWiFi(ssid: String, password: String) {
        guard loaded, !configurationBusy, result != .writing, tried != .trying else { return }
        writeSection = .wifi
        result = .writing
        writeStatus = "正在写入 Wi-Fi…"
        armTimeout()
        send(["SAVE", "\(generation)", enc(ssid), enc(password)].joined(separator: " "))
    }

    /// 设置某一套 OpenClaw 的连接方式：直连，或经 Shadowsocks。密码留空沿用设备上已存的。
    func setRoute(_ agent: MacAgent, route: ProxySettings) {
        guard loaded, !configurationBusy, result != .writing, tried != .trying else { return }
        writeSection = .route
        guard route.valid else { result = .failed("proxy"); return }
        guard supportsRoute else { result = .failed("设备固件尚不支持按助手设置代理，请先更新固件。"); return }
        result = .writing
        writeStatus = "正在写入连接方式…"
        armTimeout()
        if route.enabled {
            send(["ROUTE", "\(generation)", "\(agent.id)", "ss", enc(route.host.trimmingCharacters(in: .whitespaces)),
                  "\(route.port)", route.secret.isEmpty ? "-" : enc(route.secret)].joined(separator: " "))
        } else {
            send("ROUTE \(generation) \(agent.id) direct")
        }
    }

    /// 只写入 OpenClaw 配对信息，不触碰 Wi-Fi。
    func saveAgent(setupCode: String, name: String) {
        guard loaded, !configurationBusy, result != .writing, tried != .trying else { return }
        writeSection = .openClaw
        result = .writing
        writeStatus = "正在写入 OpenClaw…"
        armTimeout()
        send(["AGENT", "\(generation)", enc(name), enc(setupCode)]
            .joined(separator: " "))
    }

    private func enc(_ text: String) -> String {
        var allowed = CharacterSet(charactersIn: "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")
        allowed.insert(charactersIn: "-_.")
        return text.addingPercentEncoding(withAllowedCharacters: allowed) ?? text
    }

    private func dec(_ text: String) -> String {
        text.removingPercentEncoding ?? text
    }

    private func send(_ line: String) {
        guard let link = connection else {
            failPending("USB 配置连接尚未就绪")
            return
        }
        let id = connectionID
        transportQueue.async {
            if !link.send(line) {
                DispatchQueue.main.async {
                    guard self.connectionID == id else { return }
                    self.failPending("写入 USB 失败，请检查数据线后重试")
                }
            }
        }
    }

    private func armTimeout() {
        operationTimer?.invalidate()
        operationTimer = Timer.scheduledTimer(withTimeInterval: 45, repeats: false) { [weak self] _ in
            self?.failPending("设备未在 45 秒内确认，请重试；表单已保留")
        }
    }

    private func failPending(_ reason: String) {
        operationTimer?.invalidate()
        if result == .writing { result = .failed(reason) }
        if tried == .trying { tried = .failed(reason) }
        if deviceCheck?.running == true {
            checkTimer?.invalidate()
            deviceCheck = .failed(reason)
        }
    }

    private func handle(_ line: String) {
        let parts = line.split(separator: " ", omittingEmptySubsequences: false).map(String.init)
        guard let head = parts.first else { return }
        switch head {
        case "HELLO" where parts.count >= 6:
            if let updated = UInt(parts[2]), updated != generation, deviceCheck?.running != true { deviceCheck = nil }
            generation = UInt(parts[2]) ?? generation
            language = parts[5]
            if parts.count >= 7, parts[6].count == 16,
               parts[6].utf8.allSatisfy({ ($0 >= 48 && $0 <= 57) || ($0 >= 97 && $0 <= 102) }) {
                build = parts[6]
            }
            supportsRoute = parts.dropFirst(7).contains("gateway-route")
            let supported = parts.dropFirst(7).contains("device-control")
            if supported && !supportsDeviceControl { send("DEVICE") }
            supportsDeviceControl = supported
        case "DEVICE":
            if let status = PassportDeviceStatus(line: line) {
                let changed = status.generation.map { $0 != generation } == true ||
                    status.network.map { $0 != connectedNet } == true ||
                    status.agent.map { $0 != activeAgent } == true
                device = status
                if changed && result != .writing && deviceCheck?.running != true { reload() }
            }
        case "CHECK":
            guard deviceCheck?.running == true else { return }
            if parts.count >= 2, parts[1] == "started" { return }
            checkTimer?.invalidate()
            if parts.count >= 2, parts[1] == "cancelled" { deviceCheck = .cancelled }
            else if let report = PassportCheckReport(line: line) { deviceCheck = .report(report) }
            else if parts.dropFirst().first == "simulated" { deviceCheck = .failed("本机模拟设备不连接真实网关，请连接 Passport 后检测。") }
            else { deviceCheck = .failed(parts.dropFirst().first == "busy" ? "设备正在使用，请先结束通话或提问。" : "检测未能启动，请检查设备网络和配对状态。") }
            send("DEVICE")
        case "NETS" where parts.count >= 4:
            generation = UInt(parts[1]) ?? generation
            connectedNet = Int(parts[3]) ?? -1
            savedRows = []
        case "NETS":
            saved = savedRows
            savedCurrent = savedRows.indices.contains(connectedNet) ? savedRows[connectedNet] : nil
            netsDone = true
            loaded = netsDone && agentsDone
        case "SAVED" where parts.count >= 4:
            savedRows.append(dec(parts[3]))
        case "AGENTS" where parts.count >= 4:
            generation = UInt(parts[1]) ?? generation
            activeAgent = Int(parts[3]) ?? -1
            agentRows = []
        case "AGENTS":
            agents = agentRows
            agentsDone = true
            loaded = netsDone && agentsDone
            configurationRevision &+= 1
        case "AGENT" where parts.count >= 5:
            let index = Int(parts[1]) ?? agentRows.count
            agentRows.append(MacAgent(id: index, name: dec(parts[2]), url: dec(parts[3]),
                                      state: parts[4], current: index == activeAgent))
        case "ROUTE" where parts.count >= 6:
            if let index = Int(parts[1]), let row = agentRows.firstIndex(where: { $0.id == index }) {
                agentRows[row].route = ProxySettings(enabled: parts[2] == "ss", host: parts[3] == "-" ? "" : parts[3],
                                                     port: UInt16(parts[4]) ?? 0, hasSecret: parts[5] == "1")
            }
        case "SCAN" where parts.count >= 2 && parts[1] == "END":
            sees = seesRows
        case "SCAN":
            seesRows = []
        case "NET" where parts.count >= 3:
            seesRows.append(PassportSees(ssid: parts.dropFirst(2).joined(separator: " "),
                                         rssi: Int(parts[1]) ?? -90))
        case "TRY" where parts.count >= 2:
            operationTimer?.invalidate()
            tried = parts[1] == "ok" ? .ok : .failed(parts.count >= 3 ? parts[2] : "fail")
        case "OK":
            if result == .writing, parts.count >= 2,
               (writeSection == .wifi && parts[1] == "saved") ||
               (writeSection == .route && parts[1] == "route") ||
               (writeSection == .volume && parts[1] == "volume") ||
               (writeSection == .language && parts[1] == "lang") ||
               (writeSection == .openClaw && (parts[1] == "agent" || parts[1] == "use")) {
                operationTimer?.invalidate()
                result = .saved
            }
            reload()
        case "ERR":
            operationTimer?.invalidate()
            if result == .writing {
                // ERR join pass → 只留原因
                let reason = parts.count >= 3 && parts[1] == "join" ? parts[2] : (parts.count >= 2 ? parts[1] : "")
                result = .failed(reason)
            }
            if parts.count >= 2, parts[1] == "stale" {
                reload()
            }
            if deviceCheck?.running == true {
                checkTimer?.invalidate()
                deviceCheck = .failed("设备暂时无法检测，请稍后重试。")
            }
        default:
            break
        }
    }
}
