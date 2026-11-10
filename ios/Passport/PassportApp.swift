import SwiftUI

@main
struct PassportApp: App {
    @StateObject private var link = PassportLink()
    @StateObject private var incoming = ConfigurationInbox()
    var body: some Scene {
        WindowGroup {
            PassportSetupView(link: link, incoming: incoming)
                .onOpenURL { incoming.receive($0) }
        }
    }
}
