/* Measures kernelbase!OpenGlobalizationUserSettingsKey (private, exported by name):
 * NTSTATUS (ACCESS_MASK access, HANDLE token, HANDLE *key). Prints status, opened key name,
 * subkeys and values, the RedirectedKey of CommonGlobUserSettings and the NULL-out status. */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

typedef LONG (WINAPI *ogusk_fn)(ACCESS_MASK, HANDLE, HANDLE *);
typedef LONG (WINAPI *ntqk_fn)(HANDLE, int, void *, ULONG, ULONG *);

static ntqk_fn ntqk;

static void dump(const char *label, HANDLE key)
{
    BYTE buf[2048];
    ULONG len = 0;
    LONG st = ntqk(key, 3 /* KeyNameInformation */, buf, sizeof(buf) - 2, &len);
    WCHAR name[256], data[512];
    DWORD i, nlen, dlen, type;

    if (!st) { ((WCHAR *)(buf + 4))[*(ULONG *)buf / 2] = 0; printf("%s name=%ls\n", label, (WCHAR *)(buf + 4)); }
    else printf("%s NtQueryKey=0x%lx\n", label, st);
    for (i = 0; i < 40; i++)
    {
        nlen = ARRAYSIZE(name);
        if (RegEnumKeyExW((HKEY)key, i, name, &nlen, NULL, NULL, NULL, NULL)) break;
        printf("  key[%lu] %ls\n", i, name);
    }
    for (i = 0; i < 40; i++)
    {
        nlen = ARRAYSIZE(name); dlen = sizeof(data) - 2;
        memset(data, 0, sizeof(data));
        if (RegEnumValueW((HKEY)key, i, name, &nlen, NULL, &type, (BYTE *)data, &dlen)) break;
        if (type == REG_SZ || type == REG_EXPAND_SZ) printf("  val[%lu] %ls type=%lu %ls\n", i, name, type, data);
        else printf("  val[%lu] %ls type=%lu len=%lu\n", i, name, type, dlen);
    }
}

int main(void)
{
    ogusk_fn ogusk;
    HANDLE key = (HANDLE)0x1234;
    HKEY common;
    WCHAR redirect[512];
    DWORD len = sizeof(redirect), type;
    LONG st;

    setvbuf(stdout, NULL, _IONBF, 0);
    ogusk = (ogusk_fn)GetProcAddress(GetModuleHandleA("kernelbase.dll"), "OpenGlobalizationUserSettingsKey");
    ntqk = (ntqk_fn)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryKey");
    printf("ogusk=%p\n", ogusk);
    if (!ogusk) return 1;

    st = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Control\\CommonGlobUserSettings", 0, KEY_READ, &common);
    printf("CommonGlobUserSettings open=%ld\n", st);
    if (!st)
    {
        st = RegQueryValueExW(common, L"RedirectedKey", NULL, &type, (BYTE *)redirect, &len);
        printf("RedirectedKey=%ld type=%lu %ls\n", st, st ? 0 : type, st ? L"" : redirect);
        RegCloseKey(common);
    }

    st = ogusk(KEY_READ, NULL, &key);
    printf("read: st=0x%lx key=%p\n", st, key);
    if (!st) { dump("read", key); CloseHandle(key); }
    key = (HANDLE)0x1234;
    st = ogusk(KEY_READ | KEY_WRITE, NULL, &key);
    printf("readwrite: st=0x%lx key=%s\n", st, key == (HANDLE)0x1234 ? "untouched" : (key ? "set" : "null"));
    if (!st && key != (HANDLE)0x1234) CloseHandle(key);
    key = (HANDLE)0x1234;
    st = ogusk(KEY_READ, GetCurrentProcessToken(), &key);
    printf("token: st=0x%lx key=%s\n", st, key == (HANDLE)0x1234 ? "untouched" : (key ? "set" : "null"));
    if (!st && key != (HANDLE)0x1234) { dump("token", key); CloseHandle(key); }
    key = (HANDLE)0x1234;
    st = ogusk(0, NULL, &key);
    printf("zeroaccess: st=0x%lx key=%s\n", st, key == (HANDLE)0x1234 ? "untouched" : (key ? "set" : "null"));
    if (!st && key != (HANDLE)0x1234) CloseHandle(key);
    st = ogusk(KEY_READ, NULL, NULL);
    printf("nullout: st=0x%lx\n", st);
    return 0;
}
