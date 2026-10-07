#!/usr/bin/env python3
"""uxtheme GetColorFromPreference, implemented against measured native
Windows 11 26100 behaviour (audit-gcfp.exe); absent from Wine master (upstream
only has 2026-07-15 semi-stubs for the ordinal immersive-colour APIs),
wine-staging 2395d933, Proton 10 and ReactOS.  Requires xodus-gucp."""
import os, shutil, sys

root = sys.argv[1] if len(sys.argv) > 1 else "."

def edit(rel, old, new):
    p = os.path.join(root, rel); t = open(p).read()
    if new in t: return
    if old not in t: sys.exit("anchor missing " + rel)
    if not os.path.exists(p + ".pre-gcfp"): shutil.copy2(p, p + ".pre-gcfp")
    open(p, "w").write(t.replace(old, new, 1)); print("patched", rel)

edit("dlls/uxtheme/uxtheme.spec", "@ stdcall GetCurrentThemeName(ptr long ptr long ptr long)\n",
     "@ stdcall GetColorFromPreference(ptr long long long)\n@ stdcall GetCurrentThemeName(ptr long ptr long ptr long)\n")

p = os.path.join(root, "dlls/uxtheme/system.c"); t = open(p).read()
if "GetUserColorPreference" not in t: sys.exit("xodus-gucp missing")
if "GetColorFromPreference" not in t:
    if not os.path.exists(p + ".pre-gcfp"): shutil.copy2(p, p + ".pre-gcfp")
    t += r'''
/***********************************************************************
 *      GetColorFromPreference                              (UXTHEME.@)
 *
 * xodus-gcfp: measured native behaviour for the preference-independent colour
 * types only.  Types 1-7 are the system accent palette (light3 .. dark3) read
 * once per process from the real user hive Explorer\Accent AccentPalette, which
 * must be a 32-byte REG_BINARY of R,G,B,A entries (alpha ignored); otherwise the
 * built-in default palette is used.  Types 0, 8 and 0xd2 are constant.  The
 * remaining types are derived from the preference by transforms that are not
 * implemented; they return the colour native returns for unknown types.
 */
static const DWORD default_accent_palette[7] =
{
    0xffffeb99, 0xffffc24c, 0xfff89100, 0xffd47800, 0xffc06700, 0xff923e00, 0xff681a00
};

static DWORD accent_palette[7];

static BOOL WINAPI init_accent_palette( INIT_ONCE *once, void *param, void **context )
{
    BYTE data[32];
    DWORD type, size = sizeof(data);
    HKEY user, key;
    BOOL ok = FALSE;
    unsigned int i;

    if (!RtlOpenCurrentUser( KEY_READ, (HANDLE *)&user ))
    {
        if (!RegOpenKeyExW( user, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", 0, KEY_READ, &key ))
        {
            ok = !RegQueryValueExW( key, L"AccentPalette", NULL, &type, data, &size )
                 && type == REG_BINARY && size == sizeof(data);
            RegCloseKey( key );
        }
        RegCloseKey( user );
    }

    for (i = 0; i < ARRAY_SIZE(accent_palette); i++)
        accent_palette[i] = ok ? 0xff000000 | RGB( data[i * 4], data[i * 4 + 1], data[i * 4 + 2] )
                               : default_accent_palette[i];
    return TRUE;
}

DWORD WINAPI GetColorFromPreference( const USER_COLOR_PREFERENCE *pref, UINT type, BOOL ignore_high_contrast,
                                     UINT high_contrast_cache_mode )
{
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;

    TRACE( "%p %#x %d %u\n", pref, type, ignore_high_contrast, high_contrast_cache_mode );

    switch (type)
    {
    case 0:
        return 0xff000000;
    case 1: case 2: case 3: case 4: case 5: case 6: case 7:
        InitOnceExecuteOnce( &once, init_accent_palette, NULL, NULL );
        return accent_palette[type - 1];
    case 8:
    case 0xd2:
        return 0xffffffff;
    default:
        FIXME( "%p %#x %d %u: colour type not implemented\n", pref, type, ignore_high_contrast,
               high_contrast_cache_mode );
        return 0xffff00ff;
    }
}
'''
    open(p, "w").write(t); print("patched dlls/uxtheme/system.c")
