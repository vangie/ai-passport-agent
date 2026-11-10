import AppKit
import SwiftUI

struct PassportDisclosureStyle: DisclosureGroupStyle {
    func makeBody(configuration: Configuration) -> some View {
        VStack(alignment: .leading, spacing: 0) {
            Button {
                configuration.isExpanded.toggle()
            } label: {
                HStack(spacing: 6) {
                    Image(systemName: "chevron.right")
                        .font(.caption.weight(.semibold))
                        .foregroundStyle(.secondary)
                        .rotationEffect(.degrees(configuration.isExpanded ? 90 : 0))
                        .frame(width: 12)
                    configuration.label
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .background(DisclosureHeaderCursor())
            .accessibilityValue(configuration.isExpanded ? "已展开" : "已收起")
            if configuration.isExpanded {
                configuration.content
            }
        }
    }
}

private struct DisclosureHeaderCursor: NSViewRepresentable {
    @Environment(\.isEnabled) private var isEnabled

    func makeNSView(context: Context) -> DisclosureCursorView {
        DisclosureCursorView()
    }

    func updateNSView(_ view: DisclosureCursorView, context: Context) {
        view.isEnabled = isEnabled
    }
}

class DisclosureCursorView: NSView {
    var isEnabled = true {
        didSet {
            if oldValue != isEnabled { window?.invalidateCursorRects(for: self) }
        }
    }

    override func resetCursorRects() {
        if isEnabled { addCursorRect(bounds, cursor: .pointingHand) }
    }

    override func hitTest(_ point: NSPoint) -> NSView? { nil }
}
