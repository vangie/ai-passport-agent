import Foundation

enum L10n {
    static func language(for preferences: [String] = Locale.preferredLanguages) -> String {
        Bundle.preferredLocalizations(from: ["en", "zh-Hans"], forPreferences: preferences).first ?? "en"
    }

    static func string(_ key: String, _ arguments: String...) -> String {
        text(key, arguments: arguments)
    }

    static func text(_ key: String, arguments: [String] = [],
                     preferences: [String] = Locale.preferredLanguages, bundle: Bundle = .main) -> String {
        let language = language(for: preferences)
        let localized = bundle.path(forResource: language, ofType: "lproj").flatMap(Bundle.init(path:)) ?? bundle
        let format = localized.localizedString(forKey: key, value: key, table: "Localizable")
        guard !arguments.isEmpty else { return format }
        return String(format: format, locale: Locale(identifier: language), arguments: arguments)
    }
}
