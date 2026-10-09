#include "package-manager-state.h"
#include <winternl.h>
#include <string.h>
#include <wchar.h>
#include <stdint.h>
#include <stdio.h>

#ifndef _WIN64
#error This provider is scoped to the measured x64 state-selector ABI.
#endif

typedef NTSTATUS (NTAPI *PM_QUERY_PROCESS)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);

static int readable(const void *address, SIZE_T bytes)
{
    MEMORY_BASIC_INFORMATION info;
    ULONG_PTR at = (ULONG_PTR)address, base;
    if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info)) return 0;
    if (info.State != MEM_COMMIT || info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return 0;
    if (!(info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                         PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
        return 0;
    base = (ULONG_PTR)info.BaseAddress;
    return at >= base && at - base <= info.RegionSize &&
        bytes <= info.RegionSize - (at - base);
}

PM_STATE_FACTS pm_state_query_context(void)
{
    PM_STATE_FACTS result = {PM_STATE_UNAVAILABLE, 0, 0, 0};
    PROCESS_BASIC_INFORMATION process;
    PM_QUERY_PROCESS query;
    BYTE *shared;
    DWORD first;
    NTSTATUS status;
    query = (PM_QUERY_PROCESS)(ULONG_PTR)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                                        "NtQueryInformationProcess");
    if (!query) return result;
    status = query(GetCurrentProcess(), ProcessBasicInformation, &process, sizeof(process), NULL);
    if (status < 0 || !readable((BYTE *)process.PebBaseAddress + 0x90, sizeof(shared)))
        return result;
    /*
     * PEB.SharedData (x64 offset 0x90, Wine winternl.h) is the silo selector.
     * The exact genuine ntdll RtlIsStateSeparationEnabled code uses the first
     * DWORD and byte +0x1d, otherwise KUSER_SHARED_DATA flags at 0x7ffe02f0,
     * bit 10. No platform state is manufactured or written.
     */
    memcpy(&shared, (BYTE *)process.PebBaseAddress + 0x90, sizeof(shared));
    if (shared) {
        if (!readable(shared, 0x1e)) return result;
        memcpy(&first, shared, sizeof(first));
        if (first) {
            result.silo_scope = 1;
            result.state_enabled = shared[0x1d];
            if (result.state_enabled > 1) return result;
            result.context = result.state_enabled ? PM_STATE_ENABLED : PM_STATE_DISABLED;
            return result;
        }
    }
    if (!readable((const void *)(ULONG_PTR)0x7ffe02f0, sizeof(DWORD))) return result;
    memcpy(&result.shared_flags, (const void *)(ULONG_PTR)0x7ffe02f0, sizeof(DWORD));
    result.state_enabled = !!(result.shared_flags & 0x400);
    result.context = result.state_enabled ? PM_STATE_ENABLED : PM_STATE_DISABLED;
    return result;
}

DWORD pm_state_resolve(PM_STATE_CONTEXT context, LPCWSTR source, LPCWSTR fallback,
                       LPWSTR target, DWORD bytes, LPDWORD required)
{
    SIZE_T length;
    DWORD needed;
    (void)source;
    /* Enabled scopes require the real NT redirection map; do not guess it. */
    if (context != PM_STATE_DISABLED) return ERROR_NOT_SUPPORTED;
    if (!fallback) return ERROR_FILE_NOT_FOUND;
    length = wcslen(fallback);
    if (length > (UINT32_MAX / sizeof(WCHAR)) - 1) return ERROR_ARITHMETIC_OVERFLOW;
    needed = (DWORD)((length + 1) * sizeof(WCHAR));
    if (bytes < needed) {
        if (required) *required = needed;
        return ERROR_MORE_DATA;
    }
    /* Windows faults for this invalid input; the bridge fails safely instead. */
    if (!target) return ERROR_INVALID_PARAMETER;
    memcpy(target, fallback, needed);
    if (required) *required = needed;
    return ERROR_SUCCESS;
}

__declspec(dllexport) DWORD WINAPI GetPersistedRegistryLocationW(
    LPCWSTR source, LPCWSTR fallback, LPWSTR target, DWORD bytes, LPDWORD required)
{
    DWORD last_error = GetLastError();
    PM_STATE_FACTS facts = pm_state_query_context();
    DWORD result = pm_state_resolve(facts.context, source, fallback, target, bytes, required);
    fprintf(stderr, "PackageManager state: GetPersistedRegistryLocationW context=%d result=%lu\n",
            (int)facts.context, result);
    SetLastError(last_error);
    return result;
}
