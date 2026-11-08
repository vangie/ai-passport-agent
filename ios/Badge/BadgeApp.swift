import SwiftUI

@main
struct BadgeApp: App {
    @StateObject private var model = BadgeModel()
    @StateObject private var incoming = ConfigurationInbox()
    var body: some Scene {
        WindowGroup {
            BadgeSetupView(model: model, incoming: incoming)
                .onOpenURL { incoming.receive($0) }
        }
    }
}
