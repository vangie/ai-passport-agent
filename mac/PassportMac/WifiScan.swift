import CoreLocation
import CoreWLAN
import Foundation

struct ScannedWifi: Hashable {
    var ssid: String
    var rssi: Int
    var has24: Bool
    var secure: Bool
}

/// 这台 Mac 能看到的 Wi-Fi。macOS 要定位授权才肯给出网络名。
final class WifiScan: NSObject, ObservableObject, CLLocationManagerDelegate {
    @Published var nearby: [ScannedWifi] = []
    @Published var current: String?
    @Published var locationDenied = false
    @Published var scanning = false
    @Published var canScanSilently = false

    private let location = CLLocationManager()
    private var again = false
    private var scanRequestedOnGrant = false

    override init() {
        super.init()
        location.delegate = self
    }

    func start(scanMac: Bool = true) {
        updateAuthorization()
        if scanMac { scanIfAuthorized() }
    }

    func locationManagerDidChangeAuthorization(_ manager: CLLocationManager) {
        let status = manager.authorizationStatus
        updateAuthorization()
        if isAuthorized(status) && scanRequestedOnGrant {
            scanRequestedOnGrant = false
            // 授权前扫到的网络没有名字，拿到授权再扫一次。
            if scanning { again = true } else { performScan() }
        }
    }

    private func isAuthorized(_ status: CLAuthorizationStatus) -> Bool {
        status == .authorizedAlways
    }

    private func updateAuthorization() {
        let status = location.authorizationStatus
        locationDenied = status == .denied || status == .restricted
        canScanSilently = isAuthorized(status)
    }

    /// Normal startup and refresh must never request permission.
    func scanIfAuthorized() {
        updateAuthorization()
        if canScanSilently { performScan() }
    }

    /// Only the explicit "scan Mac networks" action may ask for location access.
    func scan() {
        updateAuthorization()
        if location.authorizationStatus == .notDetermined {
            scanRequestedOnGrant = true
            location.requestWhenInUseAuthorization()
            return
        }
        if canScanSilently { performScan() }
    }

    private func performScan() {
        guard !scanning else { return }
        scanning = true
        DispatchQueue.global(qos: .userInitiated).async {
            let interface = CWWiFiClient.shared().interface()
            let found = (try? interface?.scanForNetworks(withSSID: nil)) ?? []
            var best: [String: ScannedWifi] = [:]
            for network in found {
                guard let ssid = network.ssid, !ssid.isEmpty else { continue }
                let is24 = network.wlanChannel?.channelBand == .band2GHz
                var item = best[ssid] ?? ScannedWifi(ssid: ssid, rssi: network.rssiValue, has24: false,
                                                     secure: !network.supportsSecurity(.none))
                item.rssi = max(item.rssi, network.rssiValue)
                item.has24 = item.has24 || is24
                best[ssid] = item
            }
            let sorted = best.values.sorted { $0.rssi > $1.rssi }
            let current = interface?.ssid()
            DispatchQueue.main.async {
                self.nearby = sorted
                self.current = current
                self.scanning = false
                if self.again {
                    self.again = false
                    self.scanIfAuthorized()
                }
            }
        }
    }

    /// 读出密码。系统会弹窗请用户允许。
    static func savedPassword(ssid: String) -> String? {
        let result = run("/usr/bin/security",
                         ["find-generic-password", "-D", "AirPort network password", "-a", ssid, "-w"])
        guard result.ok, let text = result.text?.trimmingCharacters(in: .whitespacesAndNewlines),
              !text.isEmpty else { return nil }
        return text
    }

    private static func run(_ tool: String, _ args: [String]) -> (ok: Bool, text: String?) {
        let task = Process()
        task.executableURL = URL(fileURLWithPath: tool)
        task.arguments = args
        let output = Pipe()
        task.standardOutput = output
        task.standardError = Pipe()
        guard (try? task.run()) != nil else { return (false, nil) }
        let data = output.fileHandleForReading.readDataToEndOfFile()
        task.waitUntilExit()
        return (task.terminationStatus == 0, String(data: data, encoding: .utf8))
    }
}
