import Foundation

/// 这个 App 自己记住的 Wi-Fi 密码。放在本机文件里，选中就填上，不再问登录钥匙串的密码。
enum WifiMemory {
    static func ssids() -> Set<String> { Set(load().filter { !$0.value.isEmpty }.keys) }
    private static var file: URL {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("AI Passport OpenClaw", isDirectory: true)
        try? FileManager.default.createDirectory(at: base, withIntermediateDirectories: true)
        return base.appendingPathComponent("wifi.json")
    }

    static func password(for ssid: String) -> String? {
        guard let password = load()[ssid], !password.isEmpty else { return nil }
        return password
    }

    static func remember(_ password: String, for ssid: String) {
        guard !ssid.isEmpty else { return }
        var all = load()
        all[ssid] = password
        save(all)
    }

    static func forget(_ ssid: String) {
        var all = load()
        all.removeValue(forKey: ssid)
        save(all)
    }

    private static func load() -> [String: String] {
        guard let data = try? Data(contentsOf: file),
              let all = try? JSONDecoder().decode([String: String].self, from: data) else { return [:] }
        return all
    }

    private static func save(_ all: [String: String]) {
        guard let data = try? JSONEncoder().encode(all) else { return }
        try? data.write(to: file, options: .atomic)
        try? FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: file.path)
    }
}
