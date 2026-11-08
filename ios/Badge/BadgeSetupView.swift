import SwiftUI

final class BadgeModel: ObservableObject {
    @Published var link = BadgeLink()
}

struct BadgeSetupView: View {
    @ObservedObject var model: BadgeModel
    @ObservedObject var incoming: ConfigurationInbox
    @State private var name = ""
    @State private var gatewayURL = ""
    @State private var setupCode = ""
    @State private var ssid = ""
    @State private var password = ""
    @State private var copied = false
    @State private var scanning = false

    var body: some View {
        NavigationStack {
            Form {
                Section("AI Passport") {
                    Text(model.link.status)
                    Text("版本 \(model.link.generation)")
                        .foregroundStyle(.secondary)
                    Picker("屏幕语言", selection: Binding(
                        get: { model.link.language },
                        set: { model.link.setLanguage($0) }
                    )) {
                        Text("中文").tag("zh")
                        Text("English").tag("en")
                    }
                }
                Section("OpenClaw") {
                    if model.link.agents.isEmpty {
                        Text("AI Passport 上还没有")
                    }
                    ForEach(model.link.agents) { agent in
                        VStack(alignment: .leading, spacing: 4) {
                            Text(agent.current ? "\(agent.name)  当前" : agent.name)
                            Text(agent.url)
                                .font(.footnote)
                                .foregroundStyle(.secondary)
                            HStack {
                                Button("使用") { model.link.useAgent(agent) }
                                Button("删除", role: .destructive) { model.link.dropAgent(agent) }
                            }
                        }
                    }
                    Button(copied ? "已复制，贴给 OpenClaw" : "复制提示词给 OpenClaw") {
                        PairPrompt.copyToPasteboard()
                        copied = true
                    }
                    if CodeScanner.available {
                        Button("扫描二维码") { scanning = true }
                    }
                    TextField("地址", text: $gatewayURL, prompt: Text("wss://你的网关地址"))
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                    TextField("配对码", text: $setupCode, axis: .vertical)
                        .textInputAutocapitalization(.never)
                        .autocorrectionDisabled()
                        .onChange(of: setupCode) { _, value in
                            if let pair = PairPrompt.decode(payload: value) { gatewayURL = pair.url }
                        }
                    Text("配对码包含网关地址，粘贴后会自动填入。")
                        .font(.footnote).foregroundStyle(.secondary)
                    Button("写入 AI Passport") {
                        guard let resolved else { return }
                        let host = URL(string: resolved.url)?.host ?? "OpenClaw"
                        model.link.saveAgent(name: name.isEmpty ? host : name, setupCode: resolved.code)
                        setupCode = ""
                        gatewayURL = ""
                        copied = false
                    }
                    .disabled(resolved == nil)
                }
                .sheet(isPresented: $scanning) {
                    NavigationStack {
                        CodeScanner { payload in
                            if let filled = PairPrompt.decode(payload: payload) {
                                gatewayURL = filled.url
                                setupCode = filled.code
                            }
                            scanning = false
                        }
                        .ignoresSafeArea()
                        .navigationTitle("扫描二维码")
                        .navigationBarTitleDisplayMode(.inline)
                        .toolbar {
                            ToolbarItem(placement: .cancellationAction) {
                                Button("取消") { scanning = false }
                            }
                        }
                    }
                }
                Section("Wi-Fi") {
                    if model.link.networks.isEmpty {
                        Text("AI Passport 上还没有已保存的网络")
                    }
                    ForEach(model.link.networks) { network in
                        VStack(alignment: .leading) {
                            Text(network.name)
                            Text(network.ssid)
                                .font(.footnote)
                                .foregroundStyle(.secondary)
                        }
                        .swipeActions {
                            Button("删除", role: .destructive) { model.link.forgetNetwork(network) }
                        }
                    }
                    TextField("名称", text: $ssid)
                        .textInputAutocapitalization(.never)
                    SecureField("密码", text: $password)
                    Button("保存这套 Wi-Fi") {
                        model.link.saveNetwork(ssid: ssid, password: password)
                        password = ""
                    }
                    .disabled(ssid.isEmpty)
                }
            }
            .navigationTitle("AI Passport OpenClaw")
            .onReceive(incoming.$pending) { configuration in
                guard let configuration else { return }
                DispatchQueue.main.async {
                    guard incoming.pending == configuration else { return }
                    if let address = configuration.gatewayURL { gatewayURL = address }
                    if let code = configuration.setupCode { setupCode = code }
                    if let value = configuration.name { name = value }
                    if let network = configuration.ssid { ssid = network }
                    if let pass = configuration.password { password = pass }
                    incoming.pending = nil
                }
            }
            .alert("无法导入配置", isPresented: Binding(
                get: { incoming.error != nil }, set: { if !$0 { incoming.error = nil } }
            )) { Button("好", role: .cancel) {} }
            message: { Text(incoming.error ?? "") }
        }
    }

    private var resolved: (url: String, code: String)? {
        PairPrompt.resolve(url: gatewayURL, code: setupCode)
    }
}
