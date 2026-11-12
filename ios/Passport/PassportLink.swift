import CoreBluetooth
import Foundation

struct NearbyPassport: Identifiable {
    let id: UUID
    var name: String
    var rssi: Int
}

final class PassportLink: NSObject, ObservableObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    static let service = CBUUID(string: "D1B48E2A-7051-339C-7E4A-104D2E8A1C6B")
    static let rxUUID = CBUUID(string: "D2B48E2A-7051-339C-7E4A-104D2E8A1C6B")
    static let txUUID = CBUUID(string: "D3B48E2A-7051-339C-7E4A-104D2E8A1C6B")

    @Published private(set) var status = L10n.string("请开启手机蓝牙")
    @Published private(set) var devices: [NearbyPassport] = []
    @Published private(set) var snapshot = PassportSnapshot()
    @Published private(set) var nearby: [NearbyNetwork] = []
    @Published private(set) var networkScanInProgress = false
    @Published private(set) var ready = false
    @Published private(set) var busy = false
    @Published private(set) var scanning = false
    @Published private(set) var connecting = false
    @Published private(set) var error: String?
    @Published private(set) var snapshotRevision: UInt = 0
    @Published private(set) var acknowledged: PhoneMutation?
    @Published private(set) var deviceName = "AI Passport"

    private var central: CBCentralManager!
    private var discovered: [UUID: CBPeripheral] = [:]
    private var peripheral: CBPeripheral?
    private var rx: CBCharacteristic?
    private var tx: CBCharacteristic?
    private var buffer = BLELineBuffer()
    private var writer: BLECommandWriter?
    private var expected = ""
    private var responseComplete = false
    private var rejection: String?
    private var completion: (() -> Void)?
    private var timer: Timer?
    private var candidate = PassportSnapshot()
    private var nearbyRows: [NearbyNetwork] = []
    private var mutations: [PhoneMutation] = []

    override init() {
        super.init()
        #if targetEnvironment(simulator)
        status = L10n.string("模拟器仅预览界面，蓝牙配网需要真实 iPhone")
        #if DEBUG
        if networkPreview != nil {
            let saved = ProcessInfo.processInfo.arguments.contains("--preview-saved-networks")
                ? ["Saved iPhone", "Office Wi-Fi", "Home Wi-Fi"] : ["Saved iPhone"]
            try? snapshot.receive("HELLO ai-passport-openclaw 1 \(saved.count) 0 zh ui-preview ble-secure")
            try? snapshot.receive("NETS 1 \(saved.count) 0")
            for (index, name) in saved.enumerated() {
                let encoded = PassportCommand.encode(name)
                try? snapshot.receive("SAVED \(index) \(encoded) \(encoded)")
            }
            try? snapshot.receive("NETS END")
            try? snapshot.receive("AGENTS 1 0 -1")
            try? snapshot.receive("AGENTS END")
            ready = true
            deviceName = L10n.string("AI Passport · 预览")
            status = L10n.string("模拟数据，不连接真实设备")
        }
        #endif
        #else
        central = CBCentralManager(delegate: self, queue: .main)
        #endif
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state == .poweredOn { search() }
        else {
            disconnect()
            status = central.state == .unauthorized ? L10n.string("请在系统设置中允许蓝牙访问") : L10n.string("请开启手机蓝牙")
        }
    }

    func search() {
        guard let central, central.state == .poweredOn, peripheral == nil else { return }
        error = nil
        devices = []
        discovered = [:]
        scanning = true
        status = L10n.string("正在寻找 AI Passport")
        central.scanForPeripherals(withServices: [Self.service])
        armTimeout(20) { [weak self] in
            guard let self else { return }
            self.central.stopScan()
            self.scanning = false
            self.status = self.devices.isEmpty ? L10n.string("尚未发现设备，请在 Passport「设置」中开启蓝牙") : L10n.string("选择要配置的设备")
        }
    }

    func connect(_ device: NearbyPassport) {
        guard let peripheral = discovered[device.id], self.peripheral == nil else { return }
        timer?.invalidate()
        central.stopScan()
        scanning = false
        connecting = true
        error = nil
        self.peripheral = peripheral
        deviceName = device.name
        status = L10n.string("正在连接 %@", String(describing: device.name))
        central.connect(peripheral)
        armTimeout(30) { [weak self] in self?.fail(L10n.string("连接超时，请靠近设备后重试。")) }
    }

    func disconnect() {
        timer?.invalidate()
        if let peripheral { central?.cancelPeripheralConnection(peripheral) }
        central?.stopScan()
        peripheral = nil
        rx = nil
        tx = nil
        ready = false
        busy = false
        connecting = false
        scanning = false
        writer = nil
        rejection = nil
        completion = nil
        mutations = []
        nearby = []
        networkScanInProgress = false
        buffer = BLELineBuffer()
        status = L10n.string("设备已断开，可重新搜索")
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        discovered[peripheral.identifier] = peripheral
        let name = advertisementData[CBAdvertisementDataLocalNameKey] as? String ?? peripheral.name ?? "AI Passport"
        let device = NearbyPassport(id: peripheral.identifier, name: name, rssi: RSSI.intValue)
        if let index = devices.firstIndex(where: { $0.id == device.id }) { devices[index] = device }
        else { devices.append(device) }
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        guard peripheral == self.peripheral else { return }
        peripheral.delegate = self
        peripheral.discoverServices([Self.service])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        guard peripheral == self.peripheral else { return }
        fail(L10n.string("没有连上设备，请在工牌设置里重新开启蓝牙。"))
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        guard peripheral == self.peripheral else { return }
        fail(L10n.string("蓝牙连接已断开。输入内容仍保留；重新连接后可继续。"))
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard peripheral == self.peripheral else { return }
        guard error == nil, let service = peripheral.services?.first(where: { $0.uuid == Self.service }) else {
            fail(L10n.string("设备没有配置服务，请检查固件。"))
            return
        }
        peripheral.discoverCharacteristics([Self.rxUUID, Self.txUUID], for: service)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        guard peripheral == self.peripheral else { return }
        rx = service.characteristics?.first { $0.uuid == Self.rxUUID }
        tx = service.characteristics?.first { $0.uuid == Self.txUUID }
        guard error == nil, rx?.properties.contains(.write) == true, let tx else {
            fail(L10n.string("设备配置接口不完整，请更新固件。"))
            return
        }
        peripheral.setNotifyValue(true, for: tx)
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        guard peripheral == self.peripheral, characteristic.uuid == Self.txUUID else { return }
        guard error == nil, characteristic.isNotifying else {
            fail(L10n.string("无法订阅设备状态，请重新连接。"))
            return
        }
        status = L10n.string("正在建立加密连接并读取配置")
        readSnapshot()
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        guard peripheral == self.peripheral, characteristic.uuid == Self.rxUUID, writer != nil else { return }
        guard error == nil else {
            fail(L10n.string("蓝牙写入未完成。请确认系统蓝牙配对提示后重连。"))
            return
        }
        writer?.acknowledge()
        sendFragment()
        finishIfReady()
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard peripheral == self.peripheral, characteristic.uuid == Self.txUUID else { return }
        guard error == nil, let data = characteristic.value else {
            fail(L10n.string("读取设备状态失败，请重连。"))
            return
        }
        do {
            for line in try buffer.append(data) { try receive(line) }
        } catch { fail(error.localizedDescription) }
    }

    func reload() {
        guard ready, !busy else { return }
        readSnapshot()
    }

    func scanNetworks() {
        guard ready, !busy else { return }
        error = nil
        nearby = []
        networkScanInProgress = true
        status = L10n.string("正在由工牌扫描 2.4 GHz Wi-Fi")
        nearbyRows = []
        #if DEBUG && targetEnvironment(simulator)
        if let networkPreview {
            busy = true
            let rows = ["NET -40 Test iPhone", "NET_KIND hotspot Test%20iPhone",
                        "NET -65 Office Wi-Fi", "NET -70 iPhone-named router"]
            if networkPreview == "success" || networkPreview == "progressive" {
                for (index, line) in rows.enumerated() {
                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.2 + Double(index) * 0.2) { [weak self] in
                        guard let self else { return }
                        if NearbyNetwork.receive(line, into: &self.nearbyRows) {
                            self.nearby = self.nearbyRows
                        }
                    }
                }
            }
            if networkPreview == "progressive" { return }
            DispatchQueue.main.asyncAfter(deadline: .now() + 1) { [weak self] in
                guard let self else { return }
                if networkPreview == "failure" {
                    self.fail(L10n.string("模拟扫描失败，请重新连接设备。"))
                    return
                }
                self.networkScanInProgress = false
                self.busy = false
                self.status = L10n.string("模拟扫描完成")
            }
            return
        }
        #endif
        request(snapshot.liveScan ? "SCAN LIVE" : "SCAN", ending: "SCAN END", timeout: 60) { [weak self] in
            guard let self else { return }
            self.networkScanInProgress = false
            self.status = self.nearby.isEmpty ? L10n.string("未找到 Wi-Fi，也可以手动输入名称") : L10n.string("选择 Wi-Fi 并填写密码")
        }
    }

    #if DEBUG && targetEnvironment(simulator)
    private var networkPreview: String? {
        let arguments = ProcessInfo.processInfo.arguments
        guard let index = arguments.firstIndex(of: "--preview-networks"),
              arguments.indices.contains(index + 1),
              ["success", "progressive", "empty", "failure"].contains(arguments[index + 1]) else { return nil }
        return arguments[index + 1]
    }
    #endif

    func save(_ steps: [PhoneMutation]) {
        guard ready, !busy, !steps.isEmpty else { return }
        error = nil
        mutations = steps
        saveNext()
    }

    func forget(_ network: PassportNetwork) {
        guard ready, !busy else { return }
        request("FORGET \(snapshot.generation) \(network.id)", ending: "OK forget") { [weak self] in
            self?.readSnapshot()
        }
    }

    private func saveNext() {
        guard let step = mutations.first else {
            status = L10n.string("配置已保存到 AI Passport")
            return
        }
        if case .wifi = step { status = L10n.string("正在连接 Wi-Fi 并保存，最长约 60 秒") }
        else { status = L10n.string("正在保存配置") }
        let ending: String
        switch step {
        case .wifi: ending = "OK saved"
        case .language: ending = "OK lang"
        case .pairing: ending = "OK agent"
        }
        request(step.command(generation: snapshot.generation), ending: ending, timeout: 60) { [weak self] in
            guard let self else { return }
            self.acknowledged = step
            self.mutations.removeFirst()
            self.readSnapshot { self.saveNext() }
        }
    }

    private func readSnapshot(after: (() -> Void)? = nil) {
        candidate = PassportSnapshot()
        request("HELLO", ending: "HELLO ", timeout: 90) { [weak self] in
            guard let self else { return }
            guard self.candidate.secureBLE else {
                self.fail(L10n.string("此固件不支持加密蓝牙配网，请先用 Mac 更新固件。未发送 Wi-Fi 密码。"))
                return
            }
            self.request("NETS", ending: "NETS END") {
                self.request("AGENTS", ending: "AGENTS END") {
                    self.snapshot = self.candidate
                    self.ready = true
                    self.connecting = false
                    self.snapshotRevision += 1
                    self.status = L10n.string("已读取设备配置")
                    after?()
                }
            }
        }
    }

    private func request(_ command: String, ending: String, timeout: TimeInterval = 20, completion: @escaping () -> Void) {
        guard peripheral != nil, rx != nil, writer == nil else { return }
        busy = true
        expected = ending
        responseComplete = false
        rejection = nil
        self.completion = completion
        writer = BLECommandWriter(command)
        armTimeout(timeout) { [weak self] in
            self?.fail(L10n.string("等待设备超时，可能已保存。请重连读取确认后再试，输入内容仍保留。"))
        }
        sendFragment()
    }

    private func sendFragment() {
        guard let peripheral, let rx,
              let fragment = writer?.next(maximum: peripheral.maximumWriteValueLength(for: .withResponse)) else { return }
        peripheral.writeValue(fragment, for: rx, type: .withResponse)
    }

    private func receive(_ line: String) throws {
        if line == "SESSION TAKEN" {
            fail(L10n.string("设备已被其他配置端接管，输入内容仍保留。需要时请手动重新连接。"))
            return
        }
        guard writer != nil, !responseComplete else { return }
        if line.hasPrefix("ERR ") {
            rejection = ProvisioningRejection.message(for: line)
            responseComplete = true
            finishIfReady()
            return
        }
        if expected == "SCAN END" {
            if NearbyNetwork.receive(line, into: &nearbyRows) {
                nearby = nearbyRows
            }
        } else if ["HELLO ", "NETS END", "AGENTS END"].contains(expected) {
            try candidate.receive(line)
        }
        if line == expected || (expected == "HELLO " && line.hasPrefix(expected)) {
            responseComplete = true
            finishIfReady()
        }
    }

    private func finishIfReady() {
        guard responseComplete, writer?.complete == true else { return }
        timer?.invalidate()
        writer = nil
        busy = false
        let finished = completion
        completion = nil
        if let rejection {
            self.rejection = nil
            mutations = []
            networkScanInProgress = false
            if ready {
                error = rejection
                status = L10n.string("操作未完成，蓝牙仍已连接")
            } else {
                fail(rejection)
            }
            return
        }
        finished?()
    }

    private func fail(_ message: String) {
        disconnect()
        error = message
        status = L10n.string("操作未完成")
    }

    private func armTimeout(_ interval: TimeInterval, action: @escaping () -> Void) {
        timer?.invalidate()
        timer = Timer.scheduledTimer(withTimeInterval: interval, repeats: false) { _ in action() }
    }
}
