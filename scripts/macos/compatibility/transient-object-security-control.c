#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>
#include <string.h>

typedef LONG (WINAPI *QUERY)(ULONG, const WCHAR *, PSECURITY_DESCRIPTOR *);
typedef void (WINAPI *FREE_DESCRIPTOR)(void *);
static unsigned checks, failures;

static void check(BOOL condition, const char *label)
{
    ++checks;
    if (!condition) { ++failures; printf("FAIL %s error=%lu\n", label, GetLastError()); }
}

static void negative(QUERY query, ULONG kind, const WCHAR *name, ULONG expected, const char *label)
{
    PSECURITY_DESCRIPTOR out = (void *)(ULONG_PTR)0x12345678;
    LONG status = query(kind, name, &out);
    check((ULONG)status == expected && out == (void *)(ULONG_PTR)0x12345678, label);
    printf("QUERY %s status=%08lx output_untouched=%u\n", label, (unsigned long)status,
           out == (void *)(ULONG_PTR)0x12345678);
}

int main(int argc, char **argv)
{
    BOOL native = argc == 2 &&
        (!strcmp(argv[1], "--native") || !strcmp(argv[1], "--nativeReadonly"));
    HMODULE module = LoadLibraryW(L"sechost.dll");
    QUERY query = module ? (QUERY)(ULONG_PTR)GetProcAddress(module, "QueryTransientObjectSecurityDescriptor") : NULL;
    FREE_DESCRIPTOR release = module ? (FREE_DESCRIPTOR)(ULONG_PTR)GetProcAddress(module, "FreeTransientObjectSecurityDescriptor") : NULL;
    const WCHAR *missing = L"XodusAbsentReadOnlyProbe_20261007";
    const WCHAR *root = L"Software\\Microsoft\\SecurityManager\\TransientObjects\\";
    WCHAR path[512], name[128];
    PSECURITY_DESCRIPTOR expected = NULL, actual = NULL;
    HKEY key = NULL;
    DWORD bytes, disposition;
    LONG status;
    if (argc != 1 && !native)
    {
        fprintf(stderr, "Usage: transient-object-security-control [--native|--nativeReadonly]\n");
        return 2;
    }
    if (!query || !release) return 2;
    negative(query, 8, NULL, 0xc000000d, "null-name");
    negative(query, 7, missing, 0xc000000d, "unsupported-kind");
    negative(query, 99, missing, 0xc000000d, "invalid-kind");
    negative(query, 8, missing, 0xc0000034, "absent-object");
    check((ULONG)query(8, missing, NULL) == 0xc000000d, "null-output");
    release(NULL);
    check(TRUE, "free-null-no-fault");
    if (native) goto done;
    if (!GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtXodusQueryRegisteredPackage"))
    {
        fprintf(stderr, "Synthetic registry controls require the isolated Xbox Wine runtime.\n");
        return 3;
    }
    _snwprintf(name, 128, L"XodusTransientFixture_%lu", GetCurrentProcessId());
    _snwprintf(path, 512, L"%ls%%5C%%5C.%%5CAlpcPort%%5C%ls", root, name);
    status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, NULL, 0,
                            KEY_READ | KEY_WRITE | KEY_WOW64_64KEY, NULL, &key, &disposition);
    if (status || disposition != REG_CREATED_NEW_KEY)
    {
        fprintf(stderr, "Fixture key setup refused status=%ld disposition=%lu\n", status, disposition);
        if (key) RegCloseKey(key);
        return 3;
    }
    negative(query, 8, name, 0xc0000034, "absent-value");
    {
        BYTE data[] = {1, 2, 3, 4, 5};
        DWORD size;
        for (size = 1; size <= 4; ++size)
        {
            status = RegSetValueExW(key, L"SecurityDescriptor", 0, REG_BINARY, data, size);
            check(!status, "set-small-binary");
            negative(query, 8, name, 0xc000090b, "native-small-binary-sizing-boundary");
        }
        status = RegSetValueExW(key, L"SecurityDescriptor", 0, REG_BINARY, data, sizeof(data));
        check(!status, "set-opaque-five-byte-binary");
        status = query(8, name, &actual);
        check(!status && actual && !memcmp(actual, data, sizeof(data)), "opaque-binary-copy-no-fabricated-validation");
        if (!status && actual) { release(actual); actual = NULL; }
    }
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;0x1;;;WD)", SDDL_REVISION_1, &expected, &bytes))
    {
        fprintf(stderr, "Fixture descriptor conversion failed error=%lu\n", GetLastError());
        failures++;
        goto cleanup;
    }
    status = RegSetValueExW(key, L"SecurityDescriptor", 0, REG_BINARY, expected, bytes);
    check(!status, "set-synthetic-binary");
    status = query(8, name, &actual);
    check(!status && actual && actual != expected, "own-binary-result");
    if (!status && actual)
    {
        check(IsValidSecurityDescriptor(actual), "valid-binary-result");
        check(GetSecurityDescriptorLength(actual) == bytes && !memcmp(actual, expected, bytes),
              "binary-content-exact");
        release(actual); actual = NULL;
    }
    {
        const WCHAR text[] = L"D:(A;;0x1;;;WD)";
        status = RegSetValueExW(key, L"SecurityDescriptor", 0, REG_SZ, (const BYTE *)text, sizeof(text));
        check(!status, "set-synthetic-string");
        status = query(8, name, &actual);
        check(!status && actual, "own-string-result");
        if (!status && actual)
        {
            check(IsValidSecurityDescriptor(actual), "valid-string-result");
            release(actual); actual = NULL;
        }
    }
    {
        DWORD number = 42;
        status = RegSetValueExW(key, L"SecurityDescriptor", 0, REG_DWORD, (BYTE *)&number, sizeof(number));
        check(!status, "set-unsupported-type");
        negative(query, 8, name, 0xc000090b, "unsupported-value-type");
    }
    {
        const WCHAR text[] = L"{unused-domain}D:(A;;0x1;;;WD)";
        status = RegSetValueExW(key, L"SecurityDescriptor", 0, REG_SZ, (BYTE *)text, sizeof(text));
        check(!status, "set-unqualified-domain-string");
        negative(query, 8, name, 0xc00000bb, "domain-resolver-honestly-unsupported");
    }
cleanup:
    if (actual && actual != (void *)(ULONG_PTR)0x12345678) release(actual);
    if (expected) LocalFree(expected);
    RegCloseKey(key);
    status = RegDeleteKeyExW(HKEY_LOCAL_MACHINE, path, KEY_WOW64_64KEY, 0);
    check(!status, "remove-only-owned-fixture-key");
done:
    FreeLibrary(module);
    printf("TRANSIENT_OBJECT_SECURITY checks=%u failures=%u native_read_only=%u\n",
           checks, failures, native);
    return failures ? 1 : 0;
}
