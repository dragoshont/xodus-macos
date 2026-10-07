#include <windows.h>
#include <stdio.h>

typedef struct { COLORREF start, accent; } PREF;
typedef HRESULT (WINAPI *fn_get)(PREF *, BOOL);
static fn_get pg;

static void call(const char *tag, BOOL force)
{
    PREF p = { 0xdeadbeef, 0xdeadbeef };
    HRESULT hr;
    SetLastError(0xdead);
    hr = pg(&p, force);
    printf("%s force=%d hr=%08lx start=%08lx accent=%08lx gle=%lu\n", tag, force, hr, p.start, p.accent, GetLastError());
}

static void setv(HKEY root, const char *name, DWORD type, DWORD v)
{
    HKEY k;
    RegCreateKeyExA(root, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
    if (type == REG_NONE) RegDeleteValueA(k, name);
    else if (type == REG_BINARY + 100) RegSetValueExA(k, name, 0, REG_BINARY, (BYTE *)&v, 4);
    else RegSetValueExA(k, name, 0, type, (BYTE *)&v, type == REG_BINARY ? 2 : 4);
    RegCloseKey(k);
}

int main(int argc, char **argv)
{
    HMODULE h;
    HKEY tmp, k;
    DWORD v, sz, t;
    int mode = argc > 1 ? atoi(argv[1]) : 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    h = LoadLibraryA("uxtheme.dll");
    pg = (fn_get)GetProcAddress(h, "GetUserColorPreference");
    printf("byname=%d apiset=%d\n", pg != NULL,
           GetProcAddress(LoadLibraryA("ext-ms-win-uxtheme-themes-l1-1-0.dll"), "GetUserColorPreference") != NULL);
    if (!pg) return 1;
    if (mode == 3)
    {
        RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\XodusAuditGucp", 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &tmp, NULL);
        setv(tmp, "StartColorMenu", REG_DWORD, 0x11223344);
        setv(tmp, "AccentColorMenu", REG_DWORD, 0x55667788);
        RegOverridePredefKey(HKEY_CURRENT_USER, tmp);
        call("override-first", FALSE);
        call("override-first", TRUE);
        RegOverridePredefKey(HKEY_CURRENT_USER, NULL);
        RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\XodusAuditGucp");
        RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\XodusAuditGucp");
        return 0;
    }
    if (mode == 4)
    {
        call("pre", FALSE);
        setv(HKEY_CURRENT_USER, "AccentColorMenu", REG_DWORD, 0xff112233);
        call("changed", FALSE);
        call("changed", TRUE);
        Sleep(1500);
        call("changed-later", FALSE);
        setv(HKEY_CURRENT_USER, "AccentColorMenu", REG_NONE, 0);
        call("deleted", TRUE);
        setv(HKEY_CURRENT_USER, "AccentColorMenu", REG_DWORD, 0xffd77800);
        call("restored", TRUE);
        return 0;
    }
    if (mode == 5)
    {
        call("pre", TRUE);
        setv(HKEY_CURRENT_USER, "AccentColorMenu", REG_NONE, 0);
        setv(HKEY_CURRENT_USER, "StartColorMenu", REG_NONE, 0);
        call("both-missing", TRUE);
        RegDeleteKeyValueA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", "AccentPalette");
        call("palette-missing", TRUE);
        RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent");
        call("key-missing", TRUE);
        return 0;
    }
    if (mode == 6)
    {
        setv(HKEY_CURRENT_USER, "StartColorMenu", REG_NONE, 0);
        call("start-missing", TRUE);
        return 0;
    }
    if (mode == 7)
    {
        call("pre", TRUE);
        setv(HKEY_CURRENT_USER, "StartColorMenu", REG_SZ, 0x00333231);
        call("start-sz", TRUE);
        setv(HKEY_CURRENT_USER, "StartColorMenu", REG_BINARY, 0x1234);
        call("start-bin2", TRUE);
        setv(HKEY_CURRENT_USER, "StartColorMenu", REG_BINARY + 100, 0x1234);
        call("start-bin4", TRUE);
        return 0;
    }
    if (mode == 8)
    {
        call("first", TRUE);
        setv(HKEY_CURRENT_USER, "AccentColorMenu", REG_DWORD, 0xff112233);
        call("after-change", FALSE);
        call("after-change", TRUE);
        call("after-change-again", FALSE);
        return 0;
    }
    if (mode == 1) { printf("null hr=%08lx\n", pg(NULL, FALSE)); return 0; }
    if (mode == 2) { printf("null-force hr=%08lx\n", pg(NULL, TRUE)); return 0; }

    if (!RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", 0, KEY_READ, &k))
    {
        sz = 4; v = 0; t = 0;
        printf("real StartColorMenu rc=%ld", RegQueryValueExA(k, "StartColorMenu", NULL, &t, (BYTE *)&v, &sz));
        printf(" t=%lu v=%08lx", t, v);
        sz = 4; v = 0; t = 0;
        printf(" AccentColorMenu rc=%ld", RegQueryValueExA(k, "AccentColorMenu", NULL, &t, (BYTE *)&v, &sz));
        printf(" t=%lu v=%08lx\n", t, v);
        RegCloseKey(k);
    }
    else printf("real accent key missing\n");
    call("real", FALSE);
    call("real", TRUE);

    RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\XodusAuditGucp", 0, NULL, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, NULL, &tmp, NULL);
    RegOverridePredefKey(HKEY_CURRENT_USER, tmp);
    call("empty-cached", FALSE);
    call("empty", TRUE);
    call("empty-again", FALSE);
    setv(tmp, "StartColorMenu", REG_DWORD, 0x11223344);
    call("startonly-cached", FALSE);
    call("startonly", TRUE);
    setv(tmp, "AccentColorMenu", REG_DWORD, 0x55667788);
    call("both", TRUE);
    setv(tmp, "AccentColorMenu", REG_DWORD, 0xff667788);
    call("both-changed-nocache", FALSE);
    setv(tmp, "StartColorMenu", REG_NONE, 0);
    call("accentonly", TRUE);
    setv(tmp, "StartColorMenu", REG_SZ, 0x00333231);
    call("start-sz", TRUE);
    setv(tmp, "StartColorMenu", REG_BINARY, 0x1234);
    call("start-bin2", TRUE);
    RegOverridePredefKey(HKEY_CURRENT_USER, NULL);
    call("restored", TRUE);
    RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\XodusAuditGucp");
    RegDeleteKeyA(HKEY_CURRENT_USER, "Software\\XodusAuditGucp");
    return 0;
}
