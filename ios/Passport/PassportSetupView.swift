import SwiftUI

struct PassportSetupView: View {
    @ObservedObject var link: PassportLink
    @ObservedObject var incoming: ConfigurationInbox
    @State private var draft = PhoneDraft()
    @State private var showPassword = false
    @State private var showPairing = false
    @State private var scanningCode = false
    @State private var copied = false
    @State private var forgetting: PassportNetwork?
    @State private var showNearby = false
    @State private var showSavedNetworks = false
    @State private var showNetworkOptions = false

    var body: some View {
        NavigationStack {
            Form {
                connectionSection
                if let error = link.error {
                    Section {
                        Label(error, systemImage: "exclamationmark.triangle")
                            .foregroundStyle(.orange)
                    }
                }
                wifiSection
                assistantSection
                deviceSection
                saveSection
            }
            .listSectionSpacing(20)
            .scrollDismissesKeyboard(.interactively)
            .navigationTitle("AI Passport")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItemGroup(placement: .keyboard) {
                    Spacer()
                    Button("完成") { UIApplication.shared.sendAction(#selector(UIResponder.resignFirstResponder), to: nil, from: nil, for: nil) }
                }
            }
            .onReceive(link.$acknowledged) { mutation in
                mutation?.accept(into: &draft)
            }
            .onChange(of: link.snapshotRevision, initial: true) { _, _ in draft.receive(link.snapshot) }
            .onReceive(incoming.$pending) { configuration in
                guard let configuration else { return }
                if let address = configuration.gatewayURL { draft.value.pair.url = address }
                if let code = configuration.setupCode { draft.value.pair.code = code }
                if let name = configuration.name { draft.value.pair.name = name }
                if let ssid = configuration.ssid { draft.value.wifi.ssid = ssid }
                if let password = configuration.password { draft.value.wifi.password = password }
                if configuration.setupCode != nil { showPairing = true }
                incoming.pending = nil
            }
            .alert("无法导入配置", isPresented: Binding(
                get: { incoming.error != nil }, set: { if !$0 { incoming.error = nil } }
            )) { Button("好", role: .cancel) {} }
            message: { Text(incoming.error ?? "") }
            .sheet(isPresented: $showNearby) {
                NearbyNetworksView(link: link) { ssid in selectNetwork(ssid) }
            }
            .confirmationDialog("从 Passport 忘记这个 Wi-Fi？", isPresented: Binding(
                get: { forgetting != nil }, set: { if !$0 { forgetting = nil } }
            ), titleVisibility: .visible) {
                if let network = forgetting {
                    Button("忘记 \(network.ssid)", role: .destructive) { link.forget(network) }
                }
            } message: { Text("不会修改手机的 Wi-Fi，也不会删除其他已保存网络。") }
            .sheet(isPresented: $scanningCode) {
                NavigationStack {
                    CodeScanner { payload in
                        if let pair = PairPrompt.decode(payload: payload) {
                            draft.value.pair.url = pair.url
                            draft.value.pair.code = pair.code
                        }
                        scanningCode = false
                    }
                    .ignoresSafeArea()
                    .navigationTitle("扫描 OpenClaw 配对二维码")
                    .navigationBarTitleDisplayMode(.inline)
                    .toolbar { ToolbarItem(placement: .cancellationAction) { Button("取消") { scanningCode = false } } }
                }
            }
        }
        .tint(.indigo)
    }

    private var connectionSection: some View {
        Section {
            HStack(spacing: 12) {
                Image(systemName: "lanyardcard")
                    .font(.title2)
                    .foregroundStyle(.indigo)
                    .frame(width: 48, height: 48)
                    .background(.indigo.opacity(0.08), in: RoundedRectangle(cornerRadius: 12))
                VStack(alignment: .leading, spacing: 4) {
                    Text(link.ready || link.connecting ? link.deviceName : "连接设备").font(.headline)
                    Text(link.status).font(.subheadline).foregroundStyle(.secondary)
                }
                Spacer(minLength: 0)
                if link.busy || link.connecting || link.scanning {
                    ProgressView()
                } else if link.ready {
                    Image(systemName: "checkmark.circle.fill")
                        .foregroundStyle(.green)
                        .accessibilityLabel("蓝牙已连接")
                }
                if link.ready || link.connecting {
                    Menu {
                        if link.ready {
                            Button("重新读取配置", systemImage: "arrow.clockwise") { link.reload() }
                        }
                        Button("断开蓝牙", systemImage: "xmark.circle") { link.disconnect() }
                    } label: {
                        Image(systemName: "ellipsis")
                            .frame(minWidth: 44, minHeight: 44)
                    }
                    .accessibilityLabel("设备操作")
                    .disabled(link.busy)
                }
            }
            .padding(.vertical, 6)
            if !link.ready && !link.connecting {
                ForEach(link.devices) { device in
                    Button { link.connect(device) } label: {
                        HStack {
                            Label(device.name, systemImage: "lanyardcard")
                            Spacer()
                            Text(String(device.id.uuidString.suffix(6))).font(.caption.monospaced()).foregroundStyle(.secondary)
                            Image(systemName: "chevron.right").font(.caption)
                        }
                    }
                }
                Button(link.scanning ? "正在搜索设备" : "搜索附近的 Passport", systemImage: "magnifyingglass") {
                    link.search()
                }
                .disabled(link.scanning)
            }
        } footer: {
            if !link.ready {
                Text("在 Passport 的「设置」中开启蓝牙，然后选择设备。")
            } else {
                Text("配置保存在设备上，修改后统一保存。")
            }
        }
    }

    private var wifiSection: some View {
        Section {
            if !link.snapshot.networks.isEmpty {
                DisclosureGroup(isExpanded: $showSavedNetworks) {
                    savedNetworkRows
                } label: {
                    HStack {
                        Text("已保存的网络")
                        Spacer()
                        Text("\(link.snapshot.networks.count)")
                            .foregroundStyle(.secondary).monospacedDigit()
                    }
                }
                .listRowInsets(EdgeInsets(top: 0, leading: 16, bottom: 0, trailing: 16))
            }
            WiFiSetupField("网络名称") {
                HStack(spacing: 8) {
                    TextField("输入或选择 Wi-Fi", text: $draft.value.wifi.ssid)
                        .textInputAutocapitalization(.never).autocorrectionDisabled()
                        .accessibilityLabel("Wi-Fi 名称")
                        .accessibilityIdentifier("wifi.ssid")
                    networkSearchButton
                }
            }
            .listRowInsets(EdgeInsets(top: 2, leading: 16, bottom: 2, trailing: 12))
            if !draft.value.wifi.open {
                WiFiSetupField("密码") {
                    HStack(spacing: 8) {
                        Group {
                            if showPassword { TextField(passwordPrompt, text: $draft.value.wifi.password) }
                            else { SecureField(passwordPrompt, text: $draft.value.wifi.password) }
                        }
                        .textInputAutocapitalization(.never).autocorrectionDisabled()
                        .accessibilityLabel("Wi-Fi 密码")
                        .accessibilityIdentifier("wifi.password")
                        .privacySensitive()
                        Button { showPassword.toggle() } label: {
                            Image(systemName: showPassword ? "eye.slash" : "eye")
                                .frame(minWidth: 44, minHeight: 44)
                        }
                        .buttonStyle(.borderless)
                        .accessibilityLabel(showPassword ? "隐藏密码" : "显示密码")
                    }
                }
                .listRowInsets(EdgeInsets(top: 2, leading: 16, bottom: 2, trailing: 12))
            }
            if !knownNetwork {
                DisclosureGroup("网络选项", isExpanded: $showNetworkOptions) {
                    Toggle("无密码网络", isOn: $draft.value.wifi.open)
                        .onChange(of: draft.value.wifi.open) { _, open in if open { draft.value.wifi.password = "" } }
                }
                .listRowInsets(EdgeInsets(top: 0, leading: 16, bottom: 0, trailing: 16))
            }
        } header: { Label("Wi-Fi", systemImage: "wifi") }
        footer: { Text("仅支持 2.4 GHz，不会切换手机网络。iPhone 热点请开启「允许其他人加入」和「最大兼容性」。") }
        .environment(\.defaultMinListRowHeight, 44)
        .disabled(link.busy)
    }

    private var networkSearchButton: some View {
        Button {
            showNearby = true
        } label: {
            HStack(spacing: 6) {
                Image(systemName: "magnifyingglass")
                Text("搜索").fixedSize()
            }
            .font(.subheadline.weight(.medium))
            .fixedSize(horizontal: true, vertical: false)
            .frame(minWidth: 44, minHeight: 44)
        }
        .buttonStyle(.borderless)
        .fixedSize(horizontal: true, vertical: false)
        .disabled(!link.ready)
        .accessibilityLabel("搜索 Wi-Fi")
        .accessibilityIdentifier("wifi.search")
    }

    private var savedNetworkRows: some View {
        VStack(spacing: 0) {
            ForEach(link.snapshot.networks) { network in
                HStack(spacing: 8) {
                    Button { selectNetwork(network.ssid) } label: {
                        HStack(spacing: 8) {
                            Image(systemName: draft.value.wifi.ssid == network.ssid ? "checkmark.circle.fill" : "circle")
                            Text(network.ssid).foregroundStyle(.primary).lineLimit(1)
                            Spacer(minLength: 4)
                            if network.current {
                                Text("已连接").font(.caption).foregroundStyle(.secondary)
                            }
                        }
                        .frame(minHeight: 44)
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.borderless)
                    .accessibilityIdentifier("wifi.saved.\(network.id)")
                    .accessibilityValue(draft.value.wifi.ssid == network.ssid ? "已选择" : "未选择")
                    Menu {
                        Button("忘记网络", role: .destructive) { forgetting = network }
                    } label: {
                        Image(systemName: "ellipsis")
                            .frame(minWidth: 44, minHeight: 44)
                    }
                    .accessibilityLabel("\(network.ssid) 的操作")
                }
                if network.id != link.snapshot.networks.last?.id {
                    Divider().padding(.leading, 28)
                }
            }
        }
        .listRowInsets(EdgeInsets(top: 0, leading: 16, bottom: 0, trailing: 12))
    }

    private var assistantSection: some View {
        Section {
            ForEach(link.snapshot.agents) { agent in
                HStack(spacing: 12) {
                    Image(systemName: "bubble.left.and.bubble.right").foregroundStyle(.indigo)
                    VStack(alignment: .leading, spacing: 4) {
                        Text(agent.name).font(.body.weight(.medium))
                        Text(URL(string: agent.url)?.host ?? "OpenClaw 网关")
                            .font(.caption).foregroundStyle(.secondary).lineLimit(1)
                    }
                    Spacer(minLength: 4)
                    Text(agent.current ? "当前助手" : "已配对")
                        .font(.caption).foregroundStyle(.secondary)
                }
                .padding(.vertical, 4)
            }
            DisclosureGroup(link.snapshot.agents.isEmpty ? "配对 OpenClaw" : "添加助手", isExpanded: $showPairing) {
                VStack(alignment: .leading, spacing: 12) {
                    Text("复制提示词发给 OpenClaw，再扫码或粘贴配对信息。")
                        .font(.footnote).foregroundStyle(.secondary)
                    SetupActions { pairingActions }
                    .buttonStyle(.bordered)
                }
                .padding(.vertical, 4)
                SetupField("助手名称") {
                    TextField("例如：春庭", text: $draft.value.pair.name)
                        .accessibilityLabel("助手名称")
                }
                SetupField("网关地址") {
                    TextField("wss://…", text: $draft.value.pair.url)
                        .textInputAutocapitalization(.never).autocorrectionDisabled().keyboardType(.URL)
                        .accessibilityLabel("网关地址")
                }
                SetupField("配对码") {
                    TextField("粘贴配对码或完整配对信息", text: $draft.value.pair.code, axis: .vertical)
                        .lineLimit(2...3)
                        .textInputAutocapitalization(.never).autocorrectionDisabled()
                        .accessibilityLabel("配对码")
                        .privacySensitive()
                        .onChange(of: draft.value.pair.code) { _, code in
                            if let pair = PairPrompt.decode(payload: code), draft.value.pair.url.isEmpty {
                                draft.value.pair.url = pair.url
                            }
                        }
                }
            }
        } header: { Label("OpenClaw", systemImage: "bubble.left.and.bubble.right") }
        .disabled(link.busy)
    }

    @ViewBuilder private var pairingActions: some View {
        Button {
            PairPrompt.copyToPasteboard()
            copied = true
        } label: {
            Label(copied ? "已复制" : "复制提示词", systemImage: copied ? "checkmark" : "doc.on.doc")
                .frame(maxWidth: .infinity, minHeight: 32)
        }
        if CodeScanner.available {
            Button { scanningCode = true } label: {
                Label("扫码配对", systemImage: "qrcode.viewfinder")
                    .frame(maxWidth: .infinity, minHeight: 32)
            }
        }
    }

    private var deviceSection: some View {
        Section {
            Picker("屏幕语言", selection: $draft.value.language) {
                Text("中文").tag("zh")
                Text("English").tag("en")
            }
            if link.ready {
                LabeledContent("固件版本") {
                    Text(link.snapshot.firmware)
                        .font(.caption.monospaced()).lineLimit(1).truncationMode(.middle)
                }
                .foregroundStyle(.secondary)
            }
        } header: { Label("设备偏好", systemImage: "slider.horizontal.3") }
        .disabled(link.busy)
    }

    private var saveSection: some View {
        Section {
            VStack(spacing: 12) {
                if draft.dirty {
                    Text("有未保存的修改").font(.subheadline.weight(.medium))
                    if let validation {
                        Text(validation).font(.footnote).foregroundStyle(.secondary)
                    } else if !link.ready {
                        Text("连接设备后即可保存，填写的内容会保留。")
                            .font(.footnote).foregroundStyle(.secondary)
                    }
                }
                Button { save() } label: {
                    HStack(spacing: 8) {
                        if link.busy { ProgressView().tint(.white) }
                        Text(link.busy ? "正在处理" : "保存配置")
                    }
                    .frame(maxWidth: .infinity, minHeight: 32)
                }
                .buttonStyle(.borderedProminent).controlSize(.large)
                .accessibilityIdentifier("configuration.save")
                .disabled(!link.ready || link.busy || !draft.dirty || validation != nil)
                if link.busy {
                    Text(link.status).font(.footnote).foregroundStyle(.secondary)
                }
                if draft.dirty {
                    Button("撤销修改") { draft.discard() }
                        .frame(maxWidth: .infinity, minHeight: 44)
                        .buttonStyle(.borderless)
                        .disabled(link.busy)
                        .accessibilityIdentifier("configuration.discard")
                }
            }
            .multilineTextAlignment(.center)
            .padding(.vertical, 4)
            .listRowBackground(Color.clear)
            .listRowInsets(EdgeInsets(top: 8, leading: 0, bottom: 8, trailing: 0))
        }
    }

    private var knownNetwork: Bool { link.snapshot.networks.contains { $0.ssid == draft.value.wifi.ssid } }
    private var passwordPrompt: String { knownNetwork ? "留空沿用已存密码" : "输入 Wi-Fi 密码" }

    private var validation: String? {
        if draft.value.wifi != draft.saved.wifi, let reason = draft.value.wifi.validation(saved: link.snapshot.networks) { return reason }
        if draft.value.pair != draft.saved.pair {
            if draft.value.pair.name.utf8.count >= 24 { return "助手名称不能超过 23 字节。" }
            if PairPrompt.resolve(url: draft.value.pair.url, code: draft.value.pair.code) == nil { return "请填写有效的 OpenClaw 配对码及匹配地址。" }
        }
        return nil
    }

    private func selectNetwork(_ ssid: String) {
        draft.value.wifi.selectNetwork(ssid)
        showNetworkOptions = false
    }

    private func save() {
        var steps: [PhoneMutation] = []
        if draft.value.pair != draft.saved.pair,
           let pair = PairPrompt.resolve(url: draft.value.pair.url, code: draft.value.pair.code) {
            let name = draft.value.pair.name.isEmpty ? "OpenClaw" : draft.value.pair.name
            steps.append(.pairing(draft.value.pair, name: name, code: pair.code))
        }
        if draft.value.language != draft.saved.language { steps.append(.language(draft.value.language)) }
        if draft.value.wifi != draft.saved.wifi { steps.append(.wifi(draft.value.wifi)) }
        link.save(steps)
    }
}

private struct WiFiSetupField<Content: View>: View {
    @Environment(\.dynamicTypeSize) private var dynamicTypeSize
    let title: String
    @ViewBuilder let content: () -> Content

