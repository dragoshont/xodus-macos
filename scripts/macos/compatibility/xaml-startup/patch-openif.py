"""ALPC OBJ_OPENIF slice. Usage: patch-openif.py <wine source dir>

NtAlpcCreatePort on an existing name returns STATUS_INVALID_PARAMETER on Windows
even with OBJ_OPENIF (probe-outputs/native-tokensd-openif.txt); stock Wine returned
NULL without an error. Idempotent; a `.pre-openif` backup is kept on first change.
Apply after the Phase 1 service-principal slice and before patch-sendattr.py
(see README, "Patch application order")."""
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
patch("server/alpc.c", [
("""        else
        {
            release_object( port );
            return NULL;
        }
    }

    return port;""",
"""        else
        {
            /* existing ports are never opened through create; Windows rejects OBJ_OPENIF */
            set_error( STATUS_INVALID_PARAMETER );
            release_object( port );
            return NULL;
        }
    }

    return port;"""),
], ".pre-openif")