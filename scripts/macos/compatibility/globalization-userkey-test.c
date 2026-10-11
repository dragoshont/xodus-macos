#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef LONG (WINAPI *OPEN_KEY)(ACCESS_MASK, void *, HANDLE *);
typedef LONG (WINAPI *OPEN_CURRENT)(ACCESS_MASK, HANDLE *);
typedef LONG (WINAPI *QUERY_KEY)(HANDLE, ULONG, void *, ULONG, ULONG *);

struct key_name
{
    ULONG bytes;
    WCHAR name[512];
};

static unsigned checks, failures;

static void check(int valid, const char *name)
{
    checks++;
    if (!valid)
    {
        failures++;
        printf("FAIL %s\n", name);
    }
}

int main(void)
{
    HMODULE kernelbase = GetModuleHandleW(L"kernelbase.dll"), ntdll = GetModuleHandleW(L"ntdll.dll");
    OPEN_KEY open = (OPEN_KEY)(ULONG_PTR)GetProcAddress(kernelbase, "OpenGlobalizationUserSettingsKey");
    OPEN_CURRENT current = (OPEN_CURRENT)(ULONG_PTR)GetProcAddress(ntdll, "RtlOpenCurrentUser");
    QUERY_KEY query = (QUERY_KEY)(ULONG_PTR)GetProcAddress(ntdll, "NtQueryKey");
    const ACCESS_MASK access[] = {KEY_READ, KEY_QUERY_VALUE, KEY_READ | KEY_WRITE, KEY_ALL_ACCESS};
    unsigned i;

    check(open != NULL, "globalization key export");
    check(current != NULL, "current-user key provider");
    check(query != NULL, "registry key-name provider");
    if (!open || !current || !query) goto done;
    check(open(KEY_READ, NULL, NULL) == (LONG)0xc000000d, "null output NTSTATUS");
    for (i = 0; i < sizeof(access) / sizeof(access[0]); i++)
    {
        HANDLE actual = NULL, expected = NULL;
        struct key_name actual_name = {0}, expected_name = {0};
        ULONG actual_bytes = 0, expected_bytes = 0;
        LONG actual_status = open(access[i], NULL, &actual);
        LONG expected_status = current(access[i], &expected);

        check(!actual_status && actual_status == expected_status && actual && expected,
              "open returns real current-user handles");
        check(actual && expected &&
              !query(actual, 3, &actual_name, sizeof(actual_name), &actual_bytes) &&
              !query(expected, 3, &expected_name, sizeof(expected_name), &expected_bytes) &&
              actual_name.bytes && actual_name.bytes == expected_name.bytes &&
              actual_name.bytes <= sizeof(actual_name.name) &&
              !memcmp(actual_name.name, expected_name.name, actual_name.bytes),
              "opened key name exactly matches current-user root");
        {
            LONG actual_close = actual ? RegCloseKey((HKEY)actual) : ERROR_INVALID_HANDLE;
            LONG expected_close = expected ? RegCloseKey((HKEY)expected) : ERROR_INVALID_HANDLE;
            check(!actual_close && !expected_close, "ordinary registry handles close normally");
        }
    }
done:
    printf("RESULT globalization_userkey_checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