    init(_ title: String, @ViewBuilder content: @escaping () -> Content) {
        self.title = title
        self.content = content
    }

    var body: some View {
        Group {
            if dynamicTypeSize >= .xxLarge {
                VStack(alignment: .leading, spacing: 4) {
                    Text(title).font(.subheadline).foregroundStyle(.secondary)
                    content()
                }
            } else {
                HStack(spacing: 12) {
                    Text(title).font(.subheadline).foregroundStyle(.secondary)
                        .frame(width: 68, alignment: .leading)
                    content()
                }
            }
        }
        .frame(minHeight: 44)
    }
}

private struct SetupActions<Content: View>: View {
    @Environment(\.dynamicTypeSize) private var dynamicTypeSize
    @ViewBuilder let content: () -> Content

    var body: some View {
        if dynamicTypeSize >= .xxLarge {
            VStack(spacing: 12, content: content)
        } else {
            HStack(spacing: 12, content: content)
        }
    }
}

private struct SetupField<Content: View>: View {
    let title: String
    let content: Content

    init(_ title: String, @ViewBuilder content: () -> Content) {
        self.title = title
        self.content = content()
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(title).font(.subheadline).foregroundStyle(.secondary)
            content
        }
        .padding(.vertical, 4)
    }
}

private struct NearbyNetworksView: View {
    @Environment(\.dismiss) private var dismiss
    @ObservedObject var link: PassportLink
    let select: (String) -> Void

