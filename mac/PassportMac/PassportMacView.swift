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

struct PassportMacView: View {
    @ObservedObject var link: PassportSocket
    @ObservedObject var wifi: WifiScan
    @ObservedObject var incoming: ConfigurationInbox
    @State private var importedName: String?
    @State private var wifiDraft = ConfigurationDraft(WiFiConfiguration())
    @State private var passwordDrafts: [String: String] = [:]
    @State private var routeDrafts: [String: ConfigurationDraft<RouteForm>] = [:]
    @State private var pairingDrafts: [String: ConfigurationDraft<String>] = [:]
    @State private var selectionDraft = ConfigurationDraft(-1)
    @State private var configurationSave: ConfigurationSave?
    @State private var configurationSaved = false
    @State private var savedBeforeFailure = 0
    @State private var saveReadbackTimer: Timer?
    @State private var showPairing = false
    @State private var showRoute = false
    @State private var showDiagnostics = false
    @State private var showMaintenance = false
    @State private var remembered: Set<String> = []
    @State private var showPassword = false
    @State private var readingPassword = false
    @State private var keychainNote: String?
    @State private var showAll = false
    @State private var manualNetwork = false
    @State private var manualSSID = ""
    @State private var gatewayURL = ""
    @State private var copied = false
    @State private var filled = false
    @State private var forgetting: String?
    @State private var dropping: MacAgent?
    @State private var dropped = false
    @State private var probe: ProbeResult?
    @State private var prober = OpenClawProbe()
    @State private var languageDraft = ConfigurationDraft("zh")
    @State private var volumeDraft = ConfigurationDraft(0)
    @State private var firmware: BuiltFirmware?
    private let accent = Color.accentColor

    private var ssid: String? {
        get { wifiDraft.value.ssid }
        nonmutating set {
            guard newValue != ssid else { return }
            if let ssid { passwordDrafts[ssid] = password }
            wifiDraft.value = WiFiConfiguration(ssid: newValue,
                password: newValue.map { passwordDrafts[$0] ?? WifiMemory.password(for: $0) ?? "" } ?? "")
            showPassword = false
            keychainNote = nil
        }
    }

    private var password: String {
        get { wifiDraft.value.password }
        nonmutating set { wifiDraft.value.password = newValue }
    }

    private var agentPick: Int {
        get { selectionDraft.value }
        nonmutating set { selectionDraft.value = newValue }
    }

    private var setupCode: String {
        get { pairingDrafts[agentDraftKey]?.value ?? "" }
        nonmutating set {
            var draft = pairingDrafts[agentDraftKey] ?? ConfigurationDraft("")
            draft.value = newValue
            pairingDrafts[agentDraftKey] = draft
        }
    }

    private var routeForm: RouteForm {
        get { routeDrafts[agentDraftKey]?.value ?? RouteForm(pickedSaved?.route ?? ProxySettings()) }
        nonmutating set {
            var draft = routeDrafts[agentDraftKey] ?? ConfigurationDraft(RouteForm(pickedSaved?.route ?? ProxySettings()))
            draft.value = newValue
            routeDrafts[agentDraftKey] = draft
        }
    }

    private var agentDraftKey: String { pickedSaved.map { "saved:\($0.name)" } ?? "new" }
    private var pairingChanged: Bool {
        !setupCode.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && pairingDrafts[agentDraftKey]?.isDirty == true
    }
    private var routeChanged: Bool { routeDrafts[agentDraftKey]?.isDirty == true }
    private var selectionChanged: Bool { selectionDraft.isDirty && pickedSaved != nil }
    private var agentChanged: Bool { selectionChanged || pairingChanged || routeChanged }
    private var unsavedAgents: Bool {
        selectionChanged || pairingDrafts.values.contains {
            $0.isDirty && !$0.value.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
        } || routeDrafts.values.contains { $0.isDirty }
    }
    private var preferencesChanged: Bool { languageDraft.isDirty || volumeDraft.isDirty }
    private var hasChanges: Bool { wifiDraft.isDirty || unsavedAgents || preferencesChanged }

