// Print the CGWindowID of the first normal on-screen window owned by a pid (for `screencapture -l`).
// Usage: swift window-id.swift <pid>
import CoreGraphics

guard CommandLine.arguments.count == 2, let pid = Int32(CommandLine.arguments[1]) else {
    print("usage: window-id.swift <pid>")
    exit(2)
}
let windows = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as? [[String: Any]] ?? []
for w in windows where (w[kCGWindowOwnerPID as String] as? Int32) == pid && (w[kCGWindowLayer as String] as? Int) == 0 {
    print(w[kCGWindowNumber as String] as! Int)
    exit(0)
}
exit(1)
