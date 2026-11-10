import Foundation

struct ConfigurationDraft<Value: Equatable> {
    private(set) var saved: Value
    var value: Value

    init(_ value: Value) {
        saved = value
        self.value = value
    }

    var isDirty: Bool { value != saved }

    mutating func receive(_ latest: Value) {
        if !isDirty { value = latest }
        saved = latest
    }

    mutating func accept(_ written: Value) {
        saved = written
    }

    mutating func discard() {
        value = saved
    }
}

struct WiFiConfiguration: Equatable {
    var ssid: String?
    var password = ""
}

enum ConfigurationSaveStep: Equatable {
    case pairing(key: String, name: String, code: String)
    case route(name: String, value: RouteForm)
    case selection(name: String)
    case language(String)
    case volume(Int)
    case wifi(WiFiConfiguration)
}

struct AgentConfigurationEdit {
    var key: String
    var name: String
    var code: String?
    var route: RouteForm?
}

struct ConfigurationSave {
    let steps: [ConfigurationSaveStep]
    private(set) var completedCount = 0
    private(set) var waitingRevision: UInt?

    static func plan(agents: [AgentConfigurationEdit], selection: String?, language: String?,
                     volume: Int?, wifi: WiFiConfiguration?) -> ConfigurationSave {
        var steps: [ConfigurationSaveStep] = []
        for agent in agents {
            if let code = agent.code { steps.append(.pairing(key: agent.key, name: agent.name, code: code)) }
            if let route = agent.route { steps.append(.route(name: agent.name, value: route)) }
        }
        if let selection { steps.append(.selection(name: selection)) }
        if let language { steps.append(.language(language)) }
        if let volume { steps.append(.volume(volume)) }
        if let wifi { steps.append(.wifi(wifi)) }
        return ConfigurationSave(steps: steps)
    }

    var current: ConfigurationSaveStep? {
        completedCount < steps.count ? steps[completedCount] : nil
    }

    var acknowledgedCount: Int { completedCount + (waitingRevision == nil ? 0 : 1) }

    mutating func acknowledge(revision: UInt) -> Bool {
        guard current != nil, waitingRevision == nil else { return false }
        waitingRevision = revision
        return true
    }

    mutating func readBack(revision: UInt) -> Bool {
        guard let waitingRevision, revision > waitingRevision else { return false }
        completedCount += 1
        self.waitingRevision = nil
        return true
    }
}

enum WiFiListLayout {
    static func height(rowCount: Int, expanded: Bool) -> Double {
        guard rowCount > 0 else { return 68 }
        return Double(min(rowCount, expanded ? 6 : 4) * 50 - 2)
    }
}

struct RouteForm: Equatable {
    var enabled = false
    var host = ""
    var port = ""
    var secret = ""
    var hasSecret = false

    init() {}

    init(_ value: ProxySettings, retainingSecret: String = "") {
        enabled = value.enabled
        host = value.host
        port = value.port == 0 ? "" : String(value.port)
        hasSecret = value.hasSecret
        secret = value.hasSecret ? retainingSecret : ""
    }

    var settings: ProxySettings {
        ProxySettings(enabled: enabled, host: host.trimmingCharacters(in: .whitespaces),
                      port: UInt16(port) ?? 0, secret: secret, hasSecret: hasSecret)
    }

    func acknowledged() -> RouteForm {
        var result = self
        result.host = settings.host
        if enabled && !secret.isEmpty { result.hasSecret = true }
        return result
    }

    func refreshed(_ route: ProxySettings) -> RouteForm {
        RouteForm(route, retainingSecret: route.host == host && route.port == settings.port ? secret : "")
    }
}
