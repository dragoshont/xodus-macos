// SPDX-License-Identifier: GPL-3.0-only
import AppKit
import Foundation

guard let application = NSWorkspace.shared.frontmostApplication,
      application.processIdentifier > 0 else {
    FileHandle.standardError.write(Data("No current graphical foreground process is available.\n".utf8))
    exit(1)
}
print(application.processIdentifier)
