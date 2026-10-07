#!/usr/bin/env python3
"""uxtheme GetUserColorPreference, implemented against measured native
Windows 11 26100 behaviour (audit-gucp.exe); absent from Wine master, wine-staging
2395d933, Proton 10 and ReactOS."""
import os, shutil, sys

root = sys.argv[1] if len(sys.argv) > 1 else "."

def edit(rel, old, new):
    p = os.path.join(root, rel); t = open(p).read()
    if new in t: return
    if old not in t: sys.exit("anchor missing " + rel)
    if not os.path.exists(p + ".pre-gucp"): shutil.copy2(p, p + ".pre-gucp")
    open(p, "w").write(t.replace(old, new, 1)); print("patched", rel)

edit("dlls/uxtheme/uxtheme.spec", "@ stdcall GetWindowTheme(ptr)\n",
     "@ stdcall GetUserColorPreference(ptr long)\n@ stdcall GetWindowTheme(ptr)\n")
edit("dlls/uxtheme/system.c", '#include "winreg.h"\n', '#include "winreg.h"\n#include "winternl.h"\n')

p = os.path.join(root, "dlls/uxtheme/system.c"); t = open(p).read()
if "GetUserColorPreference" not in t:
    t += r'''
/***********************************************************************
 *      GetUserColorPreference                              (UXTHEME.@)
 *
 * xodus-gucp: measured native behaviour.  The colours come from the real user
 * hive (HKCU overrides are ignored) Explorer\Accent StartColorMenu and
 * AccentColorMenu REG_DWORD values; if either is missing or not REG_DWORD both
 * fall back to the built-in defaults.  The first call fills a process cache that
 * non-forced calls return; forced calls re-read without updating the cache.
 */
typedef struct
{
    COLORREF start;
    COLORREF accent;
} USER_COLOR_PREFERENCE;

static void read_user_color_preference( USER_COLOR_PREFERENCE *pref )
{
    DWORD start, accent, type, size;
    HKEY user, key;
    BOOL ok = FALSE;

    if (!RtlOpenCurrentUser( KEY_READ, (HANDLE *)&user ))
    {
        if (!RegOpenKeyExW( user, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", 0, KEY_READ, &key ))
        {
            size = sizeof(start);
            if (!RegQueryValueExW( key, L"StartColorMenu", NULL, &type, (BYTE *)&start, &size ) && type == REG_DWORD)
            {
                size = sizeof(accent);
                ok = !RegQueryValueExW( key, L"AccentColorMenu", NULL, &type, (BYTE *)&accent, &size ) && type == REG_DWORD;
            }
            RegCloseKey( key );
        }
        RegCloseKey( user );
    }

    if (ok)
    {
        pref->start = start;
        pref->accent = accent;
    }
    else
    {
        pref->start = 0xff9e5a00;
        pref->accent = 0xffd77800;
    }
}

static USER_COLOR_PREFERENCE cached_user_color_preference;

static BOOL WINAPI init_user_color_preference( INIT_ONCE *once, void *param, void **context )
{
    read_user_color_preference( &cached_user_color_preference );
    return TRUE;
}

HRESULT WINAPI GetUserColorPreference( USER_COLOR_PREFERENCE *pref, BOOL force_reload )
{
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;

    TRACE( "%p %d\n", pref, force_reload );

    InitOnceExecuteOnce( &once, init_user_color_preference, NULL, NULL );
    if (force_reload) read_user_color_preference( pref );
    else *pref = cached_user_color_preference;
    return S_OK;
}
'''
    open(p, "w").write(t); print("patched dlls/uxtheme/system.c")
