import Foundation

@main struct AppLocalizationTests {
    static func main() throws {
        let root = URL(fileURLWithPath: CommandLine.arguments[1])
        precondition(L10n.language(for: ["en-US"]) == "en")
        precondition(L10n.language(for: ["zh-Hans-CN", "en-US"]) == "zh-Hans")
        precondition(L10n.language(for: ["en-GB", "zh-Hans"]) == "en")
        precondition(L10n.language(for: ["fr-FR", "zh-Hans"]) == "zh-Hans")
        precondition(L10n.language(for: ["de-DE"]) == "en")
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString + ".bundle")
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: directory) }
        let info: [String: Any] = ["CFBundleIdentifier": "test.passport.localization",
                                  "CFBundleDevelopmentRegion": "en", "CFBundleLocalizations": ["en", "zh-Hans"]]
        let infoData = try PropertyListSerialization.data(fromPropertyList: info, format: .xml, options: 0)
        try infoData.write(to: directory.appendingPathComponent("Info.plist"))
        var tables: [String: [String: String]] = [:]
        for language in ["en", "zh-Hans"] {
            let source = root.appendingPathComponent("shared/\(language).lproj")
            try FileManager.default.copyItem(at: source, to: directory.appendingPathComponent("\(language).lproj"))
            let data = try Data(contentsOf: source.appendingPathComponent("Localizable.strings"))
            tables[language] = try PropertyListSerialization.propertyList(from: data, options: [], format: nil) as? [String: String]
        }
        let english = tables["en"]!
        let chinese = tables["zh-Hans"]!
        precondition(english.count >= 328 && Set(english.keys) == Set(chinese.keys))
        for (key, value) in english {
            precondition(!value.isEmpty && chinese[key] == key)
            precondition(key.components(separatedBy: "%@").count == value.components(separatedBy: "%@").count)
        }
        let bundle = Bundle(path: directory.path)!
        precondition(L10n.text("保存配置", preferences: ["en-US"], bundle: bundle) == "Save Changes")
        precondition(L10n.text("中文", preferences: ["en-US"], bundle: bundle) == "Simplified Chinese")
        precondition(L10n.text("搜索", preferences: ["en-US"], bundle: bundle) == "Scan")
        precondition(L10n.text("搜索 Wi-Fi", preferences: ["en-US"], bundle: bundle) == "Scan for Wi-Fi networks")
        precondition(L10n.text("重新扫描", preferences: ["en-US"], bundle: bundle) == "Scan Again")
        precondition(L10n.text("正在寻找附近的 Wi-Fi…", preferences: ["en-US"], bundle: bundle) == "Scanning for nearby Wi-Fi…")
        precondition(L10n.text("正在查找附近网络…", preferences: ["en-US"], bundle: bundle) == "Scanning for nearby networks…")
        precondition(L10n.text("保存配置", preferences: ["zh-Hans"], bundle: bundle) == "保存配置")
        precondition(L10n.text("已发现 %@ 个网络，继续扫描中…", arguments: ["3"], preferences: ["en"], bundle: bundle)
                     == "Found 3 networks, still scanning…")
        precondition(L10n.text("正在连接 %@", arguments: ["Home % Wi-Fi"], preferences: ["en"], bundle: bundle)
                     == "Connecting to Home % Wi-Fi")
        precondition(L10n.text("unknown-key", preferences: ["en"], bundle: bundle) == "unknown-key")
        precondition(PairPrompt.text(for: ["en-US"]) == PairPrompt.englishText)
        precondition(PairPrompt.text(for: ["zh-Hans"]) == PairPrompt.chineseText)
        for text in [PairPrompt.chineseText, PairPrompt.englishText] {
            for command in ["https://github.com/vangie/ai-passport-agent", "SHA256SUMS.txt",
                            "openclaw plugins install --link --accept-capabilities", "passport.status",
                            "node doctor.mjs", "/passport/v1", "gateway.auth.token",
                            "openclaw qr --json --no-ascii --limited --url", "gatewayUrl", "setupCode"] {
                precondition(text.contains(command), "Missing bilingual setup requirement: \(command)")
            }
        }
        precondition(!PairPrompt.englishText.unicodeScalars.contains { (0x3400...0x9fff).contains($0.value) })
        for platform in ["ios/Passport", "mac/PassportMac"] {
            for language in ["en", "zh-Hans"] {
                let file = root.appendingPathComponent("\(platform)/\(language).lproj/InfoPlist.strings")
                let data = try Data(contentsOf: file)
                let entries = try PropertyListSerialization.propertyList(from: data, options: [], format: nil) as! [String: String]
                precondition(entries.count == 2 && entries.values.allSatisfy { !$0.isEmpty })
            }
        }
        print("App localization: language negotiation, English fallback, matching keys, formatting, permissions and bilingual prompts PASS")
    }
}