    private var layout: some View {
        VStack(spacing: 0) {
            header
            Divider()
            ScrollView {
                HStack(alignment: .top, spacing: 18) {
                    wifiCard
                    VStack(spacing: 16) {
                        agentCard
                        deviceSettings
                    }.frame(maxWidth: .infinity)
                }
                .padding(18)
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
            Divider()
            maintenance
            Divider()
            footer
        }
        .disclosureGroupStyle(PassportDisclosureStyle())
        .background(Color(nsColor: .windowBackgroundColor))
        .frame(minWidth: 820, minHeight: 640)
    }

    private var importLifecycle: some View {
        layout
        .onAppear {
            firmware = BuiltFirmware.locate()
            remembered = WifiMemory.ssids()
            if gatewayURL.isEmpty { gatewayURL = UserDefaults.standard.string(forKey: "gatewayURL") ?? "" }
            wifi.start(scanMac: remembered.isEmpty)
            prefill()
        }
        .onChange(of: link.loaded) { _, loaded in
            if loaded { prefill(); continueConfigurationSave() }
            else if configurationSave != nil {
                link.result = .failed("设备连接已断开，未保存的修改已保留。重新连接后可继续保存。")
            }
        }
        .onChange(of: link.configurationRevision) { _, _ in
            prefill()
            continueConfigurationSave()
        }
        .onReceive(NotificationCenter.default.publisher(for: NSApplication.didBecomeActiveNotification)) { _ in
            firmware = BuiltFirmware.locate()
        }
        .onChange(of: link.device) { _, status in
            if let status { volumeDraft.receive(status.volume) }
        }
        .onReceive(link.$result) { result in
            handleWriteResult(result)
        }
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
        .onChange(of: manualSSID) { _, value in
            if manualNetwork { ssid = value.isEmpty ? nil : value }
        }
    }

    private var formLifecycle: some View {
        deviceLifecycle
        .onChange(of: [ssid ?? "", password, setupCode, String(agentPick), String(describing: routeForm),
                      languageDraft.value, String(volumeDraft.value)]) { _, _ in
            if !writing && hasChanges { link.result = nil; configurationSaved = false; savedBeforeFailure = 0 }
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
                if let agent = dropping {
                    routeDrafts.removeValue(forKey: "saved:\(agent.name)")
                    pairingDrafts.removeValue(forKey: "saved:\(agent.name)")
                    dropped = true
                    link.dropAgent(agent)
                }
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
            showPairing = true
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
                Text("AI Passport").font(.title2.weight(.semibold))
                Text("配置网络与助手，保存到设备。")
                    .font(.callout).foregroundStyle(.secondary)
            }
            Spacer()
            if hasChanges {
                Label("有未保存的修改", systemImage: "circle.fill")
                    .font(.caption).foregroundStyle(.orange)
            }
            if link.configurationBusy {
                Label(link.deviceCheck?.running == true ? "正在检测设备" : "设备正在使用", systemImage: "waveform")
                    .font(.callout).foregroundStyle(.secondary)
            }
        }
        .padding(.horizontal, 24).padding(.vertical, 14)
    }

    private var deviceSettings: some View {
        card {
            HStack {
                sectionTitle("设备偏好", symbol: "slider.horizontal.3")
                Spacer()
                draftIndicator(preferencesChanged)
            }
            HStack {
                Text("屏幕语言").font(.callout)
                Spacer()
                Picker("设备屏幕语言", selection: $languageDraft.value) {
                    Text("中文").tag("zh")
                    Text("English").tag("en")
                }
                .pickerStyle(.segmented).labelsHidden().frame(width: 142)
                .disabled(!link.loaded || writing || link.tried == .trying || link.configurationBusy)
            }
            VStack(alignment: .leading, spacing: 5) {
                HStack {
                    Text("扬声器音量").font(.caption).foregroundStyle(.secondary)
                    Spacer()
                    Text(link.device == nil ? "—" : "\(volumeDraft.value)%")
                        .font(.caption.monospacedDigit()).foregroundStyle(.secondary)
                }
                Slider(value: Binding(get: { Double(volumeDraft.value) }, set: { volumeDraft.value = Int($0) }),
                       in: 0...100, step: 5)
                .accessibilityLabel("设备扬声器音量")
                .disabled(!link.loaded || !link.supportsDeviceControl || link.device == nil ||
                          writing || link.configurationBusy || link.tried == .trying)
            }
        }
    }

