import AppKit
import SwiftUI

private enum Reach: Int { case near, away, only5 }

private struct WifiChoice: Identifiable, Equatable {
    var id: String { ssid }
    var ssid: String
    var rssi: Int?
    var reach: Reach
    var secure: Bool
}

private struct RouteForm: Equatable {
    var enabled = false
    var host = ""
    var port = ""
    var secret = ""
    var hasSecret = false
    init() {}
    init(_ value: ProxySettings) {
        enabled = value.enabled; host = value.host
        port = value.port == 0 ? "" : String(value.port)
        hasSecret = value.hasSecret
    }
    var settings: ProxySettings {
        ProxySettings(enabled: enabled, host: host.trimmingCharacters(in: .whitespaces),
                      port: UInt16(port) ?? 0, secret: secret, hasSecret: hasSecret)
    }
}

struct PassportMacView: View {
    @ObservedObject var link: BadgeSocket
    @ObservedObject var wifi: WifiScan
    @ObservedObject var incoming: ConfigurationInbox
    @State private var importedName: String?
    @State private var ssid: String?
    @State private var password = ""
    @State private var passwordDrafts: [String: String] = [:]
    @State private var routeForm = RouteForm()
    @State private var remembered: Set<String> = []
    @State private var showPassword = false
    @State private var readingPassword = false
    @State private var keychainNote: String?
    @State private var showAll = false
    @State private var manualNetwork = false
    @State private var manualSSID = ""
    @State private var agentPick = -1
    @State private var name = ""
    @State private var gatewayURL = ""
    @State private var setupCode = ""
    @State private var copied = false
    @State private var filled = false
    @State private var justSaved: String?
    @State private var forgetting: String?
    @State private var dropping: MacAgent?
    @State private var dropped = false
    @State private var probe: ProbeResult?
    @State private var prober = OpenClawProbe()
    private let accent = Color.accentColor

    private var layout: some View {
        VStack(spacing: 0) {
            header
            Divider()
            HStack(alignment: .top, spacing: 18) {
                wifiCard
                agentCard
            }
            .padding(18)
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
            Divider()
            footer
        }
        .background(Color(nsColor: .windowBackgroundColor))
        .frame(minWidth: 820, minHeight: 640)
    }

    private var importLifecycle: some View {
        layout
        .onAppear {
            remembered = WifiMemory.ssids()
            if gatewayURL.isEmpty { gatewayURL = UserDefaults.standard.string(forKey: "gatewayURL") ?? "" }
            wifi.start(scanMac: remembered.isEmpty)
            prefill()
        }
        .onChange(of: link.loaded) { _, _ in prefill() }
        .onReceive(incoming.$pending) { configuration in
            guard let configuration else { return }
            DispatchQueue.main.async { applyImportedConfiguration(configuration) }
        }
        .alert("无法导入配置", isPresented: Binding(
            get: { incoming.error != nil }, set: { if !$0 { incoming.error = nil } }
        )) { Button("好", role: .cancel) {} }
        message: { Text(incoming.error ?? "") }
    }

    private var deviceLifecycle: some View {
        importLifecycle
        .onChange(of: wifi.current) { _, _ in prefill() }
        .onChange(of: ssid) { old, _ in
            if let old { passwordDrafts[old] = password }
            pickedWifi()
        }
        .onChange(of: routeForm) { _, _ in
            if link.writeSection == .route && !writing { link.result = nil }
        }
        .onChange(of: manualSSID) { _, value in
            if manualNetwork { ssid = value.isEmpty ? nil : value }
        }
        .onChange(of: link.agents) { _, agents in
            let target = justSaved.flatMap { savedName in agents.first { $0.name == savedName } }
                ?? ((dropped || !agents.contains { $0.id == agentPick }) ? agents.first { $0.current } : nil)
            justSaved = nil
            if dropped {
                dropped = false
                agentPick = target?.id ?? -1
                name = target?.name ?? ""
            } else if let target { agentPick = target.id }
            if let picked = agents.first(where: { $0.id == agentPick }) { routeForm = RouteForm(picked.route) }
        }
    }

