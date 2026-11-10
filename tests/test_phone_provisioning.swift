import Foundation

@main struct PhoneProvisioningTests {
    static func main() throws {
        var networks: [NearbyNetwork] = []
        precondition(NearbyNetwork.receive("NET -65 Office Wi-Fi", into: &networks))
        precondition(NearbyNetwork.receive("NET -45 Test iPhone", into: &networks))
        precondition(networks.allSatisfy { !$0.personalHotspot && $0.symbol == "wifi" })
        precondition(NearbyNetwork.receive("NET_KIND hotspot Test%20iPhone", into: &networks))
        precondition(networks[1].personalHotspot && networks[1].symbol == "personalhotspot")
        precondition(NearbyNetwork.receive("NET -35 Test iPhone", into: &networks))
        precondition(networks.count == 2 && networks[1].personalHotspot && networks[1].rssi == -35)
        precondition(!NearbyNetwork.receive("NET_KIND unknown Office%20Wi-Fi", into: &networks))
        precondition(!NearbyNetwork.receive("NET_KIND hotspot Missing", into: &networks))
        precondition(!NearbyNetwork.receive("NET invalid Missing", into: &networks))
        precondition(!NearbyNetwork.receive("SCAN END", into: &networks))
        precondition(!networks[0].personalHotspot)
        let source = "NET -54 办公 Wi-Fi\r\nNETS END\n"
        let bytes = Data(source.utf8)
        for boundary in 0...bytes.count {
            var buffer = BLELineBuffer()
            let first = try buffer.append(bytes.prefix(boundary))
            let last = try buffer.append(bytes.dropFirst(boundary))
            precondition(first + last == ["NET -54 办公 Wi-Fi", "NETS END"])
        }
        var invalid = BLELineBuffer()
        do {
            _ = try invalid.append(Data([255, 10]))
            preconditionFailure("Invalid UTF-8 was accepted")
        } catch ProvisioningFailure.invalidResponse {}
        var oversized = BLELineBuffer()
        do {
            _ = try oversized.append(Data(repeating: 65, count: 8193))
            preconditionFailure("An unbounded response was accepted")
        } catch ProvisioningFailure.tooLong {}
        for maximum in [20, 64, 185, 512] {
            let command = "SAVE 42 " + PassportCommand.encode(String(repeating: "中", count: 10)) + " " + String(repeating: "%21", count: 64)
            var writer = BLECommandWriter(command)
            var sent = Data()
            while let fragment = writer.next(maximum: maximum) {
                precondition(fragment.count <= min(256, maximum))
                precondition(writer.next(maximum: maximum) == nil)
                sent.append(fragment)
                writer.acknowledge()
            }
            precondition(writer.complete)
            precondition(sent == Data((command + "\n").utf8))
        }
        precondition(PassportCommand.encode("中 文%\n").allSatisfy(\.isASCII))
        precondition(PassportCommand.decode(PassportCommand.encode("中 文%")) == "中 文%")

        var snapshot = PassportSnapshot()
        try snapshot.receive("HELLO ai-passport-openclaw 42 1 1 en build-id device-control ble-secure scan-live")
        precondition(snapshot.language == "en" && snapshot.generation == 42 && snapshot.secureBLE && snapshot.liveScan)
        try snapshot.receive("NETS 42 1 0")
        try snapshot.receive("SAVED 0 Office Office%20Wi-Fi")
        try snapshot.receive("NETS END")
        try snapshot.receive("AGENTS 42 1 0")
        try snapshot.receive("AGENT 0 OpenClaw wss%3A%2F%2Fgateway.example pending")
        try snapshot.receive("ROUTE 0 direct - 0 0 chacha20-ietf-poly1305")
        try snapshot.receive("AGENTS END")
        precondition(snapshot.networks.first?.ssid == "Office Wi-Fi")
        precondition(snapshot.networks.first?.current == true)
        precondition(snapshot.agents.first?.current == true)
        var empty = PassportSnapshot()
        try empty.receive("HELLO ai-passport-openclaw 0 0 0 zh build-id ble-secure")
        try empty.receive("NETS 0 0 -1")
        try empty.receive("NETS END")
        try empty.receive("AGENTS 0 0 -1")
        try empty.receive("AGENTS END")
        do {
            try empty.receive("NETS 1 0 -1")
            preconditionFailure("A stale snapshot was accepted")
        } catch ProvisioningFailure.invalidResponse {}
        do {
            try empty.receive("NETS 0 1 0")
            try empty.receive("NETS END")
            preconditionFailure("An incomplete snapshot was accepted")
        } catch ProvisioningFailure.invalidResponse {}
        var legacy = PassportSnapshot()
        try legacy.receive("HELLO ai-passport-openclaw 0 0 0 zh build-id device-control")
        precondition(!legacy.secureBLE && !legacy.liveScan)

        precondition(PhoneWiFi(ssid: "Office Wi-Fi").validation(saved: snapshot.networks) == nil)
        precondition(PhoneWiFi(ssid: "New Wi-Fi").validation(saved: []) != nil)
        precondition(PhoneWiFi(ssid: "Open Wi-Fi", open: true).validation(saved: []) == nil)
        precondition(PhoneWiFi(ssid: String(repeating: "中", count: 11), password: "test-pass").validation(saved: []) != nil)
        precondition(PhoneWiFi(ssid: "Office", password: "short").validation(saved: []) != nil)
        precondition(PhoneWiFi(ssid: "Office", password: String(repeating: "a", count: 64)).validation(saved: []) == nil)
        precondition(PhoneWiFi(ssid: "Office", password: String(repeating: "z", count: 64)).validation(saved: []) != nil)

        let notFound = ProvisioningRejection.message(for: "ERR join notfound")
        precondition(notFound.contains("未发现") && notFound.contains("最大兼容性"))
        precondition(notFound.contains("个人热点") && notFound.contains("亮屏"))
        precondition(ProvisioningRejection.message(for: "ERR join pass").contains("认证未通过"))
        precondition(ProvisioningRejection.message(for: "ERR join security").contains("安全模式不兼容"))
        precondition(ProvisioningRejection.message(for: "ERR join timeout").contains("IP 地址超时"))
        precondition(ProvisioningRejection.message(for: "ERR stale").contains("重新读取"))
        precondition(ProvisioningRejection.message(for: "ERR busy").contains("通话"))
        precondition(ProvisioningRejection.message(for: "ERR join fail").contains("连接 Wi-Fi 失败"))
        precondition(ProvisioningRejection.message(for: "ERR full").contains("full"))
        for response in ["ERR join notfound", "ERR join pass", "ERR join security", "ERR join timeout", "ERR stale", "ERR busy"] {
            precondition(ProvisioningRejection.message(for: response).contains("输入内容仍保留"))
        }

        var selectedNetwork = PhoneWiFi(ssid: "Test iPhone", password: "test-password")
        selectedNetwork.selectNetwork("Test iPhone")
        precondition(selectedNetwork.password == "test-password")
        selectedNetwork.selectNetwork("Saved iPhone")
        precondition(selectedNetwork == PhoneWiFi(ssid: "Saved iPhone"))
        selectedNetwork = PhoneWiFi(ssid: "Guest", open: true)
        selectedNetwork.selectNetwork("Test iPhone")
        precondition(selectedNetwork == PhoneWiFi(ssid: "Test iPhone"))

        var draft = PhoneDraft()
        draft.receive(snapshot)
        precondition(!draft.dirty && draft.value.language == "en")
        draft.value.wifi = PhoneWiFi(ssid: "Phone Hotspot", password: "test-pass")
        draft.receive(snapshot)
        precondition(draft.value.wifi.password == "test-pass" && draft.dirty)
        let write = PhoneMutation.wifi(draft.value.wifi)
        precondition(write.command(generation: 42) == "SAVE 42 Phone%20Hotspot test-pass")
        write.accept(into: &draft)
        var newSnapshot = PassportSnapshot()
        try newSnapshot.receive("HELLO ai-passport-openclaw 43 1 0 en build-id ble-secure")
        try newSnapshot.receive("NETS 43 1 0")
        try newSnapshot.receive("SAVED 0 Phone%20Hotspot Phone%20Hotspot")
        try newSnapshot.receive("NETS END")
        draft.receive(newSnapshot)
        precondition(draft.value.wifi.password == "test-pass" && !draft.dirty)
        try newSnapshot.receive("NETS 43 1 -1")
        try newSnapshot.receive("SAVED 0 Phone%20Hotspot Phone%20Hotspot")
        try newSnapshot.receive("NETS END")
        draft.receive(newSnapshot)
        precondition(draft.value.wifi.ssid == "Phone Hotspot" && draft.value.wifi.password == "test-pass")
        draft.value.language = "zh"
        draft.value.wifi.password = "new-pass"
        PhoneMutation.language("zh").accept(into: &draft)
        precondition(draft.dirty && draft.value.wifi.password == "new-pass")
        draft.discard()
        precondition(!draft.dirty && draft.value.wifi.password == "test-pass")

        draft.value.pair = PhonePair(name: "Assistant", url: "wss://gateway.example", code: "pending")
        let pendingPair = draft.value.pair
        let savedLanguage = draft.value.language
        draft.value.wifi = PhoneWiFi(ssid: "我的 iPhone", password: "hotspot-pass")
        precondition(draft.value.wifi.validation(saved: newSnapshot.networks) == nil)
        precondition(draft.value.wifi.command(generation: 43) == "SAVE 43 %E6%88%91%E7%9A%84%20iPhone hotspot-pass")
        precondition(draft.dirty && draft.value.pair == pendingPair && draft.value.language == savedLanguage)
        draft.receive(newSnapshot)
        precondition(draft.value.wifi.ssid == "我的 iPhone" && draft.value.wifi.password == "hotspot-pass")
        precondition(draft.value.pair == pendingPair)
        precondition(PhoneWiFi(ssid: "Phone Hotspot").validation(saved: newSnapshot.networks) == nil)
        precondition(PhoneWiFi(ssid: "Another iPhone").validation(saved: newSnapshot.networks) != nil)
        draft.discard()
        precondition(draft.value.wifi.ssid == "Phone Hotspot" && !draft.dirty)
        print("Phone provisioning tests: PASS")
    }
}