    private var maintenance: some View {
        DisclosureGroup(isExpanded: $showMaintenance) {
            HStack(alignment: .center, spacing: 24) {
                VStack(alignment: .leading, spacing: 5) {
                    Text("固件是设备运行的软件。仅在修复问题或增加功能时更新，需要重启设备。")
                    Text("不会保存当前表单的修改；兼容分区布局下保留设备已保存的配置。")
                        .foregroundStyle(.secondary)
                }.font(.caption)
                Spacer(minLength: 0)
                Button("更新固件") { updateFirmware() }
                    .disabled(link.linkKind != "usb" || !link.loaded || firmware == nil ||
                              firmware?.matches(link.build) == true || writing ||
                              link.configurationBusy || link.tried == .trying)
                    .help("只更新应用区，不写入尚未保存的配置")
            }.padding(.top, 8)
        } label: {
            HStack {
                Label("设备维护", systemImage: "cpu").font(.callout.weight(.medium))
                Spacer()
                Text(firmwareStatus).font(.caption).foregroundStyle(.secondary)
                    .help(link.build == "-" ? "连接设备后读取固件标识" : "当前固件：\(link.build)")
            }
        }
        .padding(.horizontal, 24).padding(.vertical, 12)
    }

    private var firmwareStatus: String {
        guard link.loaded else { return "等待连接" }
        guard let firmware else { return "未找到可用更新" }
        return firmware.matches(link.build) ? "固件已是配套版本" : "有可用固件更新"
    }

