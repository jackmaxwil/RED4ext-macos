// Press a key in a process without bringing it to the front: the key events go to that process's event queue only,
// so the user's focus, keyboard and mouse are untouched.
// Usage: swift keypress.swift <pid> <macOS virtual key code>
import CoreGraphics
import Foundation

let args = CommandLine.arguments
guard args.count == 3, let pid = Int32(args[1]), let key = CGKeyCode(args[2]) else {
    print("usage: keypress.swift <pid> <keycode>")
    exit(2)
}
for down in [true, false] {
    CGEvent(keyboardEventSource: nil, virtualKey: key, keyDown: down)?.postToPid(pid)
    usleep(80_000)
}
