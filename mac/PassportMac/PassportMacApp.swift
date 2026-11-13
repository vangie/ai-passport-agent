import AppKit
import SwiftUI

@main
struct PassportMacApp: App {
    @StateObject private var link = PassportSocket(path: "/tmp/passport-config.sock")
    @StateObject private var wifi = WifiScan()
    @StateObject private var incoming = ConfigurationInbox()

    var body: some Scene {
        Window("AI Passport Agent", id: "main") {
            PassportMacView(link: link, wifi: wifi, incoming: incoming)
                .onOpenURL { incoming.receive($0) }
                .onAppear {
                    DispatchQueue.main.async {
                        if !UserDefaults.standard.bool(forKey: "desktopLayoutV2"),
                           let window = NSApp.windows.first {
                            window.setContentSize(NSSize(width: 880, height: 670))
                            UserDefaults.standard.set(true, forKey: "desktopLayoutV2")
                        }
                        NSApp.windows.first?.setFrameTopLeftPoint(NSPoint(x: 80, y: (NSScreen.main?.visibleFrame.maxY ?? 900) - 40))
                    }
                }
        }
        .defaultSize(width: 880, height: 670)
        .windowResizability(.contentMinSize)
    }
}