    private var formLifecycle: some View {
        deviceLifecycle
        .onChange(of: [ssid ?? "", password, name, setupCode]) { _, _ in
            if link.result != .writing { link.result = nil }
        }
        .onChange(of: [ssid ?? "", password]) { _, _ in
            if link.tried != .trying { link.tried = nil }
        }
        .onChange(of: [setupCode, gatewayURL]) { _, _ in
            prober.cancel()
            probe = nil
        }
    }

    var body: some View {
        formLifecycle
        .confirmationDialog("让 AI Passport 忘掉「\(forgetting ?? "")」？", isPresented: Binding(
            get: { forgetting != nil }, set: { if !$0 { forgetting = nil } }
        )) {
            Button("忘掉网络", role: .destructive) {
                if let network = forgetting { link.forget(ssid: network) }
                forgetting = nil
            }
        } message: { Text("删除设备保存的网络和密码。这台 Mac 记住的密码会保留。") }
        .confirmationDialog("删除「\(dropping?.name ?? "")」？", isPresented: Binding(
            get: { dropping != nil }, set: { if !$0 { dropping = nil } }
        )) {
            Button("删除配置", role: .destructive) {
                if let agent = dropping { dropped = true; link.dropAgent(agent) }
                dropping = nil
            }
        } message: { Text("从 AI Passport 删除这套 OpenClaw 配置。") }
    }

    private func applyImportedConfiguration(_ configuration: ConfigurationLink) {
        guard incoming.pending == configuration else { return }
        guard !writing else {
            incoming.error = "正在保存配置，请稍后重新打开链接。"
            incoming.pending = nil
            return
        }
        if let address = configuration.gatewayURL { gatewayURL = address }
        if let code = configuration.setupCode {
            agentPick = -1
            importedName = configuration.name
            setupCode = code
        } else if let imported = configuration.name { importedName = imported }
        if let network = configuration.ssid {
            if let pass = configuration.password { passwordDrafts[network] = pass }
            manualNetwork = !choices.contains { $0.ssid == network }
            manualSSID = network
            ssid = network
            if let pass = configuration.password { password = pass }
        }
        copied = false
        incoming.pending = nil
    }

    private var header: some View {
        HStack(spacing: 14) {
            Image("Passport")
                .resizable().interpolation(.high)
                .frame(width: 54, height: 54)
                .clipShape(RoundedRectangle(cornerRadius: 13))
            VStack(alignment: .leading, spacing: 5) {
                Text("设置你的 AI Passport").font(.title2.weight(.semibold))
                Text("配置网络和 OpenClaw，保存到设备。")
                    .font(.callout).foregroundStyle(.secondary)
            }
            Spacer()
            VStack(alignment: .trailing, spacing: 5) {
                Text("设备屏幕语言").font(.caption).foregroundStyle(.secondary)
                Picker("设备屏幕语言", selection: Binding(get: { link.language }, set: { link.setLanguage($0) })) {
                    Text("中文").tag("zh")
                    Text("English").tag("en")
                }
                .pickerStyle(.segmented).labelsHidden().frame(width: 142)
                .disabled(!link.loaded || writing)
            }
        }
        .padding(.horizontal, 24).padding(.vertical, 14)
    }

