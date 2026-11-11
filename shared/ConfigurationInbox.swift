import Combine
import Foundation

final class ConfigurationInbox: ObservableObject {
    @Published var pending: ConfigurationLink?
    @Published var error: String?

    func receive(_ url: URL) {
        do {
            let configuration = try ConfigurationLink.parse(url)
            error = nil
            pending = configuration
        } catch {
            self.error = (error as? LocalizedError)?.errorDescription ?? L10n.string("无法导入配置。")
        }
    }
}
