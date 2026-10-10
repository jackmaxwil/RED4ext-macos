// Move the mouse in a process without bringing it to the front: mouse-moved events with the given deltas go to that
// process's event queue only, so the user's pointer is untouched (the system cursor does not move).
// Usage: swift mousemove.swift <pid> <dx per step> <dy per step> <steps> <milliseconds between steps>
import CoreGraphics
import Foundation

let args = CommandLine.arguments
guard args.count >= 6, let pid = Int32(args[1]), let dx = Int64(args[2]), let dy = Int64(args[3]),
      let steps = Int(args[4]), let ms = UInt32(args[5]) else {
    print("usage: mousemove.swift <pid> <dx> <dy> <steps> <ms>")
    exit(2)
}
let at = CGEvent(source: nil)?.location ?? .zero
for _ in 0..<steps {
    if let e = CGEvent(mouseEventSource: nil, mouseType: .mouseMoved, mouseCursorPosition: at, mouseButton: .left) {
        e.timestamp = CGEventTimestamp(clock_gettime_nsec_np(CLOCK_UPTIME_RAW)) // the time of the movement, like real input
        e.setIntegerValueField(.mouseEventDeltaX, value: dx)
        e.setIntegerValueField(.mouseEventDeltaY, value: dy)
        e.postToPid(pid)
    }
    usleep(ms * 1000)
}
