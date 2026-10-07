// Click inside the game window at a fraction of its bounds (the same fractions as a point in its screenshot).
// Usage: swift click.swift <pid> <fx> <fy>
import CoreGraphics
import Foundation

let args = CommandLine.arguments
guard args.count == 4, let pid = Int32(args[1]), let fx = Double(args[2]), let fy = Double(args[3]) else {
    print("usage: click.swift <pid> <fx> <fy>")
    exit(2)
}
let windows = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as? [[String: Any]] ?? []
guard let w = windows.first(where: { ($0[kCGWindowOwnerPID as String] as? Int32) == pid && ($0[kCGWindowLayer as String] as? Int) == 0 }),
      let b = w[kCGWindowBounds as String] as? [String: Double], let x = b["X"], let y = b["Y"], let wd = b["Width"], let h = b["Height"]
else { exit(1) }
let p = CGPoint(x: x + wd * fx, y: y + h * fy)
// The game moves its own cursor by the events' motion deltas (it ignores the absolute position), so: push it into
// the window's top-left corner, where it clamps, then move it by the target's offset from that corner.
// CP_CURSOR_SCALE converts window points to the game's cursor units: 2 on a Retina display (measured from screenshots).
let scale = Double(ProcessInfo.processInfo.environment["CP_CURSOR_SCALE"] ?? "2") ?? 2
func post(_ type: CGEventType, dx: Int64 = 0, dy: Int64 = 0) {
    guard let e = CGEvent(mouseEventSource: nil, mouseType: type, mouseCursorPosition: p, mouseButton: .left) else { return }
    e.setIntegerValueField(.mouseEventDeltaX, value: dx)
    e.setIntegerValueField(.mouseEventDeltaY, value: dy)
    e.post(tap: .cghidEventTap)
    usleep(50_000)  // the game takes one motion event per frame; faster events are lost
}
for _ in 0..<30 { post(.mouseMoved, dx: -100, dy: -100) }
var rx = (wd * fx * scale).rounded(), ry = ((h * fy - 28) * scale).rounded()  // 28: title bar
while rx > 0 || ry > 0 {
    let sx = min(rx, 20), sy = min(ry, 20)
    post(.mouseMoved, dx: Int64(sx), dy: Int64(sy))
    rx -= sx; ry -= sy
}
usleep(400_000)
post(.leftMouseDown); usleep(120_000)
post(.leftMouseUp)
print("clicked \(fx),\(fy) (scale \(scale))")
