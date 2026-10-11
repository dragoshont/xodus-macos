#include <stdio.h>
#include <wchar.h>
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windows.h"
#include "winternl.h"

static unsigned checks, failures;
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; printf("FAIL line %u: %s\n", __LINE__, #expr); } } while (0)
typedef NTSTATUS (WINAPI *load_string_fn)(void *, ULONG, const WCHAR *, ULONG, const WCHAR **,
                                        USHORT *, WCHAR *, ULONG *);

int main(void)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"), self = GetModuleHandleW(NULL);
    load_string_fn load = (void *)GetProcAddress(ntdll, "RtlLoadString");
    const WCHAR *string = (void *)1, *again = NULL;
    WCHAR path[MAX_PATH];
    USHORT length = 99;
    ULONG language_length = 0;

    CHECK(GetModuleFileNameW(ntdll, path, MAX_PATH) != 0);
    printf("RTL_PROVIDER=%ls\n", path);
    CHECK(load != NULL);
    if (!load) goto done;
    CHECK(load(self, 0, NULL, 0, NULL, &length, NULL, NULL) == STATUS_INVALID_PARAMETER);
    CHECK(load(self, 0, NULL, 1, &string, &length, NULL, NULL) == STATUS_INVALID_PARAMETER && !string && !length);
    CHECK(load(self, 0, L"en-US", 0, &string, &length, NULL, NULL) == STATUS_NOT_SUPPORTED);
    CHECK(load(self, 0, NULL, 0, &string, &length, NULL, &language_length) == STATUS_NOT_SUPPORTED);
    CHECK(load(self, 0, NULL, 0, &string, &length, NULL, NULL) == STATUS_SUCCESS);
    CHECK(length == 10 && !wmemcmp(string, L"block-zero", length));
    CHECK(load(self, 15, NULL, 0, &string, &length, NULL, NULL) == STATUS_SUCCESS);
    CHECK(length == 13 && !wmemcmp(string, L"last-in-block", length));
    CHECK(load(self, 16, NULL, 0, &string, &length, NULL, NULL) == STATUS_SUCCESS);
    CHECK(length == 10 && !wmemcmp(string, L"next-block", length));
    CHECK(load(self, 16, NULL, 0, &again, NULL, NULL, NULL) == STATUS_SUCCESS && again == string);
    CHECK(load(self, 31, NULL, 0, &string, &length, NULL, NULL) == STATUS_SUCCESS);
    CHECK(length == 15 && !wmemcmp(string, L"last-next-block", length));
    CHECK(load(self, 1, NULL, 0, &string, &length, NULL, NULL) == STATUS_SUCCESS && !length);
    CHECK(load(self, 200, NULL, 0, &string, &length, NULL, NULL) == STATUS_RESOURCE_NAME_NOT_FOUND && !string && !length);
    CHECK(load(self, (99 - 1) * 16, NULL, 0, &string, &length, NULL, NULL) == STATUS_RESOURCE_DATA_NOT_FOUND && !string && !length);
done:
    printf("rtl_load_string_checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
