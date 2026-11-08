import CoreBluetooth
import Foundation

struct BadgeAgent: Identifiable, Equatable {
    var id: Int
    var name: String
    var url: String
    var state: String
    var current: Bool
}

struct BadgeNetwork: Identifiable, Equatable {
    var id: Int
    var name: String
    var ssid: String
    var current: Bool
}

enum BadgeCommand {
    static let service = CBUUID(string: "D1B48E2A-7051-339C-7E4A-104D2E8A1C6B")
    static let rx = CBUUID(string: "D2B48E2A-7051-339C-7E4A-104D2E8A1C6B")
    static let tx = CBUUID(string: "D3B48E2A-7051-339C-7E4A-104D2E8A1C6B")

    static func encode(_ text: String) -> String {
        var allowed = CharacterSet.alphanumerics
        allowed.insert(charactersIn: "-_.")
        return text.addingPercentEncoding(withAllowedCharacters: allowed) ?? text
    }

    static func decode(_ text: String) -> String {
        text.removingPercentEncoding ?? text
    }
}

final class BadgeLink: NSObject, ObservableObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    @Published var status = "打开蓝牙后会寻找 AI Passport"
    @Published var generation: UInt = 0
    @Published var language = "zh"
    @Published var agents: [BadgeAgent] = []
    @Published var networks: [BadgeNetwork] = []

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var rx: CBCharacteristic?
    private var tx: CBCharacteristic?
    private var buffer = ""
    private var activeAgent = -1
    private var connectedNetwork = -1

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: .main)
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state == .poweredOn {
            status = "正在寻找 AI Passport"
            central.scanForPeripherals(withServices: [BadgeCommand.service])
        } else {
            status = "蓝牙没有打开"
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        self.peripheral = peripheral
        status = "正在连接"
        central.stopScan()
        central.connect(peripheral)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        peripheral.delegate = self
        peripheral.discoverServices([BadgeCommand.service])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        status = "没有连上"
        self.peripheral = nil
        central.scanForPeripherals(withServices: [BadgeCommand.service])
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard let service = peripheral.services?.first(where: { $0.uuid == BadgeCommand.service }) else { return }
        peripheral.discoverCharacteristics([BadgeCommand.rx, BadgeCommand.tx], for: service)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        rx = service.characteristics?.first { $0.uuid == BadgeCommand.rx }
        tx = service.characteristics?.first { $0.uuid == BadgeCommand.tx }
        if let tx {
            peripheral.setNotifyValue(true, for: tx)
        }
        status = "已连接"
        send("HELLO")
        send("NETS")
        send("AGENTS")
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard let data = characteristic.value, let text = String(data: data, encoding: .utf8) else { return }
        buffer += text
        while let newline = buffer.firstIndex(of: "\n") {
            let line = String(buffer[..<newline]).trimmingCharacters(in: .whitespaces)
            buffer = String(buffer[buffer.index(after: newline)...])
            handle(line)
        }
    }

    func reload() {
        networks = []
        agents = []
        send("NETS")
        send("AGENTS")
    }

    func saveAgent(name: String, setupCode: String) {
        send(["AGENT", "\(generation)", BadgeCommand.encode(name), BadgeCommand.encode(setupCode)].joined(separator: " "))
    }

    func useAgent(_ agent: BadgeAgent) {
        send("USE \(generation) \(agent.id)")
    }

    func dropAgent(_ agent: BadgeAgent) {
        send("DROP \(generation) \(agent.id)")
    }

    func forgetNetwork(_ network: BadgeNetwork) {
        send("FORGET \(generation) \(network.id)")
    }

    func setLanguage(_ lang: String) {
        language = lang
        send("LANG \(generation) \(lang)")
    }

    func saveNetwork(ssid: String, password: String) {
        send(["SAVE", "\(generation)", BadgeCommand.encode(ssid), BadgeCommand.encode(password)].joined(separator: " "))
    }

    private func send(_ line: String) {
        guard let peripheral, let rx, let data = (line + "\n").data(using: .utf8) else { return }
        peripheral.writeValue(data, for: rx, type: .withResponse)
    }

    private func handle(_ line: String) {
        let parts = line.split(separator: " ").map(String.init)
        guard let head = parts.first else { return }
        if head == "HELLO", parts.count >= 4 {
            generation = UInt(parts[2]) ?? generation
            if parts.count >= 5 { language = parts[4] }
        } else if head == "NETS", parts.count >= 4 {
            generation = UInt(parts[1]) ?? generation
            connectedNetwork = Int(parts[3]) ?? -1
            networks = []
        } else if head == "SAVED", parts.count >= 4 {
            let index = Int(parts[1]) ?? networks.count
            networks.append(BadgeNetwork(
                id: index,
                name: BadgeCommand.decode(parts[2]),
                ssid: BadgeCommand.decode(parts[3]),
                current: index == connectedNetwork
            ))
        } else if head == "AGENTS", parts.count >= 4 {
            generation = UInt(parts[1]) ?? generation
            activeAgent = Int(parts[3]) ?? -1
            agents = []
        } else if head == "AGENT", parts.count >= 5 {
            let index = Int(parts[1]) ?? agents.count
            agents.append(BadgeAgent(
                id: index,
                name: BadgeCommand.decode(parts[2]),
                url: BadgeCommand.decode(parts[3]),
                state: parts[4],
                current: index == activeAgent
            ))
        } else if head == "OK" {
            status = "已写入 AI Passport"
            reload()
        } else if head == "ERR" {
            status = line == "ERR stale" ? "AI Passport 刚被别的设备改过，已重新读取" : line
            if line == "ERR stale" { reload() }
        }
    }
}
