import AppKit

private final class RecordingCursorView: DisclosureCursorView {
    var cursorRects: [(NSRect, NSCursor)] = []

    override func addCursorRect(_ rect: NSRect, cursor: NSCursor) {
        cursorRects.append((rect, cursor))
    }
}

@main struct DisclosureCursorTests {
    static func main() {
        let view = RecordingCursorView(frame: NSRect(x: 0, y: 0, width: 360, height: 24))
        view.resetCursorRects()
        precondition(view.cursorRects.count == 1)
        precondition(view.cursorRects[0].0 == view.bounds)
        precondition(view.cursorRects[0].1 === NSCursor.pointingHand,
                     "The whole enabled disclosure header uses the pointing hand")
        precondition(view.hitTest(NSPoint(x: 180, y: 12)) == nil,
                     "The cursor region must not intercept the disclosure button's clicks")
        view.cursorRects.removeAll()
        view.isEnabled = false
        view.resetCursorRects()
        precondition(view.cursorRects.isEmpty, "Disabled headers must not advertise a clickable cursor")
        view.isEnabled = true
        view.frame.size.width = 480
        view.resetCursorRects()
        precondition(view.cursorRects.count == 1 && view.cursorRects[0].0.width == 480,
                     "Re-enabled and resized headers register their current bounds")
        print("Mac disclosure cursor: hand, disabled state, resize and click passthrough PASS")
    }
}
