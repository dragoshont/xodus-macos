/* Read-only PE console probe. No GUI, provisioning, token writes, or payload
 * dumps. Run with an optional absolute combined-provider DLL path. Even when
 * the native filtered export is ABSENT, class 39 is still measured directly.
 */
#include "token-query-provider.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void information(XPC_QUERY_INFORMATION query, HANDLE token, const char *label)
{
    ULONG type = 0, required = 0xcccccccc, bytes;
    NTSTATUS status;
    XPC_ATTRIBUTES *data;
    status = query(token, TokenType, &type, sizeof(type), &required);
    printf("NATIVE_TOKEN_TYPE %s status=%08lx required=%lu\n",
           label, (unsigned long)status, required);
    required = 0xcccccccc;
    status = query(token, (TOKEN_INFORMATION_CLASS)39, NULL, 0, &required);
    printf("NATIVE_CLASS39_PROBE %s status=%08lx required=%lu\n",
           label, (unsigned long)status, required);
    if (status != (NTSTATUS)0xc0000023 || required < sizeof(*data) ||
        required > 1024u * 1024u) return;
    bytes = required;
    data = HeapAlloc(GetProcessHeap(), 0, bytes);
    if (!data) return;
    memset(data, 0xcc, bytes);
    status = query(token, (TOKEN_INFORMATION_CLASS)39, data, bytes, &required);
    printf("NATIVE_CLASS39_STORE %s status=%08lx required=%lu", label,
           (unsigned long)status, required);
    if (!status && required >= sizeof(*data) && required <= bytes &&
        data->Version == 1 && !data->Reserved) {
        printf(" validated_header_version=%u attributes=%lu empty=%s",
               data->Version, data->AttributeCount, data->AttributeCount ? "NO" : "YES");
    } else printf(" HEADER_NOT_VALIDATED");
    putchar('\n');
    HeapFree(GetProcessHeap(), 0, data);
}

static void filtered(XPC_QUERY_ATTRIBUTES query, HANDLE token, const char *label)
{
    union { ULONG64 alignment; BYTE bytes[2048]; } data;
    WCHAR text[] = L"WIN://SYSAPPID";
    UNICODE_STRING name = {sizeof(text) - 2, sizeof(text), text};
    ULONG required = 0xcccccccc;
    NTSTATUS status;
    memset(data.bytes, 0xcc, sizeof(data.bytes));
    status = query(token, &name, 1, data.bytes, sizeof(data.bytes), &required);
    printf("FILTERED_PACKAGE %s status=%08lx required=%lu first=%02x\n",
           label, (unsigned long)status, required, data.bytes[0]);
    required = 0xcccccccc;
    memset(data.bytes, 0xcc, sizeof(data.bytes));
    status = query(token, NULL, 0, data.bytes, sizeof(data.bytes), &required);
    printf("UNFILTERED %s status=%08lx required=%lu", label, (unsigned long)status, required);
    if (!status && required >= sizeof(XPC_ATTRIBUTES) && required <= sizeof(data.bytes)) {
        XPC_ATTRIBUTES *info = (XPC_ATTRIBUTES *)data.bytes;
        printf(" version=%u count=%lu", info->Version, info->AttributeCount);
    }
    putchar('\n');
}

static void claims(XPC_QUERY_CLAIMS query, HANDLE token, const char *label)
{
    WCHAR package[1024], app[1024];
    SIZE_T pn = sizeof(package), an = sizeof(app);
    GUID dynamic;
    XPC_PACKAGE_CLAIM claim;
    ULONG64 mask = UINT64_C(0xcccccccccccccccc);
    NTSTATUS status;
    memset(package, 0xcc, sizeof(package)); memset(app, 0xcc, sizeof(app));
    memset(&claim, 0xcc, sizeof(claim)); memset(&dynamic, 0xcc, sizeof(dynamic));
    status = query(token, package, &pn, app, &an, &dynamic, &claim, &mask);
    printf("CLAIMS %s status=%08lx mask=%llx package_bytes=%llu app_bytes=%llu\n",
           label, (unsigned long)status, (unsigned long long)mask,
           (unsigned long long)pn, (unsigned long long)an);
}

int main(int argc, char **argv)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"), dll = NULL;
    XPC_QUERY_INFORMATION info = (XPC_QUERY_INFORMATION)(ULONG_PTR)GetProcAddress(
        ntdll, "NtQueryInformationToken");
    XPC_QUERY_ATTRIBUTES native = (XPC_QUERY_ATTRIBUTES)(ULONG_PTR)GetProcAddress(
        ntdll, "NtQuerySecurityAttributesToken"), provider = NULL;
    XPC_QUERY_CLAIMS claim = NULL;
    HANDLE own, no_query;
    if (!info || argc > 2) return 2;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &own)) return 2;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE, &no_query)) {
        CloseHandle(own); return 2;
    }
    printf("ABI=%u NATIVE_FILTERED_EXPORT=%s\n",
           (unsigned)(sizeof(void *) * 8), native ? "PRESENT" : "ABSENT");
    information(info, own, "own"); information(info, (HANDLE)(ULONG_PTR)0x1234, "invalid");
    information(info, no_query, "no-TOKEN_QUERY");
    if (native) filtered(native, own, "native-own");
    if (argc == 2) {
        dll = LoadLibraryA(argv[1]);
        if (!dll) { printf("PROVIDER_LOAD_ERROR=%lu\n", GetLastError()); return 2; }
        provider = (XPC_QUERY_ATTRIBUTES)(ULONG_PTR)GetProcAddress(dll, "NtQuerySecurityAttributesToken");
        claim = (XPC_QUERY_CLAIMS)(ULONG_PTR)GetProcAddress(dll, "RtlQueryPackageClaims");
        if (!claim) { puts("CLAIMS_EXPORT_MISSING"); return 2; }
        if (!provider) {
            puts("Rtl_ONLY_DLL: filtered queries use real NTDLL");
            provider = native;
        }
        if (provider) {
            filtered(provider, own, "provider-own");
            filtered(provider, (HANDLE)(ULONG_PTR)0x1234, "provider-invalid");
            filtered(provider, no_query, "provider-no-TOKEN_QUERY");
        }
        claims(claim, own, "provider-own");
        claims(claim, (HANDLE)(ULONG_PTR)0x1234, "provider-invalid");
        FreeLibrary(dll);
    }
    CloseHandle(no_query); CloseHandle(own);
    return 0;
}
