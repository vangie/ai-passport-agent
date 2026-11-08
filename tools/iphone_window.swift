import AppKit
import Foundation

struct Event: Codable {
    var type: String
    var x: Double
    var y: Double
}

let root = CommandLine.arguments.count > 1
    ? CommandLine.arguments[1] : FileManager.default.currentDirectoryPath
let shot = URL(fileURLWithPath: root).appendingPathComponent("sim/shots/iphone.png")
let events = URL(fileURLWithPath: root).appendingPathComponent("sim/shots/iphone-events.jsonl")

final class Window: NSObject, NSApplicationDelegate, NSWindowDelegate {
    let window: NSWindow
    let image = ClickView()
    var last = Date.distantPast

    override init() {
        window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 320, height: 694),
            styleMask: [.titled, .closable, .miniaturizable],
            backing: .buffered,
            defer: false
        )
        super.init()
        window.title = "工牌 iPhone"
        window.delegate = self
        image.frame = NSRect(x: 0, y: 0, width: 320, height: 694)
        image.onClick = { [weak self] point in self?.record(point) }
        window.contentView = image
        window.setFrameOrigin(NSPoint(x: 80, y: 80))
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        refresh()
        Timer.scheduledTimer(withTimeInterval: 0.35, repeats: true) { _ in self.refresh() }
    }

    func refresh() {
        let modified = (try? shot.resourceValues(forKeys: [.contentModificationDateKey]))?
            .contentModificationDate ?? .distantPast
        guard modified > last, let picture = NSImage(contentsOf: shot) else { return }
        last = modified
        image.image = picture
        image.needsDisplay = true
    }

    func record(_ point: NSPoint) {
        let bounds = image.bounds
        guard bounds.width > 0, bounds.height > 0 else { return }
        let event = Event(
            type: "tap",
            x: max(0, min(1, point.x / bounds.width)),
            y: max(0, min(1, 1 - point.y / bounds.height))
        )
        guard let data = try? JSONEncoder().encode(event) else { return }
        if !FileManager.default.fileExists(atPath: events.path) {
            FileManager.default.createFile(atPath: events.path, contents: nil)
        }
        if let handle = try? FileHandle(forWritingTo: events) {
            _ = try? handle.seekToEnd()
            try? handle.write(contentsOf: data + Data("\n".utf8))
            try? handle.close()
        }
    }
}

final class ClickView: NSView {
    var image: NSImage?
    var onClick: ((NSPoint) -> Void)?

    override var acceptsFirstResponder: Bool { true }

    override func draw(_ dirtyRect: NSRect) {
        NSColor.black.setFill()
        dirtyRect.fill()
        image?.draw(in: bounds)
    }

    override func mouseDown(with event: NSEvent) {
        onClick?(convert(event.locationInWindow, from: nil))
    }
}

let window = Window()
let application = NSApplication.shared
application.setActivationPolicy(.regular)
application.delegate = window
application.run()
