#!/usr/bin/env python3
"""InitThreadCoreMessagingIocp2 / DrainThreadCoreMessagingCompletions2 (user32 ordinals 2669/2670).
Semantics measured on Windows 11 (audit-cmiocp*.c). Usage: patch-cmiocp.py <wine source dir>
Edits the current file, so later slices are preserved; an edit whose replacement
is already present is skipped and every other anchor must match exactly once.
A `.pre-cmiocp` backup is kept the first time a file is changed. Apply after
patch-wob-a.py (see README, "Patch application order")."""
import os, shutil, sys
W = sys.argv[1]
def edit(rel, pairs):
    p = os.path.join(W, rel); b = p + '.pre-cmiocp'
    s = open(p).read(); orig = s
    for old, new in pairs:
        if s.count(new) == 1 and new != old: continue
        if s.count(old) != 1: sys.exit('%s: anchor count %d: %r' % (rel, s.count(old), old[:60]))
        s = s.replace(old, new)
    if s != orig:
        if not os.path.exists(b): shutil.copy2(p, b)
        open(p, 'w').write(s)

edit('dlls/win32u/ntuser_private.h', [(
'''    struct mouse_tracking_info   *mouse_tracking_info;    /* NtUserTrackMouseEvent handling */
};''',
'''    struct mouse_tracking_info   *mouse_tracking_info;    /* NtUserTrackMouseEvent handling */
    HANDLE                        coremsg_iocp;           /* CoreMessaging thread completion port */
    HWND                          coremsg_hwnd[2];        /* windows registered with coremsg_iocp */
};''')])

edit('dlls/win32u/input.c', [(
'''    FIXME("hwnd %p stub!\\n", hwnd);

    if (is_window(hwnd))
        return 2;

    return 0;
}
''',
'''    FIXME("hwnd %p stub!\\n", hwnd);

    if (is_window(hwnd))
        return 2;

    return 0;
}

/* A thread owns one CoreMessaging completion port, shared by at most two of
 * its windows; a slot is released when its window is destroyed. */
static HWND coremsg_slot_window( struct user_thread_info *info, UINT slot )
{
    if (info->coremsg_hwnd[slot] && !is_current_thread_window( info->coremsg_hwnd[slot] ))
        info->coremsg_hwnd[slot] = 0;
    return info->coremsg_hwnd[slot];
}

/**********************************************************************
 *       NtUserInitThreadCoreMessagingIocp2    (win32u.@)
 */
HANDLE WINAPI NtUserInitThreadCoreMessagingIocp2( HWND hwnd, DWORD *slot )
{
    struct user_thread_info *info = get_user_thread_info();
    UINT i, free_slot = ARRAY_SIZE(info->coremsg_hwnd);
    NTSTATUS status;
    DWORD tid;

    TRACE( "hwnd %p slot %p\\n", hwnd, slot );

    if (!(tid = get_window_thread( hwnd, NULL ))) return 0;
    if (tid != GetCurrentThreadId())
    {
        RtlSetLastWin32Error( ERROR_ACCESS_DENIED );
        return 0;
    }
    if (!slot)
    {
        RtlSetLastWin32Error( ERROR_INVALID_PARAMETER );
        return 0;
    }
    hwnd = get_full_window_handle( hwnd );

    for (i = 0; i < ARRAY_SIZE(info->coremsg_hwnd); i++)
    {
        HWND cur = coremsg_slot_window( info, i );
        if (cur == hwnd)
        {
            RtlSetLastWin32Error( ERROR_INVALID_PARAMETER );
            return 0;
        }
        if (!cur && free_slot == ARRAY_SIZE(info->coremsg_hwnd)) free_slot = i;
    }
    if (free_slot == ARRAY_SIZE(info->coremsg_hwnd))
    {
        RtlSetLastWin32Error( ERROR_ALREADY_INITIALIZED );
        return 0;
    }
    if (!info->coremsg_iocp &&
        (status = NtCreateIoCompletion( &info->coremsg_iocp, IO_COMPLETION_ALL_ACCESS, NULL, 0 )))
    {
        info->coremsg_iocp = 0;
        RtlSetLastWin32Error( RtlNtStatusToDosError( status ) );
        return 0;
    }
    info->coremsg_hwnd[free_slot] = hwnd;
    *slot = free_slot;
    return info->coremsg_iocp;
}

/**********************************************************************
 *       NtUserDrainThreadCoreMessagingCompletions2    (win32u.@)
 */
BOOL WINAPI NtUserDrainThreadCoreMessagingCompletions2( HWND hwnd )
{
    static LONG once;
    struct user_thread_info *info = get_user_thread_info();
    UINT i;

    TRACE( "hwnd %p\\n", hwnd );

    if (!get_window_thread( hwnd, NULL )) return FALSE;
    if (!info->coremsg_iocp)
    {
        RtlSetLastWin32Error( ERROR_ACCESS_DENIED );
        return FALSE;
    }
    hwnd = get_full_window_handle( hwnd );
    /* win32u never queues packets to the port, so a registered window has none pending */
    for (i = 0; i < ARRAY_SIZE(info->coremsg_hwnd); i++)
    {
        if (coremsg_slot_window( info, i ) != hwnd) continue;
        if (!once++) FIXME( "hwnd %p: message-arrival packets are not queued to the port\\n", hwnd );
        return TRUE;
    }
    RtlSetLastWin32Error( ERROR_INVALID_PARAMETER );
    return FALSE;
}
''')])

