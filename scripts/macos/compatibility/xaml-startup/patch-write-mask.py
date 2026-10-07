"""Write-restricted token mask slice. Usage: patch-write-mask.py <wine source dir>

Windows checks restricting SIDs of a write-restricted token only for rights that
are write-only in the generic mapping; bits also present in the read or execute
mapping are not restricted (probe-outputs/native-write-overlap.txt vs
wine-pre-write-overlap.txt). Idempotent; a `.pre-overlap` backup is kept on
first change. Apply after the Phase 1 service-principal slice."""
import os, shutil, sys
S = sys.argv[1]

def patch(rel, pairs, tag):
    p = os.path.join(S, rel)
    src = open(p).read()
    orig = src
    for old, new in pairs:
        if src.count(new) == 1 and new != old:
            continue
        if src.count(old) != 1:
            sys.exit("anchor count %d in %s: %r" % (src.count(old), rel, old[:70]))
        src = src.replace(old, new)
    if src != orig:
        if not os.path.exists(p + tag):
            shutil.copy2(p, p + tag)
        open(p, "w").write(src)
    print("patched" if src != orig else "already applied", rel)
patch("server/token.c", [
("""    unsigned int write_access = mapping->write | DELETE | WRITE_DAC | WRITE_OWNER;
""",
"""    /* write-restricted tokens check restricting SIDs only for rights that are write-only in the
     * mapping; bits also in the read or execute mapping are not restricted (measured on Windows) */
    unsigned int write_access = (mapping->write & ~(mapping->read | mapping->exec)) | DELETE | WRITE_DAC | WRITE_OWNER;
"""),
], ".pre-overlap")