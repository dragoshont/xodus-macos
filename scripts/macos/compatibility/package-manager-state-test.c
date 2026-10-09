#include "package-manager-state.h"
#include <stdio.h>
#include <wchar.h>
#include <string.h>

static int failures;
#define CHECK(test) do { if (!(test)) { printf("FAIL line=%d %s\n", __LINE__, #test); ++failures; } } while (0)

int main(void)
{
    const WCHAR actual_default[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\InstallService\\State";
    const WCHAR unicode_default[] = L"Software\\Xodus\\\x03a9";
    WCHAR buffer[256];
    DWORD needed = sizeof(actual_default), actual;
    PM_STATE_FACTS facts = pm_state_query_context();
    printf("ACTUAL_STATE_CONTEXT=%d SHARED_FLAGS=0x%08lx SILO_SCOPE=%u STATE_ENABLED=%u\n",
           facts.context, facts.shared_flags, facts.silo_scope, facts.state_enabled);
    CHECK(facts.context == PM_STATE_DISABLED);
    for (DWORD capacity = 0; capacity <= needed + 2; ++capacity) {
        for (SIZE_T i = 0; i < ARRAYSIZE(buffer); ++i) buffer[i] = 0x5a5a;
        actual = 0xffffffff;
        DWORD status = pm_state_resolve(PM_STATE_DISABLED, L"InstallServiceMutableState",
                                       actual_default, buffer, capacity, &actual);
        CHECK(actual == needed);
        if (capacity < needed) {
            CHECK(status == ERROR_MORE_DATA && buffer[0] == 0x5a5a);
        } else {
            CHECK(status == ERROR_SUCCESS && !wcscmp(buffer, actual_default));
            CHECK(buffer[needed / sizeof(WCHAR)] == 0x5a5a);
        }
    }
    actual = 0xffffffff;
    CHECK(pm_state_resolve(PM_STATE_DISABLED, NULL, actual_default, NULL, 0, &actual) == ERROR_MORE_DATA);
    CHECK(actual == needed);
    CHECK(pm_state_resolve(PM_STATE_DISABLED, NULL, actual_default, buffer, sizeof(buffer), NULL) == ERROR_SUCCESS);
    CHECK(!wcscmp(buffer, actual_default));
    actual = 0xffffffff; buffer[0] = 0x5a5a;
    CHECK(pm_state_resolve(PM_STATE_DISABLED, NULL, NULL, buffer, sizeof(buffer), &actual) == ERROR_FILE_NOT_FOUND);
    CHECK(actual == 0xffffffff && buffer[0] == 0x5a5a);
    CHECK(pm_state_resolve(PM_STATE_DISABLED, L"", L"", buffer, 2, &actual) == ERROR_SUCCESS);
    CHECK(actual == 2 && buffer[0] == 0);
    CHECK(pm_state_resolve(PM_STATE_DISABLED, L"unmapped", unicode_default, buffer,
                           sizeof(buffer), &actual) == ERROR_SUCCESS);
    CHECK(actual == sizeof(unicode_default) && !wcscmp(buffer, unicode_default));
    actual = 0xffffffff;
    CHECK(pm_state_resolve(PM_STATE_DISABLED, L"unmapped", actual_default, NULL,
                           sizeof(buffer), &actual) == ERROR_INVALID_PARAMETER);
    CHECK(actual == 0xffffffff);
    for (int mode = PM_STATE_UNAVAILABLE; mode <= PM_STATE_ENABLED; ++mode) {
        if (mode == PM_STATE_DISABLED) continue;
        actual = 0xffffffff; buffer[0] = 0x5a5a;
        CHECK(pm_state_resolve((PM_STATE_CONTEXT)mode, L"InstallServiceMutableState", actual_default,
                               buffer, sizeof(buffer), &actual) == ERROR_NOT_SUPPORTED);
        CHECK(actual == 0xffffffff && buffer[0] == 0x5a5a);
    }
    SetLastError(0x13579bdf);
    CHECK(GetPersistedRegistryLocationW(L"InstallServiceMutableState", actual_default,
                                       buffer, sizeof(buffer), &actual) == ERROR_SUCCESS);
    CHECK(GetLastError() == 0x13579bdf && actual == needed && !wcscmp(buffer, actual_default));
    buffer[0] = 0x5a5a; actual = 0xffffffff;
    SetLastError(0x13579bdf);
    CHECK(GetPersistedRegistryLocationW(NULL, actual_default, buffer, needed - 1, &actual) == ERROR_MORE_DATA);
    CHECK(GetLastError() == 0x13579bdf && actual == needed && buffer[0] == 0x5a5a);
    actual = 0xffffffff;
    SetLastError(0x13579bdf);
    CHECK(GetPersistedRegistryLocationW(NULL, NULL, buffer, sizeof(buffer), &actual) == ERROR_FILE_NOT_FOUND);
    CHECK(GetLastError() == 0x13579bdf && actual == 0xffffffff && buffer[0] == 0x5a5a);
    printf("PACKAGE_MANAGER_STATE_TEST_%s failures=%d\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
