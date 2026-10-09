/* Real coherent-Wine runtime contract test; no fixtures or provisioning.
 * A fresh prefix must have genuine empty legacy-token attributes.
 * Run: token-runtime-test.exe <absolute tested Rtl-only helper DLL path>
 */
#include "token-query-provider.h"
#include <stdio.h>
#include <string.h>

static unsigned tests, failures;
static void check(int condition, const char *label)
{
    ++tests;
    if (!condition) { ++failures; printf("FAIL %s\n", label); }
}

static int sentinel(const BYTE *bytes, SIZE_T count)
{
    SIZE_T i;
    for (i = 0; i < count; ++i) if (bytes[i] != 0xcc) return 0;
    return 1;
}

static void empty_token(XPC_QUERY_ATTRIBUTES query, XPC_QUERY_INFORMATION info,
                        XPC_QUERY_CLAIMS claims, HANDLE token, const char *label)
{
    union { ULONG64 align; BYTE bytes[256]; } buffer;
    WCHAR filter_text[] = L"WIN://SYSAPPID", package[1024], app[1024];
    UNICODE_STRING filter = {sizeof(filter_text) - 2, sizeof(filter_text), filter_text};
    ULONG required = 0xcccccccc;
    NTSTATUS status;
    SIZE_T pn = sizeof(package), an = sizeof(app);
    BYTE claim[32], dynamic[32];
    ULONG64 mask = ~((ULONG64)0);
    XPC_ATTRIBUTES *attributes = (XPC_ATTRIBUTES *)buffer.bytes;
    memset(buffer.bytes, 0xcc, sizeof(buffer.bytes));
    status = query(token, NULL, 0, NULL, 0, &required);
    check(status == (NTSTATUS)0xc0000023 && required == sizeof(*attributes),
          "real empty store size probe");
    status = query(token, NULL, 0, buffer.bytes, required - 1, &required);
    check(status == (NTSTATUS)0xc0000023 && sentinel(buffer.bytes, sizeof(buffer.bytes)),
          "short buffer untouched");
    status = query(token, NULL, 0, buffer.bytes, sizeof(buffer.bytes), &required);
    check(!status && required == sizeof(*attributes) && attributes->Version == 1 &&
          !attributes->Reserved && !attributes->AttributeCount && !attributes->AttributeV1 &&
          sentinel(buffer.bytes + required, sizeof(buffer.bytes) - required),
          "actual server-backed version-one empty legacy store");
    printf("REAL_STORE %s status=%08lx bytes=%lu version=%u count=%lu\n",
           label, (unsigned long)status, required, attributes->Version, attributes->AttributeCount);
    memset(buffer.bytes, 0xcc, sizeof(buffer.bytes)); required = 0xcccccccc;
    status = query(token, &filter, 1, buffer.bytes, sizeof(buffer.bytes), &required);
    check(status == (NTSTATUS)0xc0000225 && !required &&
          sentinel(buffer.bytes, sizeof(buffer.bytes)), "actual missing package filter");
    memset(buffer.bytes, 0xcc, sizeof(buffer.bytes)); required = 0xcccccccc;
    status = info(token, (TOKEN_INFORMATION_CLASS)39, buffer.bytes, sizeof(buffer.bytes), &required);
    check(!status && required == sizeof(*attributes) && attributes->Version == 1 &&
          !attributes->AttributeCount, "real class-39 routing");
    memset(package, 0xcc, sizeof(package)); memset(app, 0xcc, sizeof(app));
    memset(claim, 0xcc, sizeof(claim)); memset(dynamic, 0xcc, sizeof(dynamic));
    status = claims(token, package, &pn, app, &an, (GUID *)dynamic,
                    (XPC_PACKAGE_CLAIM *)claim, &mask);
    check(status == (NTSTATUS)0xc0000225 && !mask &&
          pn == sizeof(package) && an == sizeof(app) &&
          sentinel((BYTE *)package, sizeof(package)) && sentinel((BYTE *)app, sizeof(app)) &&
          sentinel(claim, sizeof(claim)) && sentinel(dynamic, sizeof(dynamic)),
          "real unpackaged Rtl query preserves outputs");
    printf("REAL_CLAIMS %s status=%08lx mask=%llx\n", label,
           (unsigned long)status, (unsigned long long)mask);
}

