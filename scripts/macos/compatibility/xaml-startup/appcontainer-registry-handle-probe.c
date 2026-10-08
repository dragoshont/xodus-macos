/* Measures profapi.dll ordinal 114 (GetAppContainerRegistryHandleFromName), the
 * private export MrmCoreR delay-loads from ReadRegistryStringFromPackage. */
#include <windows.h>
#include <stdio.h>

typedef HRESULT (WINAPI *fn_t)(const WCHAR *, const WCHAR *, const WCHAR *, REGSAM, HKEY *);

static void probe(fn_t fn, const char *label, const WCHAR *name, const WCHAR *child, const WCHAR *sub, REGSAM sam, BOOL out)
{
    HKEY key = (HKEY)0xdeadbeef;
    HRESULT hr = fn(name, child, sub, sam, out ? &key : NULL);
    WCHAR path[512] = L"";
    DWORD sub_count = 0, val_count = 0;
    if (SUCCEEDED(hr) && out)
    {
        RegQueryInfoKeyW(key, NULL, NULL, NULL, &sub_count, NULL, NULL, &val_count, NULL, NULL, NULL, NULL);
        RegCloseKey(key);
    }
    printf("%s hr=0x%08lx key=%s subkeys=%lu values=%lu\n", label, hr,
           !out ? "n/a" : key == (HKEY)0xdeadbeef ? "untouched" : key ? "set" : "null",
           sub_count, val_count);
    (void)path;
}

int wmain(int argc, WCHAR **argv)
{
    const WCHAR *family = argc > 1 ? argv[1] : L"Microsoft.GamingApp_8wekyb3d8bbwe";
    HMODULE mod = LoadLibraryW(L"profapi.dll");
    fn_t fn = mod ? (fn_t)GetProcAddress(mod, MAKEINTRESOURCEA(114)) : NULL;
    if (!fn) { printf("missing ordinal 114 err=%lu\n", GetLastError()); return 1; }
    probe(fn, "family-read", family, NULL, NULL, KEY_READ, TRUE);
    probe(fn, "family-lower", L"microsoft.gamingapp_8wekyb3d8bbwe", NULL, NULL, KEY_READ, TRUE);
    probe(fn, "family-sub-missing", family, NULL, L"NoSuchSubKey", KEY_READ, TRUE);
    probe(fn, "family-child-missing", family, L"NoSuchChild", NULL, KEY_READ, TRUE);
    probe(fn, "unknown-family", L"No.Such.Package_8wekyb3d8bbwe", NULL, NULL, KEY_READ, TRUE);
    probe(fn, "empty-family", L"", NULL, NULL, KEY_READ, TRUE);
    probe(fn, "null-family", NULL, NULL, NULL, KEY_READ, TRUE);
    probe(fn, "write-sam", family, NULL, NULL, KEY_ALL_ACCESS, TRUE);
    return 0;
}