    var body: some View {
        NavigationStack {
            List {
                Text("如果找不到本机的 iPhone 热点，请先进入系统「设置 → 个人热点」页面，再返回重新扫描。")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                    .listRowBackground(Color.clear)
                    .listRowSeparator(.hidden)
                    .accessibilityIdentifier("networks.hotspotHint")
                if link.networkScanInProgress {
                    HStack(spacing: 12) {
                        ProgressView()
                        Text(link.nearby.isEmpty ? "正在寻找附近的 Wi-Fi…" : "已发现 \(link.nearby.count) 个网络，继续扫描中…")
                    }
                    .accessibilityIdentifier("networks.scanning")
                } else if let error = link.error {
                    ContentUnavailableView("扫描未完成", systemImage: "exclamationmark.triangle",
                                           description: Text(error))
                        .accessibilityIdentifier("networks.error")
                } else if !link.ready {
                    ContentUnavailableView("先连接 Passport", systemImage: "antenna.radiowaves.left.and.right",
                                           description: Text("返回主页连接蓝牙后重试，也可以手动输入网络名称。"))
                } else if link.nearby.isEmpty {
                    ContentUnavailableView("未发现网络", systemImage: "wifi.slash",
                                           description: Text("请靠近网络后重新扫描，或返回手动填写。"))
                        .accessibilityIdentifier("networks.empty")
                }
                if !link.nearby.isEmpty {
                    Section {
                        ForEach(link.nearby) { network in
                            Button {
                                select(network.ssid)
                                dismiss()
                            } label: {
                                HStack {
                                    Image(systemName: network.symbol)
                                        .frame(width: 24)
                                        .foregroundStyle(network.personalHotspot ? Color.indigo : Color.secondary)
                                        .accessibilityLabel(network.personalHotspot ? "个人热点" : "Wi-Fi")
                                    Text(network.ssid).foregroundStyle(.primary)
                                    Spacer(minLength: 4)
                                    if link.snapshot.networks.contains(where: { $0.ssid == network.ssid }) {
                                        Text("已保存").font(.caption).foregroundStyle(.secondary)
                                    }
                                }
                                .frame(minHeight: 44)
                            }
                            .accessibilityIdentifier("network.\(network.ssid)")
                            .accessibilityValue(network.personalHotspot ? "个人热点" : "Wi-Fi")
                            .listRowInsets(EdgeInsets(top: 0, leading: 16, bottom: 0, trailing: 16))
                        }
                    } footer: {
                        if !link.nearby.isEmpty {
                            Text("链环图标表示检测到个人热点特征。未识别的网络仍可选择；选择后在主页填写密码并保存。")
                        }
                    }
                }
            }
            .navigationTitle("附近的 Wi-Fi")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("取消") { dismiss() }
                }
                ToolbarItem(placement: .primaryAction) {
                    Button("重新扫描", systemImage: "arrow.clockwise") { link.scanNetworks() }
                        .accessibilityIdentifier("networks.rescan")
                        .disabled(!link.ready || link.busy)
                }
            }
            .onAppear { link.scanNetworks() }
        }
    }
}
