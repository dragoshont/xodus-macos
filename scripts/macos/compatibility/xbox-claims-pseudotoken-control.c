/* Read-only diagnosis of the exact native-XAML claims-call shape. */
#include "package-claims.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    HMODULE helper;
    XPC_QUERY_CLAIMS claims;
    XPC_QUERY_ATTRIBUTES query = (XPC_QUERY_ATTRIBUTES)(ULONG_PTR)GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "NtQuerySecurityAttributesToken");
    WCHAR sys[] = L"WIN://SYSAPPID", pkg[] = L"WIN://PKG";
    UNICODE_STRING names[2] = {{sizeof(sys)-2, sizeof(sys), sys},
                              {sizeof(pkg)-2, sizeof(pkg), pkg}};
    HANDLE own;
    ULONG bytes;
    ULONG64 mask;
    NTSTATUS status;
    unsigned i;
    if (argc != 2 || !query) return 2;
    helper = LoadLibraryA(argv[1]);
    if (!helper) return 2;
    claims = (XPC_QUERY_CLAIMS)(ULONG_PTR)GetProcAddress(helper, "RtlQueryPackageClaims");
    if (!claims || !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &own)) return 2;
    for (i = 0; i < 2; ++i) {
        HANDLE token = i ? (HANDLE)(LONG_PTR)-4 : own;
        bytes = 0xcccccccc;
        status = query(token, names, 2, NULL, 0, &bytes);
        printf("%s NtFiltered=%08lx bytes=%08lx\n", i ? "CURRENT_PROCESS_TOKEN_-4" : "EXPLICIT_TOKEN",
               (unsigned long)status, bytes);
        mask = ~((ULONG64)0);
        status = claims(token, NULL, NULL, NULL, NULL, NULL, NULL, &mask);
        printf("%s RtlExactXamlShape=%08lx mask=%llx\n", i ? "CURRENT_PROCESS_TOKEN_-4" : "EXPLICIT_TOKEN",
               (unsigned long)status, (unsigned long long)mask);
    }
    CloseHandle(own); FreeLibrary(helper);
    return 0;
}
