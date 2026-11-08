import Foundation

/// Shadowsocks route for one OpenClaw gateway (aes-256-cfb). Off means the
/// badge connects to that gateway directly.
struct ProxySettings: Equatable {
    var enabled = false
    var host = ""
    var port: UInt16 = 0
    var secret = ""
    var hasSecret = false

    var valid: Bool {
        if !enabled { return true }
        let name = host.trimmingCharacters(in: .whitespaces)
        let labels = name.split(separator: ".", omittingEmptySubsequences: false)
        let hostOK = !name.isEmpty && name.count < 64 && name.utf8.allSatisfy {
            ($0 >= 48 && $0 <= 57) || ($0 >= 65 && $0 <= 90) || ($0 >= 97 && $0 <= 122) || $0 == 45 || $0 == 46
        } && labels.allSatisfy { !$0.isEmpty && $0.count <= 63 }
        return port > 0 && hostOK && (hasSecret || !secret.isEmpty)
    }
}
