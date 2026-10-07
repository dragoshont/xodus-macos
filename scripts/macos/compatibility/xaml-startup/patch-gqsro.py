#!/usr/bin/env python3
"""Implement NtUserGetQueueStatusReadonly / user32 #2541 GetQueueStatusReadonly.

Upstream Wine master only has stubs (user32.spec 2541, win32u.spec).  The
semantics are measured on native Windows 11 26100 (audit-gqsro.exe):
NONAME ordinal 2541; returns MAKELONG(changed & flags, wake & flags); never
clears changed bits; never validates flags or sets the last error.
"""
import os
import shutil
import sys

MARK = "xodus-gqsro"
root = sys.argv[1] if len(sys.argv) > 1 else "."


def edit(rel, old, new, count=1):
    path = os.path.join(root, rel)
    text = open(path).read()
    if new in text:
        return
    if text.count(old) < count:
        sys.exit("anchor missing in %s: %r" % (rel, old))
    b = path + ".pre-gqsro"
    if not os.path.exists(b):
        shutil.copy2(path, b)
    open(path, "w").write(text.replace(old, new, count))
    print("patched", rel)


edit("dlls/user32/user32.spec",
     "2541 stub -noname GetQueueStatusReadonly  # NtUserGetQueueStatusReadonly",
     "2541 stdcall -noname GetQueueStatusReadonly(long) NtUserGetQueueStatusReadonly")
edit("dlls/win32u/win32u.spec",
     "@ stub -syscall NtUserGetQueueStatusReadonly",
     "@ stdcall -syscall NtUserGetQueueStatusReadonly(long)")

hdr = os.path.join(root, "dlls/win32u/win32syscalls.h")
text = open(hdr).read()
if "NtUserGetQueueStatusReadonly, 0" in text:
    shutil.copy2(hdr, hdr + ".pre-gqsro")
    first = text.index("SYSCALL_ENTRY( 0x143c, NtUserGetQueueStatusReadonly, 0 )")
    text = text[:first] + text[first:].replace("NtUserGetQueueStatusReadonly, 0 )", "NtUserGetQueueStatusReadonly, 4 )", 1)
    text = text.replace("SYSCALL_ENTRY( 0x143c, NtUserGetQueueStatusReadonly, 0 )",
                        "SYSCALL_ENTRY( 0x143c, NtUserGetQueueStatusReadonly, 8 )", 1)
    stub = "    SYSCALL_STUB( NtUserGetQueueStatusReadonly ) \\\n"
    if stub not in text:
        sys.exit("stub line missing")
    text = text.replace(stub, "", 1)
    open(hdr, "w").write(text)
    print("patched dlls/win32u/win32syscalls.h")

edit("include/ntuser.h",
     "W32KAPI DWORD   WINAPI NtUserGetQueueStatus( UINT flags );\n",
     "W32KAPI DWORD   WINAPI NtUserGetQueueStatus( UINT flags );\n"
     "W32KAPI DWORD   WINAPI NtUserGetQueueStatusReadonly( UINT flags );\n")

edit("dlls/win32u/main.c",
     "DWORD SYSCALL_API NtUserGetQueueStatus( UINT flags )\n{\n    SYSCALL_FUNC( NtUserGetQueueStatus );\n}\n",
     "DWORD SYSCALL_API NtUserGetQueueStatus( UINT flags )\n{\n    SYSCALL_FUNC( NtUserGetQueueStatus );\n}\n\n"
     "DWORD SYSCALL_API NtUserGetQueueStatusReadonly( UINT flags )\n{\n    SYSCALL_FUNC( NtUserGetQueueStatusReadonly );\n}\n")

edit("dlls/wow64win/user.c",
     "NTSTATUS WINAPI wow64_NtUserGetQueueStatus( UINT *args )\n{\n    UINT flags = get_ulong( &args );\n\n    return NtUserGetQueueStatus( flags );\n}\n",
     "NTSTATUS WINAPI wow64_NtUserGetQueueStatus( UINT *args )\n{\n    UINT flags = get_ulong( &args );\n\n    return NtUserGetQueueStatus( flags );\n}\n\n"
     "NTSTATUS WINAPI wow64_NtUserGetQueueStatusReadonly( UINT *args )\n{\n    UINT flags = get_ulong( &args );\n\n    return NtUserGetQueueStatusReadonly( flags );\n}\n")

edit("dlls/win32u/input.c",
     "/*******************************************************************\n *           NtUserGetThreadInfo (win32u.@)\n */",
     "/***********************************************************************\n"
     " *           NtUserGetQueueStatusReadonly (win32u.@)\n"
     " *\n"
     " * xodus-gqsro: like NtUserGetQueueStatus, but native neither validates the\n"
     " * flags nor clears the changed bits.\n"
     " */\n"
     "DWORD WINAPI NtUserGetQueueStatusReadonly( UINT flags )\n"
     "{\n"
     "    UINT ret, wake_bits, changed_bits;\n"
     "\n"
     "    check_for_events( flags );\n"
     "\n"
     "    if (get_shared_queue_bits( &wake_bits, &changed_bits ))\n"
     "        ret = MAKELONG( changed_bits & flags, wake_bits & flags );\n"
     "    else SERVER_START_REQ( get_queue_status )\n"
     "    {\n"
     "        req->clear_bits = 0;\n"
     "        wine_server_call( req );\n"
     "        ret = MAKELONG( reply->changed_bits & flags, reply->wake_bits & flags );\n"
     "    }\n"
     "    SERVER_END_REQ;\n"
     "    return ret;\n"
     "}\n\n"
     "/*******************************************************************\n *           NtUserGetThreadInfo (win32u.@)\n */")