    private func card<Content: View>(@ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 12, content: content)
            .padding(16)
            .frame(maxWidth: .infinity, alignment: .topLeading)
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
                draftIndicator(wifiDraft.isDirty)
                Spacer()
                Button { wifi.scanIfAuthorized(); link.scan() } label: {
                    Label("重新扫描", systemImage: "arrow.clockwise")
                }
                .buttonStyle(.borderless).font(.callout)
                .foregroundStyle(accent)
                .disabled(wifi.scanning || writing)
            }
            Text("选择设备连接的网络，保存后生效。")
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
            .frame(height: WiFiListLayout.height(rowCount: choices.count, expanded: showAll))
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
                Button("测试 Wi-Fi") {
                    guard let ssid else { return }
                    link.tryWifi(ssid: ssid, password: password)
                }
                .disabled(!link.loaded || !wifiReady || writing || link.tried == .trying)
                .help("由 AI Passport 试连，不会切换这台 Mac 的 Wi-Fi")
                Spacer(minLength: 4)
            }
            testResult(wifiTestText, busy: link.tried == .trying)
            Text("支持 2.4 GHz。iPhone 热点需开启「最大兼容性」。")
                .font(.caption).foregroundStyle(.secondary)
        }.disabled(link.configurationBusy || writing)
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
                            if ssid == row.ssid {
                                password = ""
                                if wifiDraft.saved.ssid == row.ssid {
                                    wifiDraft.accept(WiFiConfiguration(ssid: row.ssid))
                                }
                            }
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
        .frame(height: 48)
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
                        if showPassword { TextField(passwordPlaceholder, text: Binding(get: { password }, set: { password = $0 })) }
                        else { SecureField(passwordPlaceholder, text: Binding(get: { password }, set: { password = $0 })) }
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
            VStack(alignment: .leading, spacing: 12) {
            HStack {
                sectionTitle("助手", symbol: "bubble.left.and.bubble.right")
                Spacer()
                draftIndicator(agentChanged)
            }
            if !link.agents.isEmpty {
                Picker("OpenClaw", selection: Binding(get: { agentPick }, set: selectAgent)) {
                    ForEach(link.agents) { agent in
                        Text(agentPickerTitle(agent)).tag(agent.id)
                    }
                    Text("添加新的 OpenClaw").tag(-1)
                }
            }
            if let agent = pickedSaved {
                HStack {
                    Label(agent.state == "ready" ? "已配对" : "等待配对完成",
                          systemImage: agent.state == "ready" ? "checkmark.shield" : "clock")
                        .font(.caption).foregroundStyle(.secondary)
                    Spacer()
                    Button(role: .destructive) { dropping = agent } label: { Image(systemName: "trash") }
                        .buttonStyle(.borderless).help("删除设备上的这套 OpenClaw")
                }
                Text(agent.url).font(.system(.caption, design: .monospaced))
                    .foregroundStyle(.secondary).textSelection(.enabled)
                    .fixedSize(horizontal: false, vertical: true)
            }
            DisclosureGroup(isExpanded: Binding(
                get: { agentPick == -1 || showPairing }, set: { showPairing = $0 }
            )) {
                pairingForm.padding(.top, 8)
            } label: { Text(pickedSaved == nil ? "连接新的 OpenClaw" : "重新配对").font(.callout.weight(.medium)) }
            Divider()
            DisclosureGroup("连接方式 · \(routeForm.enabled ? "Shadowsocks" : "直连")", isExpanded: $showRoute) {
                routeRow.padding(.top, 8)
            }.font(.callout)
            }.disabled(link.configurationBusy || writing)
            Divider()
            DisclosureGroup("连接检测", isExpanded: $showDiagnostics) {
                diagnostics.padding(.top, 8)
            }.font(.callout)
        }
    }

    private var pairingForm: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Text("从 OpenClaw 获取一次性配对码").font(.caption).foregroundStyle(.secondary)
                Spacer()
                Button {
                    PairPrompt.copyToPasteboard()
                    copied = true
                } label: {
                    Label(copied ? "已复制" : "复制提示词",
                          systemImage: copied ? "checkmark" : "doc.on.doc")
                }
                .buttonStyle(.borderless).font(.callout)
                .foregroundStyle(copied ? .green : accent)
                .help(copied ? "贴给 OpenClaw，让它生成配对码" : "复制配对提示词，然后贴给你的 OpenClaw")
            }
            if copied {
                Text("贴给 OpenClaw，再把回复的配对码粘贴到下面。")
                    .font(.caption).foregroundStyle(.secondary)
            }
            VStack(alignment: .leading, spacing: 7) {
                Text("配对码").font(.callout.weight(.medium))
                ZStack(alignment: .topLeading) {
                    if setupCode.isEmpty {
                        Text("粘贴 OpenClaw 回复的完整配对码")
                            .foregroundStyle(.tertiary).padding(.horizontal, 5).padding(.vertical, 8)
                            .allowsHitTesting(false)
                    }
                    TextEditor(text: Binding(get: { setupCode }, set: { setupCode = $0 }))
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
                        Label(pairingChanged ? "已识别配对码，尚未保存" : "此配对码已保存，无需重复提交",
                              systemImage: "checkmark.circle.fill")
                            .foregroundStyle(pairingChanged ? accent : .green).font(.caption)
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
            HStack {
                testResult(probeText, busy: probe == .testing)
                Spacer(minLength: 4)
                Button("测试地址") {
                    probe = .testing
                    prober.run(url: displayURL, token: "") { probe = $0 }
                }
                .disabled(!PairPrompt.validGateway(displayURL) || probe == .testing || writing || invalidCode)
                .help("从这台 Mac 检查网关响应，不使用也不消耗配对码")
            }
        }
    }

    private var diagnostics: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("设备连接检测").font(.callout.weight(.medium))
                Spacer()
                if link.deviceCheck?.running == true {
                    ProgressView().controlSize(.small)
                    Button(link.deviceCheck == .cancelling ? "正在停止…" : "停止") { link.cancelDeviceCheck() }
                        .disabled(link.deviceCheck == .cancelling)
                } else {
                    Button("检测当前助手") { link.checkDevice() }
                        .disabled(!link.loaded || !link.supportsDeviceControl || link.currentAgent == nil ||
                                  writing || link.tried == .trying || link.configurationBusy)
                }
            }
            Text(deviceCheckText).font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    private var deviceCheckText: String {
        switch link.deviceCheck {
        case .checking: return "正在通过设备当前 Wi-Fi 和连接方式检测「\(link.currentAgent?.name ?? "OpenClaw")」，不会录音或发起模型对话。"
        case .cancelling: return "正在停止检测，等待设备释放连接。"
        case .report(let report): return report.summary + "不代表实际语音收发已验收。"
        case .failed(let reason): return reason
        case .cancelled: return "检测已停止。"
        case nil:
            return link.loaded && !link.supportsDeviceControl ? "更新设备固件后可使用音量同步与设备端检测。" :
                "检测设备当前助手的网关认证、提问会话和通话服务配置。"
        }
    }

    private var routeRow: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("连接方式").font(.callout.weight(.medium))
                Spacer()
                Picker("连接方式", selection: routeBinding(\.enabled)) {
                    Text("直连").tag(false)
                    Text("Shadowsocks").tag(true)
                }
                .pickerStyle(.segmented).labelsHidden().fixedSize()
            }
            if routeForm.enabled {
                HStack(spacing: 10) {
                    TextField("服务器", text: routeBinding(\.host)).accessibilityLabel("Shadowsocks 服务器")
                    TextField("端口", text: routeBinding(\.port)).frame(width: 70).accessibilityLabel("Shadowsocks 端口")
                }.textFieldStyle(.roundedBorder)
                HStack(spacing: 10) {
                    SecureField(routeForm.hasSecret ? "留空沿用已保存的密码" : "密码", text: routeBinding(\.secret))
                        .textFieldStyle(.roundedBorder).accessibilityLabel("Shadowsocks 密码")
                    Text("aes-256-cfb").font(.caption).foregroundStyle(.secondary)
                }
            }
            HStack {
                Text(routeForm.enabled ? "这套 OpenClaw 经 Shadowsocks 连接。" : "这套 OpenClaw 直接连接，适合国内地址。")
                    .font(.caption).foregroundStyle(.secondary)
            }
        }
        .disabled(writing || link.configurationBusy)
    }

    private func routeBinding<Value>(_ keyPath: WritableKeyPath<RouteForm, Value>) -> Binding<Value> {
        Binding(get: { routeForm[keyPath: keyPath] }, set: { routeForm[keyPath: keyPath] = $0 })
    }

    private func draftIndicator(_ dirty: Bool) -> some View {
        Text(dirty ? "未保存" : link.loaded ? "与设备一致" : "等待设备")
            .font(.caption).foregroundStyle(dirty ? Color.orange : .secondary)
            .padding(.horizontal, 7).padding(.vertical, 3)
            .background(dirty ? Color.orange.opacity(0.10) : Color.primary.opacity(0.04), in: Capsule())
    }

    private func agentPickerTitle(_ agent: MacAgent) -> String {
        let key = "saved:\(agent.name)"
        let dirty = routeDrafts[key]?.isDirty == true ||
            pairingDrafts[key].map { $0.isDirty && !$0.value.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty } == true
        return agent.name + (agent.current ? "（正在使用）" : "") + (dirty ? " · 未保存" : "")
    }

    private var parsed: (url: String, code: String)? { PairPrompt.decode(payload: setupCode) }
    private var invalidCode: Bool { !setupCode.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && parsed == nil }
    private var displayURL: String { (pairingChanged ? parsed?.url : nil) ?? pickedSaved?.url ?? parsed?.url ?? gatewayURL }
    private var pickedSaved: MacAgent? { link.agents.first { $0.id == agentPick } }
    private var writing: Bool { link.result == .writing || configurationSave != nil }
    private var wifiReady: Bool {
        guard let choice, choice.reach != .only5, choice.ssid.utf8.count <= 32 else { return false }
        return !choice.secure || !password.isEmpty || link.saved.contains(choice.ssid)
    }
    private var newAgentName: String {
        let address = pairingDrafts["new"].flatMap { PairPrompt.decode(payload: $0.value)?.url } ?? gatewayURL
        let candidate = importedName ?? (URL(string: address)?.host ?? "OpenClaw")
        var out = ""
        for character in candidate {
            if (out + String(character)).utf8.count >= 24 { break }
            out.append(character)
        }
        return out
    }

    private func prefill() {
        guard link.loaded else { return }
        if let pairing = pairingDrafts["new"], !pairing.saved.isEmpty,
           let target = link.agents.first(where: { $0.name == newAgentName }),
           PairPrompt.decode(payload: pairing.saved)?.url == target.url {
            reconcilePairing(key: "new", target: target)
        }
        languageDraft.receive(link.language)
        if let device = link.device { volumeDraft.receive(device.volume) }
        let network = link.savedCurrent ?? wifiDraft.saved.ssid.flatMap { link.saved.contains($0) ? $0 : nil }
        let storedPassword = network == wifiDraft.saved.ssid ? wifiDraft.saved.password :
            network.flatMap { WifiMemory.password(for: $0) } ?? ""
        wifiDraft.receive(WiFiConfiguration(ssid: network, password: storedPassword))
        if configurationSave != nil || (agentPick == -1 && (pairingChanged || routeChanged)) {
            selectionDraft.accept(link.currentAgent?.id ?? -1)
        } else {
            selectionDraft.receive(link.currentAgent?.id ?? -1)
        }
        for agent in link.agents {
            let key = "saved:\(agent.name)"
            var draft = routeDrafts[key] ?? ConfigurationDraft(RouteForm(agent.route))
            draft.receive(draft.saved.refreshed(agent.route))
            routeDrafts[key] = draft
        }
        if dropped {
            dropped = false
            selectionDraft.discard()
        }
        if !filled, ssid == nil, let current = wifi.current, choices.contains(where: { $0.ssid == current && $0.reach == .near }) {
            ssid = current
        }
        if !filled {
            filled = true
        }
    }

    private func discardWiFi() {
        passwordDrafts.removeAll()
        wifiDraft.discard()
        if let ssid { passwordDrafts[ssid] = password }
        manualNetwork = false
        manualSSID = ""
        keychainNote = nil
        showPassword = false
        link.tried = nil
        if link.writeSection == .wifi { link.result = nil }
    }

    private func discardConfiguration() {
        discardWiFi()
        for key in Array(routeDrafts.keys) { routeDrafts[key]?.discard() }
        for key in Array(pairingDrafts.keys) { pairingDrafts[key]?.discard() }
        selectionDraft.discard()
        languageDraft.discard()
        volumeDraft.discard()
        showPairing = false
        showRoute = false
        prober.cancel()
        probe = nil
        link.result = nil
        configurationSaved = false
        savedBeforeFailure = 0
    }

    private func selectAgent(_ target: Int) {
        agentPick = target
        showPairing = false
        copied = false
        prober.cancel()
        probe = nil
    }

    // MARK: Status and save

    private var footer: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(spacing: 12) {
                VStack(alignment: .leading, spacing: 3) {
                    Text(hasChanges ? "待保存：\(changedSections.joined(separator: "、"))" :
                         link.loaded ? "配置与设备一致" : "连接设备后可保存配置")
                        .font(.callout).foregroundStyle(hasChanges ? .primary : .secondary)
                    if hasChanges, let validationMessage {
                        Text(validationMessage).font(.caption).foregroundStyle(.orange)
                    }
                }
                Spacer(minLength: 4)
                Button("撤销修改", action: discardConfiguration)
                    .disabled(!hasChanges || writing)
                Button("保存配置", action: saveConfiguration)
                    .buttonStyle(.borderedProminent)
                    .keyboardShortcut("s", modifiers: .command)
                    .disabled(!link.loaded || !hasChanges || validationMessage != nil || writing ||
                              link.tried == .trying || link.configurationBusy)
                    .help("统一保存 Wi-Fi、助手、屏幕语言和音量；不更新固件")
            }
            if writing {
                HStack(spacing: 8) {
                    ProgressView().controlSize(.small)
                    Text(configurationSave?.waitingRevision != nil ? "正在确认设备已保存的配置…" : link.writeStatus)
                }
            } else if let result = link.result {
                switch result {
                case .writing:
                    HStack(spacing: 8) { ProgressView().controlSize(.small); Text(link.writeStatus) }
                case .saved:
                    Label(savedText,
                          systemImage: "checkmark.circle.fill").foregroundStyle(.green)
                case .failed(let reason):
                    Label((savedBeforeFailure > 0 ? "已保存 \(savedBeforeFailure) 项，其余修改已保留。" : "") + failure(reason),
                          systemImage: "exclamationmark.triangle.fill")
                        .foregroundStyle(.orange).fixedSize(horizontal: false, vertical: true)
                }
            }
            HStack(spacing: 10) {
                Circle().fill(link.linkKind == "usb" ? .green : link.usbPresent ? .orange : .secondary.opacity(0.4))
                    .frame(width: 8, height: 8)
                Text(connectionText).font(.callout).foregroundStyle(.secondary)
                if let status = link.device {
                    Divider().frame(height: 12)
                    Image(systemName: status.wifi ? "wifi" : "wifi.slash")
                    Text(status.wifi ? (link.savedCurrent ?? "Wi-Fi 已连接") : "Wi-Fi 未连接")
                    if let agent = link.currentAgent { Text("· \(agent.name)") }
                    if let battery = status.battery {
                        Label("\(battery)%", systemImage: "battery.100").monospacedDigit()
                    }
                }
                Spacer()
                Image(systemName: "moon.zzz").foregroundStyle(.secondary)
                    .help("闲置 60 秒后息屏，不自动关机；连接电脑 USB 时保持唤醒。USB 连接不代表正在充电。")
            }
            .font(.caption).foregroundStyle(.secondary)
        }
        .padding(.horizontal, 24).padding(.vertical, 14)
    }

    private var savedText: String {
        if configurationSaved { return "配置已保存到设备" }
        switch link.writeSection {
        case .wifi: return "Wi-Fi 已保存到设备"
        case .route: return "助手已保存到设备"
        case .volume: return "音量已保存到设备"
        case .language: return "屏幕语言已保存到设备"
        case .firmware: return "固件已更新，设备已重新连接"
        default: return "助手已保存到设备"
        }
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
        case "stale": return "设备设置有更新，已重新读取；你的修改已保留，请确认后再保存。"
        case "storage": return "设备未能保存配置，请重试。表单仍然保留。"
        case "proxy": return "Shadowsocks 服务器、端口或密码无效，请检查后重试。"
        default: return reason
        }
    }
    private var changedSections: [String] {
        var sections: [String] = []
        if wifiDraft.isDirty { sections.append("Wi-Fi") }
        if unsavedAgents { sections.append("助手") }
        if languageDraft.isDirty { sections.append("屏幕语言") }
        if volumeDraft.isDirty { sections.append("音量") }
        return sections
    }

    private var agentEdits: [AgentConfigurationEdit] {
        let keys = Set(routeDrafts.keys).union(pairingDrafts.keys)
        return keys.sorted().compactMap { key in
            let pairing = pairingDrafts[key]
            let code = pairing.flatMap {
                $0.isDirty && !$0.value.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty ? $0.value : nil
            }
            let route = routeDrafts[key].flatMap { $0.isDirty ? $0.value : nil }
            guard code != nil || route != nil else { return nil }
            let name = key == "new" ? newAgentName : String(key.dropFirst("saved:".count))
            return AgentConfigurationEdit(key: key, name: name, code: code, route: route)
        }
    }

    private var validationMessage: String? {
        if wifiDraft.isDirty && !wifiReady { return "请选择 2.4 GHz 网络并填写密码。" }
        for edit in agentEdits {
            if let code = edit.code, PairPrompt.decode(payload: code) == nil {
                return "请检查「\(edit.name)」的配对码。"
            }
            if edit.key == "new" && edit.code == nil { return "请填写新助手的配对码。" }
            if edit.key == "new", link.agents.contains(where: { $0.name == edit.name }) {
                return "设备上已有「\(edit.name)」，请在列表中选择它并重新配对。"
            }
            if edit.key != "new", !link.agents.contains(where: { "saved:\($0.name)" == edit.key }) {
                return "「\(edit.name)」已从设备移除，请撤销修改后重试。"
            }
            if let route = edit.route {
                if !link.supportsRoute { return "连接方式设置需要先在「设备维护」中更新固件。" }
                if !route.settings.valid { return "请检查「\(edit.name)」的 Shadowsocks 设置。" }
            }
        }
        if volumeDraft.isDirty && (!link.supportsDeviceControl || link.device == nil) {
            return "音量设置需要连接支持设备控制的固件。"
        }
        return nil
    }

    private func saveConfiguration() {
        guard link.loaded, hasChanges, validationMessage == nil, !writing,
              link.tried != .trying, !link.configurationBusy else { return }
        let edits = agentEdits
        let needsSelection = selectionChanged || edits.contains { $0.code != nil }
        let selection = needsSelection ? pickedSaved?.name ?? (edits.contains { $0.key == "new" } ? newAgentName : nil) : nil
        let pending = ConfigurationSave.plan(agents: edits, selection: selection,
            language: languageDraft.isDirty ? languageDraft.value : nil,
            volume: volumeDraft.isDirty ? volumeDraft.value : nil,
            wifi: wifiDraft.isDirty ? wifiDraft.value : nil)
        guard pending.current != nil else { return }
        configurationSaved = false
        savedBeforeFailure = 0
        configurationSave = pending
        sendConfigurationStep()
    }

    private func sendConfigurationStep() {
        guard let pending = configurationSave else { return }
        guard let step = pending.current else {
            configurationSave = nil
            configurationSaved = true
            link.result = .saved
            return
        }
        guard link.loaded, !link.configurationBusy, link.tried != .trying else {
            link.result = .failed("设备暂时无法保存。未完成的修改已保留，请稍后重试。")
            return
        }
        switch step {
        case .pairing(_, let name, let code):
            guard let parsed = PairPrompt.decode(payload: code) else {
                link.result = .failed("code")
                return
            }
            UserDefaults.standard.set(parsed.url, forKey: "gatewayURL")
            link.saveAgent(setupCode: parsed.code, name: name)
        case .route(let name, let value):
            guard let agent = link.agents.first(where: { $0.name == name }) else {
                link.result = .failed("设备未返回这套助手配置。修改已保留，请重新连接后确认。")
                return
            }
            link.setRoute(agent, route: value.settings)
        case .selection(let name):
            guard let agent = link.agents.first(where: { $0.name == name }) else {
                link.result = .failed("设备未返回所选助手，未完成的修改已保留。")
                return
            }
            link.useAgent(agent)
        case .language(let language): link.setLanguage(language)
        case .volume(let volume): link.setVolume(volume)
        case .wifi(let value):
            guard let ssid = value.ssid else { link.result = .failed("请选择网络。"); return }
            link.saveWiFi(ssid: ssid, password: value.password)
        }
        if configurationSave != nil && link.result != .writing {
            link.result = .failed("设备暂时无法保存。未完成的修改已保留，请重新连接后重试。")
        }
    }

    private func handleWriteResult(_ result: WriteResult?) {
        if case .failed = result {
            savedBeforeFailure = configurationSave?.acknowledgedCount ?? 0
            configurationSave = nil
            saveReadbackTimer?.invalidate()
            return
        }
        guard result == .saved, var pending = configurationSave, let step = pending.current,
              pending.acknowledge(revision: link.configurationRevision) else { return }
        configurationSave = pending
        switch step {
        case .pairing(let key, _, let code): pairingDrafts[key]?.accept(code)
        case .route(let name, let value):
            routeDrafts["saved:\(name)"] = ConfigurationDraft(value.acknowledged())
        case .language(let language): languageDraft.accept(language)
        case .volume(let volume): volumeDraft.accept(volume)
        case .wifi(let value):
            wifiDraft.accept(value)
            if let network = value.ssid, !value.password.isEmpty {
                WifiMemory.remember(value.password, for: network)
                remembered = WifiMemory.ssids()
            }
        case .selection: break
        }
        saveReadbackTimer?.invalidate()
        saveReadbackTimer = Timer.scheduledTimer(withTimeInterval: 10, repeats: false) { _ in
            guard configurationSave != nil else { return }
            link.result = .failed("保存已获确认，但未能读回完整配置。请重新连接后检查。")
        }
    }

    private func continueConfigurationSave() {
        guard link.loaded, var pending = configurationSave, let step = pending.current,
              pending.readBack(revision: link.configurationRevision) else { return }
        saveReadbackTimer?.invalidate()
        if case .pairing(let key, let name, _) = step {
            guard let target = link.agents.first(where: { $0.name == name }) else {
                link.result = .failed("设备未返回这套助手配置。修改已保留，请重新连接后确认。")
                return
            }
            reconcilePairing(key: key, target: target)
        }
        if case .selection(let name) = step {
            guard let target = link.currentAgent, target.name == name else {
                link.result = .failed("设备未能切换到所选助手，请重试。")
                return
            }
            selectionDraft.accept(target.id)
        }
        configurationSave = pending
        sendConfigurationStep()
    }

    private func reconcilePairing(key: String, target: MacAgent) {
        let targetKey = "saved:\(target.name)"
        guard key != targetKey else { return }
        if let draft = pairingDrafts[key] { pairingDrafts[targetKey] = draft }
        if let draft = routeDrafts[key] { routeDrafts[targetKey] = draft }
        pairingDrafts.removeValue(forKey: key)
        routeDrafts.removeValue(forKey: key)
        if agentPick == -1 { agentPick = target.id }
    }

    private func updateFirmware() {
        guard !writing, link.tried != .trying, !link.configurationBusy, let firmware,
              !firmware.matches(link.build), link.linkKind == "usb", let port = link.usbPort else { return }
        link.writeSection = .firmware
        link.result = .writing
        link.pauseForFlash {
            Thread.detachNewThread {
                let error = FirmwareFlasher.write(port: port, built: firmware)
                DispatchQueue.main.async {
                    if let error {
                        self.link.resumeAfterFlash()
                        self.link.result = .failed(error)
                        return
                    }
                    self.link.resumeAfterFlash { ready in
                        self.link.result = ready && firmware.matches(self.link.build) ? .saved :
                            .failed("固件已写入，USB 尚未恢复。请重新连接；表单已保留。")
                    }
                }
            }
        }
    }
}