    private func card<Content: View>(@ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 12, content: content)
            .padding(16)
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
            .background(Color(nsColor: .controlBackgroundColor), in: RoundedRectangle(cornerRadius: 14))
            .overlay(RoundedRectangle(cornerRadius: 14).stroke(.primary.opacity(0.07), lineWidth: 1))
    }

    private func sectionTitle(_ title: String, symbol: String) -> some View {
        Label(title, systemImage: symbol).font(.headline)
    }

    // MARK: Wi-Fi

    private var choices: [WifiChoice] {
        var rows: [String: WifiChoice] = [:]
        for item in wifi.nearby {
            rows[item.ssid] = WifiChoice(ssid: item.ssid, rssi: item.rssi,
                reach: item.has24 ? .near : .only5, secure: item.secure)
        }
        for item in link.sees {
            var row = rows[item.ssid] ?? WifiChoice(ssid: item.ssid, rssi: item.rssi, reach: .near, secure: true)
            row.reach = .near
            row.rssi = item.rssi
            rows[item.ssid] = row
        }
        for network in link.saved where rows[network] == nil {
            rows[network] = WifiChoice(ssid: network, rssi: nil, reach: .away, secure: true)
        }
        for network in remembered where rows[network] == nil {
            rows[network] = WifiChoice(ssid: network, rssi: nil, reach: .away, secure: true)
        }
        return rows.values.sorted { a, b in
            if a.reach != b.reach { return a.reach.rawValue < b.reach.rawValue }
            let pa = priority(a.ssid), pb = priority(b.ssid)
            if pa != pb { return pa < pb }
            if a.rssi != b.rssi { return (a.rssi ?? -200) > (b.rssi ?? -200) }
            return a.ssid.localizedStandardCompare(b.ssid) == .orderedAscending
        }
    }

    private func priority(_ network: String) -> Int {
        if network == link.savedCurrent { return 0 }
        if remembered.contains(network) { return 1 }
        if network == wifi.current { return 2 }
        return 3
    }

    private var choice: WifiChoice? {
        guard let ssid, !ssid.isEmpty else { return nil }
        if manualNetwork { return WifiChoice(ssid: ssid, rssi: nil, reach: .near, secure: true) }
        return choices.first { $0.ssid == ssid }
    }

    private var wifiCard: some View {
        card {
            HStack {
                sectionTitle("Wi-Fi", symbol: "wifi")
                Spacer()
                Button { wifi.scanIfAuthorized(); link.scan() } label: {
                    Label("重新扫描", systemImage: "arrow.clockwise")
                }
                .buttonStyle(.borderless).font(.callout)
                .foregroundStyle(accent)
                .disabled(wifi.scanning || writing)
            }
            Text("选择 AI Passport 要连接的网络。")
                .font(.callout).foregroundStyle(.secondary)
            if wifi.locationDenied && link.sees.isEmpty {
                HStack {
                    Text("允许定位后可读取 Mac 扫描到的网络名。")
                        .font(.caption).foregroundStyle(.secondary)
                    Button("打开设置") {
                        NSWorkspace.shared.open(URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_LocationServices")!)
                    }.buttonStyle(.link)
                }
            }
            ScrollView {
                LazyVStack(spacing: 2) {
                    if choices.isEmpty {
                        Text(wifi.scanning ? "正在查找附近网络…" : "没找到网络，也可以手动添加。")
                            .font(.callout).foregroundStyle(.secondary).padding(.vertical, 24)
                    }
                    ForEach(showAll ? choices : Array(choices.prefix(4))) { row in wifiRow(row) }
                }
            }
            .frame(height: 164)
            .background(.primary.opacity(0.025), in: RoundedRectangle(cornerRadius: 8))
            HStack {
                if choices.count > 4 {
                    Button(showAll ? "收起列表" : "查看全部 \(choices.count) 个网络") { showAll.toggle() }
                        .buttonStyle(.link)
                }
                Spacer()
                if !wifi.canScanSilently && !wifi.locationDenied {
                    Button("扫描 Mac 网络") { wifi.scan() }.buttonStyle(.link)
                }
                Button(manualNetwork ? "从列表选择" : "手动添加") {
                    manualNetwork.toggle()
                    if manualNetwork { ssid = manualSSID.isEmpty ? nil : manualSSID }
                }.buttonStyle(.link)
            }.font(.caption)
            if manualNetwork {
                TextField("网络名称（SSID）", text: $manualSSID).textFieldStyle(.roundedBorder)
            }
            passwordRow
            HStack {
                testResult(wifiTestText, busy: link.tried == .trying)
                Spacer(minLength: 4)
                Button("测试 Wi-Fi") {
                    guard let ssid else { return }
                    link.tryWifi(ssid: ssid, password: password)
                }
                .disabled(!link.loaded || !wifiReady || writing || link.tried == .trying)
                .help("由 AI Passport 试连，不会切换这台 Mac 的 Wi-Fi")
                Button("写入 Wi-Fi", action: saveWiFi)
                    .buttonStyle(.borderedProminent)
                    .disabled(!link.loaded || !wifiReady || writing || link.tried == .trying)
            }
            Text("支持 2.4 GHz。iPhone 热点需开启「最大兼容性」。")
                .font(.caption).foregroundStyle(.secondary)
        }
    }

    private func wifiRow(_ row: WifiChoice) -> some View {
        HStack(spacing: 6) {
            Button {
                manualNetwork = false
                ssid = row.ssid
            } label: {
                HStack(spacing: 9) {
                    Image(systemName: ssid == row.ssid && !manualNetwork ? "checkmark.circle.fill" : "circle")
                        .foregroundStyle(ssid == row.ssid && !manualNetwork ? accent : .secondary.opacity(0.5))
                    VStack(alignment: .leading, spacing: 3) {
                        Text(row.ssid).lineLimit(1)
                            .foregroundStyle(row.reach == .only5 ? .secondary : .primary)
                        HStack(spacing: 6) {
                            if remembered.contains(row.ssid) {
                                Label("已存密码", systemImage: "key.fill").foregroundStyle(.green)
                            } else if link.saved.contains(row.ssid) { Text("设备已保存") }
                            if row.ssid == wifi.current { Text("Mac 当前连接") }
                            if row.reach == .only5 { Text("未发现 2.4 GHz") }
                            if row.reach == .away { Text("不在附近") }
                        }
                        .font(.system(size: 10)).foregroundStyle(.secondary).lineLimit(1)
                    }
                    Spacer(minLength: 0)
                    Image(systemName: "wifi", variableValue: signalStrength(row.rssi))
                        .symbolRenderingMode(.hierarchical)
                        .font(.callout).foregroundStyle(.secondary)
                        .frame(width: 22)
                        .opacity(row.rssi == nil ? 0.35 : 1)
                        .accessibilityLabel(signalDescription(row.rssi))
                        .help(signalDescription(row.rssi))
                }
                .padding(.vertical, 7).padding(.horizontal, 8)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .disabled(row.reach == .only5 || writing)
            Group {
              if remembered.contains(row.ssid) || link.saved.contains(row.ssid) {
                Menu {
                    if remembered.contains(row.ssid) {
                        Button("忘记这台 Mac 保存的密码") {
                            WifiMemory.forget(row.ssid)
                            passwordDrafts.removeValue(forKey: row.ssid)
                            remembered = WifiMemory.ssids()
                            if ssid == row.ssid { password = "" }
                        }
                    }
                    if link.saved.contains(row.ssid) {
                        Button("从 AI Passport 删除网络", role: .destructive) { forgetting = row.ssid }
                    }
                } label: { Image(systemName: "ellipsis") }
                .menuStyle(.borderlessButton).menuIndicator(.hidden).fixedSize()
                .disabled(writing)
                .help("管理「\(row.ssid)」的密码与设备记录")
              } else {
                Color.clear.accessibilityHidden(true)
              }
            }
            .frame(width: 24)
            .padding(.trailing, 7)
        }
        .background(ssid == row.ssid && !manualNetwork ? accent.opacity(0.11) : .clear,
                    in: RoundedRectangle(cornerRadius: 7))
    }

    private func signalStrength(_ rssi: Int?) -> Double {
        guard let rssi else { return 0 }
        if rssi >= -55 { return 1 }
        if rssi >= -67 { return 2.0 / 3.0 }
        return 1.0 / 3.0
    }

    private func signalDescription(_ rssi: Int?) -> String {
        guard let rssi else { return "未检测到 Wi-Fi 信号" }
        if rssi >= -55 { return "Wi-Fi 信号强" }
        if rssi >= -67 { return "Wi-Fi 信号中等" }
        return "Wi-Fi 信号弱"
    }

    private var passwordRow: some View {
        VStack(alignment: .leading, spacing: 7) {
            HStack {
                Text("Wi-Fi 密码").font(.callout.weight(.medium))
                Spacer()
                if let ssid { Text(ssid).font(.caption).foregroundStyle(.secondary).lineLimit(1) }
            }
            if choice?.secure == false {
                Text("开放网络，无需密码").foregroundStyle(.secondary).frame(height: 24)
            } else {
                HStack(spacing: 8) {
                    Group {
                        if showPassword { TextField(passwordPlaceholder, text: $password) }
                        else { SecureField(passwordPlaceholder, text: $password) }
                    }
                    .textFieldStyle(.roundedBorder).disabled(ssid == nil || writing)
                    Button { showPassword.toggle() } label: {
                        Image(systemName: showPassword ? "eye.slash" : "eye")
                    }
                    .buttonStyle(.borderless).help(showPassword ? "隐藏密码" : "显示密码")
                }
            }
            if password.isEmpty && ssid != nil && choice?.secure != false {
                HStack {
                    Text(keychainNote ?? "也可以从系统钥匙串读取。")
                        .font(.caption).foregroundStyle(.secondary)
                    Spacer()
                    Button(readingPassword ? "正在读取…" : "读取密码", action: readKeychain)
                        .font(.caption).disabled(readingPassword)
                }
            }
        }
    }

    private var passwordPlaceholder: String {
        guard let ssid else { return "先选择一个网络" }
        return link.saved.contains(ssid) ? "留空沿用设备已保存的密码" : "输入网络密码"
    }

    private func pickedWifi() {
        showPassword = false
        keychainNote = nil
        guard let selected = ssid else { password = ""; return }
        let draft = passwordDrafts[selected] ?? ""
        password = draft.isEmpty ? (WifiMemory.password(for: selected) ?? "") : draft
    }

    private func readKeychain() {
        guard let selected = ssid, password.isEmpty, !readingPassword else { return }
        readingPassword = true
        keychainNote = "请在系统弹窗中允许…"
        DispatchQueue.global(qos: .userInitiated).async {
            let found = WifiScan.savedPassword(ssid: selected)
            DispatchQueue.main.async {
                readingPassword = false
                guard ssid == selected, password.isEmpty else { return }
                if let found {
                    password = found
                    WifiMemory.remember(found, for: selected)
                    remembered = WifiMemory.ssids()
                    keychainNote = nil
                } else { keychainNote = "没有读取到，请手动输入。" }
            }
        }
    }

    // MARK: OpenClaw

    private var agentCard: some View {
        card {
            HStack {
                sectionTitle("OpenClaw", symbol: "bubble.left.and.bubble.right")
                Spacer()
                Button {
                    PairPrompt.copyToPasteboard()
                    copied = true
                } label: {
                    Label(copied ? "已复制" : "复制提示词给 OpenClaw",
                          systemImage: copied ? "checkmark" : "doc.on.doc")
                }
                .buttonStyle(.borderless).font(.callout)
                .foregroundStyle(copied ? .green : accent)
                .help(copied ? "贴给 OpenClaw，让它生成配对码" : "复制配对提示词，然后贴给你的 OpenClaw")
            }
            Text(copied ? "贴给 OpenClaw，再把它回复的配对码粘贴到下面。" : "先复制提示词给 OpenClaw，获取一次性配对码。")
                .font(.callout).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            if !link.agents.isEmpty {
                Picker("设备上的配置", selection: $agentPick) {
                    ForEach(link.agents) { agent in
                        Text(agent.current ? "\(agent.name)（当前）" : agent.name).tag(agent.id)
                    }
                    Text("添加新的 OpenClaw").tag(-1)
                }
                .onChange(of: agentPick) { _, pick in
                    name = link.agents.first { $0.id == pick }?.name ?? ""
                    routeForm = RouteForm(link.agents.first { $0.id == pick }?.route ?? ProxySettings())
                    prober.cancel(); probe = nil
                }
            }
            VStack(alignment: .leading, spacing: 7) {
                Text("配对码").font(.callout.weight(.medium))
                ZStack(alignment: .topLeading) {
                    if setupCode.isEmpty {
                        Text("粘贴 OpenClaw 回复的完整配对码")
                            .foregroundStyle(.tertiary).padding(.horizontal, 5).padding(.vertical, 8)
                            .allowsHitTesting(false)
                    }
                    TextEditor(text: $setupCode)
                        .scrollContentBackground(.hidden)
                        .frame(height: 78)
                        .accessibilityLabel("配对码")
                }
                    .font(.system(.callout, design: .monospaced))
                    .padding(8)
                    .frame(maxWidth: .infinity, alignment: .topLeading)
                    .background(.primary.opacity(0.035), in: RoundedRectangle(cornerRadius: 8))
                    .overlay(RoundedRectangle(cornerRadius: 8).stroke(.primary.opacity(0.13)))
                    .disabled(writing)
                if !setupCode.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                    if parsed != nil {
                        Label("已识别配对码，地址已从码中读取", systemImage: "checkmark.circle.fill")
                            .foregroundStyle(.green).font(.caption)
                    } else {
                        Label("配对码不完整或格式不对，请重新粘贴。", systemImage: "exclamationmark.circle.fill")
                            .foregroundStyle(.orange).font(.caption)
                    }
                }
            }
            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    Text("网关地址").font(.callout.weight(.medium))
                    Spacer()
                    Text(parsed != nil ? "来自配对码" : "已保存的地址")
                        .font(.caption).foregroundStyle(.secondary)
                }
                Text(displayURL.isEmpty ? "粘贴配对码后自动显示，无需手填" : displayURL)
                    .id(displayURL)
                    .font(.system(.callout, design: .monospaced))
                    .foregroundStyle(displayURL.isEmpty ? .secondary : .primary)
                    .textSelection(.enabled)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Divider()
            HStack {
                testResult(probeText, busy: probe == .testing)
                Spacer(minLength: 4)
                Button("测试地址") {
                    probe = .testing
                    prober.run(url: displayURL, token: "") { probe = $0 }
                }
                .disabled(!PairPrompt.validGateway(displayURL) || probe == .testing || writing || invalidCode)
                .help("从这台 Mac 检查网关响应，不使用也不消耗配对码")
                Button(parsed == nil && pickedSaved?.current == false ? "使用此 OpenClaw" : "写入 OpenClaw", action: saveAgent)
                    .buttonStyle(.borderedProminent)
                    .disabled(!link.loaded || invalidCode ||
                              (parsed == nil && pickedSaved?.current != false) || writing || link.tried == .trying)
            }
            if let agent = pickedSaved {
                routeRow(agent)
                HStack {
                    Label(agent.state == "ready" ? "设备已有凭据" : "设备尚待完成配对",
                          systemImage: agent.state == "ready" ? "checkmark.shield" : "clock")
                        .font(.caption).foregroundStyle(.secondary)
                    Spacer()
                    Button(role: .destructive) { dropping = agent } label: { Image(systemName: "trash") }
                        .buttonStyle(.borderless).help("删除设备上的这套 OpenClaw")
                }
            }
            Spacer(minLength: 0)
            Text("新配对码通常 10 分钟内有效。测试地址只检查连通性，不代表设备已配对。")
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    private func routeRow(_ agent: MacAgent) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("连接方式").font(.callout.weight(.medium))
                Spacer()
                Picker("连接方式", selection: $routeForm.enabled) {
                    Text("直连").tag(false)
                    Text("Shadowsocks").tag(true)
                }
                .pickerStyle(.segmented).labelsHidden().fixedSize()
            }
            if routeForm.enabled {
                HStack(spacing: 10) {
                    TextField("服务器", text: $routeForm.host).accessibilityLabel("Shadowsocks 服务器")
                    TextField("端口", text: $routeForm.port).frame(width: 70).accessibilityLabel("Shadowsocks 端口")
                }.textFieldStyle(.roundedBorder)
                HStack(spacing: 10) {
                    SecureField(routeForm.hasSecret ? "留空沿用已保存的密码" : "密码", text: $routeForm.secret)
                        .textFieldStyle(.roundedBorder).accessibilityLabel("Shadowsocks 密码")
                    Text("aes-256-cfb").font(.caption).foregroundStyle(.secondary)
                }
            }
            HStack {
                Text(routeForm.enabled ? "这套 OpenClaw 经 Shadowsocks 连接。" : "这套 OpenClaw 直接连接，适合国内地址。")
                    .font(.caption).foregroundStyle(.secondary)
                Spacer()
                Button("写入连接方式") { link.setRoute(agent, route: routeForm.settings) }
                    .disabled(!link.loaded || writing || !routeForm.settings.valid ||
                              routeForm == RouteForm(agent.route))
            }
        }
        .disabled(writing)
    }

    private var parsed: (url: String, code: String)? { PairPrompt.decode(payload: setupCode) }
    private var invalidCode: Bool { !setupCode.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && parsed == nil }
    private var displayURL: String { parsed?.url ?? pickedSaved?.url ?? gatewayURL }
    private var pickedSaved: MacAgent? { link.agents.first { $0.id == agentPick } }
    private var writing: Bool { link.result == .writing }
    private var wifiReady: Bool {
        guard let choice, choice.reach != .only5, choice.ssid.utf8.count <= 32 else { return false }
        return !choice.secure || !password.isEmpty || link.saved.contains(choice.ssid)
    }
    private var agentName: String {
        let candidate = pickedSaved?.name ?? importedName ?? (URL(string: displayURL)?.host ?? "OpenClaw")
        var out = ""
        for character in candidate {
            if (out + String(character)).utf8.count >= 24 { break }
            out.append(character)
        }
        return out
    }

    private func prefill() {
        guard link.loaded else { return }
        if !filled {
            filled = true
            if setupCode.isEmpty { agentPick = link.currentAgent?.id ?? -1 }
            if ssid == nil { ssid = link.savedCurrent }
        }
        if ssid == nil, let current = wifi.current, choices.contains(where: { $0.ssid == current && $0.reach == .near }) {
            ssid = current
        }
    }

    // MARK: Status and save

    private var footer: some View {
        VStack(alignment: .leading, spacing: 10) {
            if let result = link.result {
                switch result {
                case .writing:
                    HStack(spacing: 8) { ProgressView().controlSize(.small); Text(link.writeStatus) }
                case .saved:
                    Label(link.writeSection == .wifi ? "Wi-Fi 已写入设备"
                          : link.writeSection == .route ? "连接方式已写入设备" : "OpenClaw 已写入设备",
                          systemImage: "checkmark.circle.fill").foregroundStyle(.green)
                case .failed(let reason):
                    Label(failure(reason), systemImage: "exclamationmark.triangle.fill")
                        .foregroundStyle(.orange).fixedSize(horizontal: false, vertical: true)
                }
            }
            HStack(spacing: 10) {
                Circle().fill(link.linkKind == "usb" ? .green : link.usbPresent ? .orange : .secondary.opacity(0.4))
                    .frame(width: 8, height: 8)
                Text(connectionText).font(.callout).foregroundStyle(.secondary)
                if link.linkKind == "usb", let firmware = BuiltFirmware.locate(), firmware.matches(link.build) {
                    Text("固件已是最新").font(.caption).foregroundStyle(.secondary)
                }
                Spacer()
            }
        }
        .padding(.horizontal, 24).padding(.vertical, 14)
    }

    private var connectionText: String {
        if link.linkKind == "usb" { return "USB 已连上" }
        if link.usbPresent { return "USB 已插上，等待设备响应" }
        if link.linkKind == "socket" { return "本机模拟设备" }
        return "请连接 AI Passport 的 USB 数据线"
    }

    private func testResult(_ value: (String, Bool)?, busy: Bool) -> some View {
        Group {
            if busy { ProgressView().controlSize(.small); Text("正在测试…").foregroundStyle(.secondary) }
            else if let (text, ok) = value {
                Label(text, systemImage: ok ? "checkmark.circle.fill" : "exclamationmark.circle.fill")
                    .foregroundStyle(ok ? .green : .orange)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }.font(.caption)
    }

    private var wifiTestText: (String, Bool)? {
        switch link.tried {
        case .ok: return ("设备试连成功", true)
        case .failed(let reason): return (wifiWhy(reason), false)
        default: return nil
        }
    }
    private func wifiWhy(_ reason: String) -> String {
        switch reason {
        case "pass": return "Wi-Fi 密码不对"
        case "notfound": return "设备没有找到这个网络"
        case "security": return "不支持这个网络的加密方式"
        case "timeout": return "连接超时，请检查密码和信号"
        case "busy": return "设备正忙，请稍后再试"
        default: return "设备未能连接 Wi-Fi"
        }
    }
    private var probeText: (String, Bool)? {
        switch probe {
        case .ok: return ("网关有响应", true)
        case .badToken: return ("网关有响应，设备仍需配对", true)
        case .unreachable(let reason): return (reason, false)
        case .notOpenClaw: return ("未识别到 OpenClaw 网关", false)
        default: return nil
        }
    }
    private func failure(_ reason: String) -> String {
        switch reason {
        case "pass", "notfound", "security", "timeout", "fail", "busy": return wifiWhy(reason)
        case "url", "code", "token": return "配对码无法识别，请重新获取后粘贴。"
        case "full": return "设备最多保存 8 个 Wi-Fi，请先删除不需要的网络。"
        case "stale": return "设备配置有更新，已重新读取；再次点击保存即可。"
        case "storage": return "设备未能保存配置，请重试。表单仍然保留。"
        case "proxy": return "Shadowsocks 服务器、端口或密码无效，请检查后重试。"
        default: return reason
        }
    }
    private func saveWiFi() {
        guard let ssid, wifiReady, !writing, link.tried != .trying else { return }
        let network = ssid
        let pass = password
        if !pass.isEmpty {
            WifiMemory.remember(pass, for: network)
            remembered = WifiMemory.ssids()
        }
        link.writeSection = .wifi
        withCurrentFirmware { link.saveWiFi(ssid: network, password: pass) }
    }

    private func saveAgent() {
        guard !invalidCode, !writing, link.tried != .trying else { return }
        if let parsed {
            let name = agentName
            justSaved = name
            UserDefaults.standard.set(parsed.url, forKey: "gatewayURL")
            link.writeSection = .openClaw
            withCurrentFirmware { link.saveAgent(setupCode: parsed.code, name: name) }
        } else if let agent = pickedSaved, !agent.current {
            link.useAgent(agent)
        }
    }

    private func withCurrentFirmware(_ write: @escaping () -> Void) {
        if link.linkKind == "usb", let built = BuiltFirmware.locate(), !built.matches(link.build), let port = link.usbPort {
            link.result = .writing
            link.pauseForFlash {
                Thread.detachNewThread {
                    let error = FirmwareFlasher.write(port: port, built: built)
                    DispatchQueue.main.async {
                        if let error {
                            self.link.resumeAfterFlash()
                            self.link.result = .failed(error)
                            return
                        }
                        self.link.resumeAfterFlash { ready in
                            if ready && built.matches(self.link.build) {
                                self.link.result = nil
                                write()
                            } else { self.link.result = .failed("固件已写入，USB 尚未恢复。请重新连接；表单已保留。") }
                        }
                    }
                }
            }
        } else { write() }
    }
}
