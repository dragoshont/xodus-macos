#!/usr/bin/env python3
"""user32 IsOneCoreTransformMode: measured native (26100.8117, audit-onecore.exe)
returns FALSE for a desktop process without touching the last error.  The mode
is only entered via EnableOneCoreTransformMode, which Wine does not implement,
so the process can never be in OneCore transform mode."""
import os, shutil, sys

root = sys.argv[1] if len(sys.argv) > 1 else "."

def edit(rel, old, new):
    p = os.path.join(root, rel); t = open(p).read()
    if new in t: return
    if old not in t: sys.exit("anchor missing " + rel)
    if not os.path.exists(p + ".pre-onecore"): shutil.copy2(p, p + ".pre-onecore")
    open(p, "w").write(t.replace(old, new, 1)); print("patched", rel)

edit("dlls/user32/user32.spec", "# @ stub IsOneCoreTransformMode", "@ stdcall IsOneCoreTransformMode()")
p = os.path.join(root, "dlls/user32/misc.c"); t = open(p).read()
if "IsOneCoreTransformMode" not in t:
    shutil.copy2(p, p + ".pre-onecore")
    t += ("\n/**********************************************************************\n"
          " *              IsOneCoreTransformMode (USER32.@)\n"
          " *\n"
          " * xodus-onecore: native desktop processes return FALSE without touching the\n"
          " * last error; the mode is only entered via EnableOneCoreTransformMode, which\n"
          " * is not implemented.\n"
          " */\n"
          "BOOL WINAPI IsOneCoreTransformMode(void)\n{\n    return FALSE;\n}\n")
    open(p, "w").write(t); print("patched dlls/user32/misc.c")
