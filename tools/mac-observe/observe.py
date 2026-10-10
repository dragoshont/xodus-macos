#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run on the Mac (driven by observe.ps1). Builds/signs Xodus Observe and runs one command.

  python3 observe.py install                      # build + certificate-sign via GUI Terminal
  python3 observe.py capture window <App> <name>  # -> ~/xodus-app-tooling/observe/<name>.png
  python3 observe.py capture display <name>
"""
import subprocess
import sys
import time
from pathlib import Path

HOME = Path.home()
APP = HOME / "Applications/Xodus Observe.app"
WORK = HOME / "xodus-app-tooling/observe"
SOURCE = WORK / "XodusObserve.swift"
CERT = "6C5F1CD832A2B2842686245B4DEF219BB8B465B2"
BUNDLE_ID = "ro.hont.xodus.observe"
PLIST = f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>{BUNDLE_ID}</string>
<key>CFBundleName</key><string>Xodus Observe</string>
<key>CFBundleExecutable</key><string>XodusObserve</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>1.0</string>
<key>CFBundleVersion</key><string>1</string>
<key>LSUIElement</key><true/>
<key>LSMinimumSystemVersion</key><string>14.0</string>
</dict></plist>"""


def wait_for(path, seconds):
    for _ in range(seconds):
        if path.exists():
            return path.read_text().strip()
        time.sleep(1)
    return "pending"


def install():
    # Signing must run in the GUI Terminal session (the keychain is not usable over ssh).
    # Always sign with the same certificate: TCC stores the designated requirement, so an
    # ad-hoc or different signature silently invalidates the Screen Recording grant.
    WORK.mkdir(parents=True, exist_ok=True)
    (APP / "Contents/MacOS").mkdir(parents=True, exist_ok=True)
    (APP / "Contents/Info.plist").write_text(PLIST)
    binary = APP / "Contents/MacOS/XodusObserve"
    subprocess.run(["swiftc", "-O", "-parse-as-library", "-o", str(binary), str(SOURCE)], check=True)
    status = WORK / "sign.status"
    status.unlink(missing_ok=True)
    script = WORK / "sign.sh"
    script.write_text(
        f'codesign --force --sign {CERT} --identifier {BUNDLE_ID} --options runtime "{APP}" '
        f'> "{WORK}/sign.log" 2>&1; echo $? > "{status}"; exit\n')
    subprocess.run(["osascript", "-e", f'tell application "Terminal" to do script "sh {script}"'],
                   capture_output=True)
    print("sign", wait_for(status, 60))
    result = subprocess.run(["codesign", "-dr", "-", str(APP)], capture_output=True, text=True)
    print((result.stdout + result.stderr).strip())


def capture(kind, args):
    name = args[-1]
    out = WORK / f"{name}.png"
    status = Path(str(out) + ".status")
    out.unlink(missing_ok=True)
    status.unlink(missing_ok=True)
    subprocess.run(["open", "-n", str(APP), "--args", "capture", kind] + args[:-1] + [str(out)])
    print(name, wait_for(status, 25), out if out.exists() else "")


if __name__ == "__main__":
    if sys.argv[1:2] == ["install"]:
        install()
    elif sys.argv[1:3] == ["capture", "window"] and len(sys.argv) == 5:
        capture("window", sys.argv[3:])
    elif sys.argv[1:3] == ["capture", "display"] and len(sys.argv) == 4:
        capture("display", sys.argv[3:])
    else:
        sys.exit(__doc__)
