#!/usr/bin/env python3
"""Add native combase process-events/message-dispatcher NONAME exports.

Reversed from native combase.dll 10.0.26100.9278 (x64):
  #86  CoBeginProcessEvents                  0x155f0
  #87  CoMsgWaitInProcessEvents               0x2dde0
  #88  CoEndProcessEvents                     0x156b0
  #110 CoSetMessageDispatcher                 0x13ff20
  #111 CoHandlePriorityEventsFromMessagePump  0x109f00
  #133 RoIsASTAWorkScheduledAtPriorityOrHigher 0x1225a0

Every native entry point branches on tagSOleTlsData::IsModernSTAThread
(OLETLS flags 0x80 with 0x400000 or 0x40000000).  Wine never creates an
application STA, so only the native non-ASTA paths are reachable here and
they are reproduced exactly.  No ASTA behaviour is claimed.
"""
import os
import shutil
import sys

MARK = "xodus-process-events"
root = sys.argv[1] if len(sys.argv) > 1 else "."

CODE = r'''
/* xodus-process-events: native combase process-events family.  Native
 * implements real work only for application STA (ASTA) threads; Wine
 * apartments are never ASTA, so these reproduce the native non-ASTA paths. */
static BOOL is_modern_sta_thread(void)
{
    return FALSE;
}

void WINAPI CoSetMessageDispatcher(void *dispatcher)
{
    TRACE("%p, modern STA %d.\n", dispatcher, is_modern_sta_thread());
}

void WINAPI CoBeginProcessEvents(void *context)
{
    TRACE("%p.\n", context);
    memset(context, 0, 0x60);
}

void WINAPI CoEndProcessEvents(void *context)
{
    TRACE("%p.\n", context);
}

DWORD WINAPI CoMsgWaitInProcessEvents(void *context, DWORD count, const HANDLE *handles,
        DWORD timeout, DWORD wake_mask, DWORD flags)
{
    TRACE("%p, %lu, %p, %lu, %#lx, %#lx.\n", context, count, handles, timeout, wake_mask, flags);

    if (count > 0x38 || (flags & MWMO_WAITALL))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return WAIT_FAILED;
    }
    return MsgWaitForMultipleObjectsEx(count, handles, timeout, wake_mask, flags);
}

void WINAPI CoHandlePriorityEventsFromMessagePump(void)
{
    TRACE(".\n");
}

BOOL WINAPI RoIsASTAWorkScheduledAtPriorityOrHigher(int priority)
{
    TRACE("%d.\n", priority);
    return FALSE;
}
'''

SPEC = {
    86: "86 stdcall -noname CoBeginProcessEvents(ptr)",
    87: "87 stdcall -noname CoMsgWaitInProcessEvents(ptr long ptr long long long)",
    88: "88 stdcall -noname CoEndProcessEvents(ptr)",
    110: "110 stdcall -noname CoSetMessageDispatcher(ptr)",
    111: "111 stdcall -noname CoHandlePriorityEventsFromMessagePump()",
    133: "133 stdcall -noname RoIsASTAWorkScheduledAtPriorityOrHigher(long)",
}


def backup(path):
    b = path + ".pre-process-events"
    if not os.path.exists(b):
        shutil.copy2(path, b)


src = os.path.join(root, "dlls/combase/roapi.c")
text = open(src).read()
if MARK not in text:
    backup(src)
    if "winuser.h" not in text:
        text = text.replace('#include "objbase.h"', '#include "objbase.h"\n#include "winuser.h"', 1)
        if "winuser.h" not in text:
            sys.exit("no include anchor in roapi.c")
    text = text.rstrip("\n") + "\n" + CODE
    open(src, "w").write(text)
    print("patched", src)

spec = os.path.join(root, "dlls/combase/combase.spec")
lines = open(spec).read().split("\n")
if not any("CoSetMessageDispatcher" in l for l in lines):
    backup(spec)
    out, pending = [], dict(SPEC)
    for l in lines:
        tok = l.split(" ", 1)[0]
        if tok.isdigit():
            n = int(tok)
            for o in sorted(k for k in pending if k < n):
                out.append(pending.pop(o))
            if n in SPEC:
                sys.exit("ordinal %d already used" % n)
        out.append(l)
    if pending:
        sys.exit("unplaced ordinals %s" % sorted(pending))
    open(spec, "w").write("\n".join(out))
    print("patched", spec)
