// SPDX-License-Identifier: GPL-3.0-only
// Xodus Observe: the single remote-Mac observability tool. Extend with new subcommands; never add another helper app.
// Launch through LaunchServices so TCC attributes the request to this app, not sshd:
//   open -n -W "$HOME/Applications/Xodus Observe.app" --args capture window <AppName> <out.png>
//   open -n -W "$HOME/Applications/Xodus Observe.app" --args capture display <out.png>
// Each run writes <out>.status containing "ok ..." or "error ...".
import AppKit
import ScreenCaptureKit

@main
struct XodusObserve {
    @MainActor
    static func main() async {
        // SCContentFilter asserts inside SkyLight without a WindowServer connection.
        _ = NSApplication.shared
        let args = Array(CommandLine.arguments.dropFirst())
        guard let out = args.last, args.count >= 3 else {
            FileHandle.standardError.write(Data("usage: capture window <App> <out.png> | capture display <out.png>\n".utf8))
            exit(64)
        }
        let status = URL(fileURLWithPath: out + ".status")
        DispatchQueue.global().asyncAfter(deadline: .now() + 20) {
            try? "error timeout".write(to: status, atomically: true, encoding: .utf8)
            exit(1)
        }
        do {
            switch (args[0], args[1]) {
            case ("capture", "window") where args.count == 4:
                try await capture(window: args[2], to: out, status: status)
            case ("capture", "display"):
                try await capture(window: nil, to: out, status: status)
            default:
                throw failure(64, "unknown command \(args.joined(separator: " "))")
            }
        } catch {
            try? "error \(error.localizedDescription)".write(to: status, atomically: true, encoding: .utf8)
            exit(1)
        }
        exit(0)
    }

    @MainActor
    static func capture(window name: String?, to out: String, status: URL) async throws {
        guard CGPreflightScreenCaptureAccess() else {
            CGRequestScreenCaptureAccess()
            throw failure(2, "screen recording not granted to Xodus Observe")
        }
        let content = try await SCShareableContent.excludingDesktopWindows(true, onScreenWindowsOnly: true)
        let filter: SCContentFilter
        let size: CGSize
        if let name {
            let windows = content.windows.filter {
                $0.owningApplication?.applicationName == name && $0.windowLayer == 0 && $0.frame.width > 300
            }
            guard let window = windows.max(by: { $0.frame.width * $0.frame.height < $1.frame.width * $1.frame.height }) else {
                throw failure(4, "no on-screen window for \(name)")
            }
            filter = SCContentFilter(desktopIndependentWindow: window)
            size = window.frame.size
        } else {
            guard let display = content.displays.first else { throw failure(3, "no display") }
            filter = SCContentFilter(display: display, excludingWindows: [])
            size = CGSize(width: display.width, height: display.height)
        }
        let config = SCStreamConfiguration()
        let scale = NSScreen.main?.backingScaleFactor ?? 2
        config.width = Int(size.width * scale)
        config.height = Int(size.height * scale)
        config.showsCursor = false
        let image = try await SCScreenshotManager.captureImage(contentFilter: filter, configuration: config)
        guard let png = NSBitmapImageRep(cgImage: image).representation(using: .png, properties: [:]) else {
            throw failure(5, "png encoding failed")
        }
        try png.write(to: URL(fileURLWithPath: out))
        try "ok \(config.width)x\(config.height)".write(to: status, atomically: true, encoding: .utf8)
    }

    static func failure(_ code: Int, _ message: String) -> NSError {
        NSError(domain: "XodusObserve", code: code, userInfo: [NSLocalizedDescriptionKey: message])
    }
}