int main(int argc, char **argv)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"), helper;
    XPC_QUERY_ATTRIBUTES query = (XPC_QUERY_ATTRIBUTES)(ULONG_PTR)GetProcAddress(
        ntdll, "NtQuerySecurityAttributesToken");
    XPC_QUERY_INFORMATION info = (XPC_QUERY_INFORMATION)(ULONG_PTR)GetProcAddress(
        ntdll, "NtQueryInformationToken");
    XPC_QUERY_CLAIMS claims;
    HANDLE token = NULL, duplicate = NULL, no_query = NULL;
    HANDLE event;
    union { ULONG64 align; BYTE data[256]; } output;
    ULONG required;
    ULONG64 mask;
    NTSTATUS status;
    BYTE claim[32], dynamic[32];
    SIZE_T pn, an;
    WCHAR package[1024], app[1024];
    if (argc != 2 || !query || !info) { puts("RUNTIME_OR_QUERY_EXPORT_MISSING"); return 2; }
    helper = LoadLibraryA(argv[1]);
    if (!helper) { printf("RTL_HELPER_LOAD_FAILED=%lu\n", GetLastError()); return 2; }
    claims = (XPC_QUERY_CLAIMS)(ULONG_PTR)GetProcAddress(helper, "RtlQueryPackageClaims");
    if (!claims || !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &token) ||
        !OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE, &no_query)) return 2;
    event = CreateEventW(NULL, FALSE, FALSE, NULL);
    check(event != NULL, "classic synchronization event creation");
    if (event) {
        check(WaitForSingleObject(event, 0) == WAIT_TIMEOUT, "unsignaled wait is not fake success");
        check(SetEvent(event), "classic synchronization signal");
        check(WaitForSingleObject(event, 0) == WAIT_OBJECT_0, "actual signaled wait");
        check(WaitForSingleObject(event, 0) == WAIT_TIMEOUT, "actual auto-reset behavior");
        CloseHandle(event);
    }
    empty_token(query, info, claims, token, "own");
    check(DuplicateTokenEx(token, TOKEN_QUERY, NULL, SecurityImpersonation, TokenPrimary, &duplicate),
          "actual token duplication");
    if (duplicate) empty_token(query, info, claims, duplicate, "duplicate");
    memset(output.data, 0xcc, sizeof(output.data)); required = 0xcccccccc;
    status = query((HANDLE)(ULONG_PTR)0x1234, NULL, 0, output.data, sizeof(output.data), &required);
    check(status == (NTSTATUS)0xc0000008 && required == 0xcccccccc &&
          sentinel(output.data, sizeof(output.data)), "actual invalid-handle check");
    printf("REAL_INVALID_NT status=%08lx required=%08lx\n", (unsigned long)status, required);
    status = query(no_query, NULL, 0, output.data, sizeof(output.data), &required);
    check(status == (NTSTATUS)0xc0000022 && required == 0xcccccccc &&
          sentinel(output.data, sizeof(output.data)), "actual TOKEN_QUERY access check");
    printf("REAL_ACCESS_NT status=%08lx required=%08lx\n", (unsigned long)status, required);
    pn = sizeof(package); an = sizeof(app); mask = ~((ULONG64)0);
    memset(package, 0xcc, sizeof(package)); memset(app, 0xcc, sizeof(app));
    memset(claim, 0xcc, sizeof(claim)); memset(dynamic, 0xcc, sizeof(dynamic));
    status = claims((HANDLE)(ULONG_PTR)0x1234, package, &pn, app, &an,
                    (GUID *)dynamic, (XPC_PACKAGE_CLAIM *)claim, &mask);
    check(status == (NTSTATUS)0xc0000008 && !mask && pn == sizeof(package) &&
          an == sizeof(app) && sentinel(claim, sizeof(claim)) &&
          sentinel(dynamic, sizeof(dynamic)) && sentinel((BYTE *)package, sizeof(package)) &&
          sentinel((BYTE *)app, sizeof(app)), "actual invalid-handle Rtl output semantics");
    printf("REAL_INVALID_RTL status=%08lx mask=%llx\n", (unsigned long)status,
           (unsigned long long)mask);
    if (duplicate) CloseHandle(duplicate);
    CloseHandle(no_query); CloseHandle(token); FreeLibrary(helper);
    printf("RESULT actual_runtime_assertions=%u failures=%u\n", tests, failures);
    return failures ? 1 : 0;
}
