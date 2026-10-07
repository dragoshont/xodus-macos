#!/usr/bin/env python3
"""Regenerate the cumulative sprint patch from the stage's backups.

Usage: make-sprint-patch.py <wine source dir> <output patch> [new-file ...]

Every edited file has one or more backups: `<file>.pre-<slice>`,
`<file>.base` (Phase 1), or a partial directory copy `<dir>.pre-<slice>`.
`*.orig` files predate this stage (the 2026-10-06 ALPC lineage) and are not
baselines.
The baseline of a file is its earliest backup by creation time, so the patch
holds every sprint slice applied on top of the stage's original source.
Files of a backed-up directory that are absent from its copy and were created
at or after the copy, and the explicit `new-file` arguments, are diffed
against /dev/null.
"""
import os
import re
import subprocess
import sys

BACKUP = re.compile(r"^(.*)\.(pre-[^/]+|base)$")
ROOTS = ("dlls", "server", "include", "programs")


def btime(path):
    st = os.stat(path)
    return getattr(st, "st_birthtime", st.st_mtime)


def main():
    src, out = sys.argv[1], os.path.abspath(sys.argv[2])
    extra_new = sys.argv[3:]
    os.chdir(src)
    base = {}

    def offer(target, backup, when):
        if target not in base or when < base[target][0]:
            base[target] = (when, backup)

    for root in ROOTS:
        for dirpath, dirnames, filenames in os.walk(root):
            for d in list(dirnames):
                m = BACKUP.match(os.path.join(dirpath, d))
                if not m:
                    continue
                dirnames.remove(d)
                bdir, tdir = m.group(0), m.group(1)
                when = btime(bdir)
                for bp, _, fns in os.walk(bdir):
                    for fn in fns:
                        if BACKUP.match(fn):
                            continue
                        rel = os.path.relpath(os.path.join(bp, fn), bdir)
                        offer(os.path.join(tdir, rel), os.path.join(bp, fn), when)
                for tp, _, fns in os.walk(tdir):
                    for fn in fns:
                        if BACKUP.match(fn):
                            continue
                        t = os.path.join(tp, fn)
                        if (not os.path.exists(os.path.join(bdir, os.path.relpath(t, tdir)))
                                and btime(t) >= when - 2):
                            offer(t, "/dev/null", when)
            for fn in filenames:
                m = BACKUP.match(os.path.join(dirpath, fn))
                if m:
                    offer(m.group(1), m.group(0), btime(m.group(0)))
    for t in extra_new:
        offer(t, "/dev/null", 0)

    chunks = []
    for target in sorted(base):
        old = base[target][1]
        if not os.path.exists(target):
            continue
        old_label = old if old == "/dev/null" else "a/" + target
        r = subprocess.run(["diff", "-u", "--label", old_label, "--label",
                            "b/" + target, old, target], capture_output=True)
        if r.returncode > 1:
            sys.exit(f"diff failed for {target}: {r.stderr.decode()}")
        if r.stdout:
            chunks.append(f"diff -u a/{target} b/{target}\n".encode() + r.stdout)
    with open(out, "wb") as f:
        f.write(b"".join(chunks))
    print(f"{len(chunks)} files -> {out}")
    for c in chunks:
        print("  " + c.split(b"\n", 1)[0].decode().split(" b/")[1])


if __name__ == "__main__":
    main()