edit('dlls/win32u/win32u.spec', [
 ('@ stub -syscall NtUserDrainThreadCoreMessagingCompletions2\n', '@ stdcall -syscall NtUserDrainThreadCoreMessagingCompletions2(long)\n'),
 ('@ stub -syscall NtUserInitThreadCoreMessagingIocp2\n', '@ stdcall -syscall NtUserInitThreadCoreMessagingIocp2(long ptr)\n')])

edit('dlls/user32/user32.spec', [
 ('2669 stub -noname InitThreadCoreMessagingIocp2  # NtUserInitThreadCoreMessagingIocp2\n',
  '2669 stdcall -noname InitThreadCoreMessagingIocp2(long ptr) NtUserInitThreadCoreMessagingIocp2\n'),
 ('2670 stub -noname DrainThreadCoreMessagingCompletions2  # NtUserDrainThreadCoreMessagingCompletions2\n',
  '2670 stdcall -noname DrainThreadCoreMessagingCompletions2(long) NtUserDrainThreadCoreMessagingCompletions2\n')])

edit('dlls/win32u/main.c', [(
'''INT SYSCALL_API NtUserScheduleDispatchNotification( HWND hwnd )
{
    SYSCALL_FUNC( NtUserScheduleDispatchNotification );
}
''',
'''INT SYSCALL_API NtUserScheduleDispatchNotification( HWND hwnd )
{
    SYSCALL_FUNC( NtUserScheduleDispatchNotification );
}

HANDLE SYSCALL_API NtUserInitThreadCoreMessagingIocp2( HWND hwnd, DWORD *slot )
{
    SYSCALL_FUNC( NtUserInitThreadCoreMessagingIocp2 );
}

BOOL SYSCALL_API NtUserDrainThreadCoreMessagingCompletions2( HWND hwnd )
{
    SYSCALL_FUNC( NtUserDrainThreadCoreMessagingCompletions2 );
}
''')])

edit('include/ntuser.h', [(
'W32KAPI INT     WINAPI NtUserScheduleDispatchNotification( HWND hwnd );\n',
'''W32KAPI INT     WINAPI NtUserScheduleDispatchNotification( HWND hwnd );
W32KAPI HANDLE  WINAPI NtUserInitThreadCoreMessagingIocp2( HWND hwnd, DWORD *slot );
W32KAPI BOOL    WINAPI NtUserDrainThreadCoreMessagingCompletions2( HWND hwnd );
''')])

edit('dlls/wow64win/user.c', [(
'''    return NtUserScheduleDispatchNotification( hwnd );
}
''',
'''    return NtUserScheduleDispatchNotification( hwnd );
}

NTSTATUS WINAPI wow64_NtUserInitThreadCoreMessagingIocp2( UINT *args )
{
    HWND hwnd = get_handle( &args );
    DWORD *slot = get_ptr( &args );

    return HandleToUlong( NtUserInitThreadCoreMessagingIocp2( hwnd, slot ));
}

NTSTATUS WINAPI wow64_NtUserDrainThreadCoreMessagingCompletions2( UINT *args )
{
    HWND hwnd = get_handle( &args );

    return NtUserDrainThreadCoreMessagingCompletions2( hwnd );
}
''')])
print('patched')
