/* Native Windows control. Only this unpackaged process's own tokens are queried.
 * No general token attributes or account/claim payloads are printed.
 * x86_64-w64-mingw32-gcc -Wall -Wextra -Werror -O2
 *   token-security-native-control.c -ladvapi32 -o token-security-native-control.exe
 */
#include "package-claims.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef NTSTATUS (NTAPI *QUERY_INFO)(HANDLE, ULONG, void *, ULONG, ULONG *);

static void observe(XPC_QUERY_ATTRIBUTES query, HANDLE token, const char *label,
                    UNICODE_STRING *name, ULONG count, ULONG capacity)
{
    union { ULONG64 align; BYTE data[2048]; } output;
    ULONG required = 0xcccccccc;
    NTSTATUS status;
    memset(&output, 0xcc, sizeof(output));
    status = query(token, name, count, capacity ? output.data : NULL, capacity, &required);
    printf("%s count=%lu capacity=%lu status=%08lx required=%lu "
           "version=%04x count_out=%08lx first=%02x\n", label, count, capacity,
           (unsigned long)status, required, *(USHORT *)output.data,
           *(ULONG *)(output.data + 4), output.data[0]);
}

int main(void)
{
    XPC_QUERY_ATTRIBUTES query = (XPC_QUERY_ATTRIBUTES)(ULONG_PTR)GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "NtQuerySecurityAttributesToken");
    QUERY_INFO info = (QUERY_INFO)(ULONG_PTR)GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationToken");
    HANDLE token, no_query;
    WCHAR text[] = L"WIN://SYSAPPID";
    UNICODE_STRING name = {sizeof(text) - 2, sizeof(text), text};
    ULONG sizes[] = {0, 1, 11, 12, 15, 16, 2048}, i, needed;
    BYTE out[64];
    if (!query || !info) {
        puts("MISSING_NTDLL_TOKEN_QUERY_EXPORT: no attribute query performed");
        return 2;
    }
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) ||
        !OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE, &no_query)) return 2;
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        observe(query, token, "own/all", NULL, 0, sizes[i]);
        observe(query, token, "own/package-filter", &name, 1, sizes[i]);
    }
    observe(query, no_query, "own/no-TOKEN_QUERY", NULL, 0, 2048);
    observe(query, (HANDLE)(ULONG_PTR)0x1234, "invalid", NULL, 0, 2048);
    observe(query, token, "name/nonzero-count-zero", &name, 0, 2048);
    memset(out, 0xcc, sizeof(out)); needed = 0xcccccccc;
    printf("class39 own status=%08lx ", (unsigned long)info(token, 39, out, sizeof(out), &needed));
    printf("required=%lu version=%04x count=%08lx\n", needed, *(USHORT *)out, *(ULONG *)(out + 4));
    CloseHandle(no_query); CloseHandle(token);
    return 0;
}
