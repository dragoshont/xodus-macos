/*
 * Standalone compatibility prototype. No token, identity, entitlement, or
 * sign-in state is synthesized. Loading this DLL does not override ntdll;
 * integration requires an intentional import/provider route or Wine patch.
 *
 * Build on macOS with MinGW (also works with a Windows C compiler):
 * x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -O2 -shared
 *   -DXPC_BUILD_DLL package-claims.c -o package-claims.dll
 * For i686 add -Wl,--kill-at to export the undecorated NTAPI name.
 * To explicitly route to the class-39 bridge in the SAME DLL, additionally
 * compile token-query-provider.c with -DXPC_LOCAL_ATTRIBUTE_PROVIDER.
 *
 * Provider requirement: NtQuerySecurityAttributesToken must return version-1
 * native attributes backed by the actual token: WIN://SYSAPPID (strings in
 * package/app/family/dynamic-GUID order), WIN://PKG (packed Flags/Origin).
 * WP://SKUID and XBOX://LI are queried for presence only, never their values.
 * A Wine provider must persist genuine package activation attributes through
 * token creation/duplication and support filtered queries (and class-39
 * TokenSecurityAttributes for other consumers). A missing/stubbed provider
 * is NOT_SUPPORTED, not an unpackaged token or packaged success.
 */
#include "package-claims.h"
#include <string.h>
#include <wchar.h>

#define XPC_SUCCESS ((NTSTATUS)0)
#define XPC_OVERFLOW ((NTSTATUS)0x80000005)
#define XPC_INVALID_PARAMETER ((NTSTATUS)0xc000000d)
#define XPC_NO_MEMORY ((NTSTATUS)0xc0000017)
#define XPC_TOO_SMALL ((NTSTATUS)0xc0000023)
#define XPC_NOT_FOUND ((NTSTATUS)0xc0000225)
#define XPC_NOT_SUPPORTED ((NTSTATUS)0xc00000bb)
#define XPC_NOT_IMPLEMENTED ((NTSTATUS)0xc0000002)
#define XPC_INVALID_INFO_CLASS ((NTSTATUS)0xc0000003)
#define XPC_MISSING_APP ((NTSTATUS)0xc000050c)
#define XPC_MAX_ATTRIBUTES_BYTES (1024u * 1024u)

typedef NTSTATUS (NTAPI *XPC_GUID_FROM_STRING)(const UNICODE_STRING *, GUID *);

static UNICODE_STRING string_name(const WCHAR *name)
{
    UNICODE_STRING s;
    s.Length = (USHORT)(wcslen(name) * sizeof(WCHAR));
    s.MaximumLength = s.Length + sizeof(WCHAR);
    s.Buffer = (PWSTR)name;
    return s;
}

static NTSTATUS provider_status(NTSTATUS status)
{
    if (status == XPC_NOT_IMPLEMENTED || status == XPC_INVALID_INFO_CLASS)
        return XPC_NOT_SUPPORTED;
    return status;
}

static int in_buffer(const void *buffer, ULONG bytes, const void *p, SIZE_T n)
{
    ULONG_PTR base = (ULONG_PTR)buffer, at = (ULONG_PTR)p;
    return at >= base && at - base <= bytes && n <= bytes - (at - base);
}

static int valid_string(const void *buffer, ULONG bytes, const UNICODE_STRING *s)
{
    return !(s->Length % sizeof(WCHAR)) &&
        s->MaximumLength >= s->Length &&
        (!s->Length || in_buffer(buffer, bytes, s->Buffer, s->Length));
}

static XPC_ATTRIBUTE *find_attribute(XPC_ATTRIBUTES *info, ULONG bytes,
                                     const WCHAR *name)
{
    ULONG i;
    UNICODE_STRING wanted = string_name(name);
    for (i = 0; i < info->AttributeCount; ++i) {
        XPC_ATTRIBUTE *a = &info->AttributeV1[i];
        if (!valid_string(info, bytes, &a->Name)) return NULL;
        if (a->Name.Length == wanted.Length &&
            !memcmp(a->Name.Buffer, wanted.Buffer, wanted.Length)) return a;
    }
    return NULL;
}

static NTSTATUS read_attributes(XPC_QUERY_ATTRIBUTES provider, HANDLE token,
                               UNICODE_STRING *names, ULONG count,
                               XPC_ATTRIBUTES **result, ULONG *bytes)
{
    NTSTATUS status;
    ULONG needed = 0, attempt;
    *result = NULL;
    status = provider_status(provider(token, names, count, NULL, 0, &needed));
    if (status != XPC_TOO_SMALL) return status < 0 ? status : XPC_INVALID_PARAMETER;
    for (attempt = 0; attempt < 3; ++attempt) {
        XPC_ATTRIBUTES *info;
        ULONG allocated = needed;
        if (allocated < sizeof(*info) || allocated > XPC_MAX_ATTRIBUTES_BYTES)
            return XPC_INVALID_PARAMETER;
        info = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, allocated);
        if (!info) return XPC_NO_MEMORY;
        status = provider_status(provider(token, names, count, info, allocated, &needed));
        if (status == XPC_TOO_SMALL) {
            HeapFree(GetProcessHeap(), 0, info);
            continue;
        }
        if (status < 0) {
            HeapFree(GetProcessHeap(), 0, info);
            return status;
        }
        if (info->Version != 1 ||
            info->AttributeCount > allocated / sizeof(XPC_ATTRIBUTE) ||
            !in_buffer(info, allocated, info->AttributeV1,
                       (SIZE_T)info->AttributeCount * sizeof(XPC_ATTRIBUTE))) {
            HeapFree(GetProcessHeap(), 0, info);
            return XPC_INVALID_PARAMETER;
        }
        *result = info;
        *bytes = allocated;
        return XPC_SUCCESS;
    }
    return XPC_TOO_SMALL;
}

static NTSTATUS copy_identity(PWSTR dest, SIZE_T *bytes,
                             const UNICODE_STRING *source)
{
    SIZE_T capacity, characters, copy;
    if (!dest || !bytes || *bytes < sizeof(WCHAR)) return XPC_INVALID_PARAMETER;
    capacity = *bytes / sizeof(WCHAR);
    characters = source->Length / sizeof(WCHAR);
    copy = characters < capacity ? characters : capacity - 1;
    if (copy) memcpy(dest, source->Buffer, copy * sizeof(WCHAR));
    dest[copy] = 0;
    /* Native no-truncation formatting clears the first character on overflow,
       but the rest of the truncated write remains observable. */
    if (characters >= capacity) {
        dest[0] = 0;
        return XPC_OVERFLOW;
    }
    *bytes = (characters + 1) * sizeof(WCHAR);
    return XPC_SUCCESS;
}

static NTSTATUS query_claims(XPC_QUERY_ATTRIBUTES provider, HANDLE token,
                            PWSTR package_name, SIZE_T *package_bytes,
                            PWSTR app_id, SIZE_T *app_bytes, GUID *dynamic_id,
                            XPC_PACKAGE_CLAIM *claim, ULONG64 *present)
{
    UNICODE_STRING names[2];
    XPC_ATTRIBUTES *info = NULL;
    XPC_ATTRIBUTE *identity, *pkg;
    ULONG bytes = 0, needed, i, count = (claim || present) ? 2 : 1;
    NTSTATUS status;
    if (present) *present = 0;
    if (!provider) return XPC_NOT_SUPPORTED;
    names[0] = string_name(L"WIN://SYSAPPID");
    names[1] = string_name(L"WIN://PKG");
    status = read_attributes(provider, token, names, count, &info, &bytes);
    if (status == XPC_NOT_FOUND && count == 2)
        status = read_attributes(provider, token, names, 1, &info, &bytes);
    if (status < 0) return status;
    identity = find_attribute(info, bytes, L"WIN://SYSAPPID");
    pkg = find_attribute(info, bytes, L"WIN://PKG");
    if (!identity) { status = XPC_NOT_FOUND; goto done; }
    if (identity->ValueType != 3 || !identity->ValueCount ||
        identity->ValueCount > bytes / sizeof(UNICODE_STRING) ||
        !in_buffer(info, bytes, identity->Values.String,
                   identity->ValueCount * sizeof(UNICODE_STRING))) {
        status = XPC_INVALID_PARAMETER; goto done;
    }
    for (i = 0; i < identity->ValueCount; ++i)
        if (!valid_string(info, bytes, &identity->Values.String[i])) {
            status = XPC_INVALID_PARAMETER; goto done;
        }
    if (pkg && (pkg->ValueType != 2 || pkg->ValueCount != 1 ||
                !in_buffer(info, bytes, pkg->Values.Uint64, sizeof(ULONG64)))) {
        status = XPC_INVALID_PARAMETER; goto done;
    }
    if (claim) {
        ULONG64 value = pkg ? pkg->Values.Uint64[0] : 0;
        claim->Flags = (ULONG)value;
        claim->Origin = (ULONG)(value >> 32);
    }
    if (present) {
        static const WCHAR *const optional[] = { L"WP://SKUID", L"XBOX://LI" };
        *present = pkg ? 3 : 1;
        for (i = 0; i < 2; ++i) {
            UNICODE_STRING name = string_name(optional[i]);
            status = provider_status(provider(token, &name, 1, NULL, 0, &needed));
            if (status == XPC_TOO_SMALL) *present |= (ULONG64)4 << i;
            else if (status != XPC_NOT_FOUND && status < 0) goto done;
        }
    }
    if (package_name || package_bytes) {
        status = copy_identity(package_name, package_bytes, &identity->Values.String[0]);
        if (status < 0) goto done;
    }
    if (app_id) {
        if (identity->ValueCount < 2) { status = XPC_MISSING_APP; goto done; }
        status = copy_identity(app_id, app_bytes, &identity->Values.String[1]);
        if (status < 0) goto done;
    }
    if (dynamic_id) {
        memset(dynamic_id, 0, sizeof(*dynamic_id));
        if (identity->ValueCount > 3) {
            XPC_GUID_FROM_STRING parse = (XPC_GUID_FROM_STRING)(ULONG_PTR)
                GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGUIDFromString");
            if (!parse) { status = XPC_NOT_SUPPORTED; goto done; }
            /* Native GUID conversion is optional and does not change status. */
            parse(&identity->Values.String[3], dynamic_id);
        }
    }
    status = XPC_SUCCESS;
done:
    HeapFree(GetProcessHeap(), 0, info);
    return status;
}

NTSTATUS NTAPI RtlQueryPackageClaims(
    HANDLE token, PWSTR package_name, SIZE_T *package_bytes,
    PWSTR app_id, SIZE_T *app_bytes, GUID *dynamic_id,
    XPC_PACKAGE_CLAIM *claim, ULONG64 *present)
{
#ifdef XPC_LOCAL_ATTRIBUTE_PROVIDER
    XPC_QUERY_ATTRIBUTES provider = NtQuerySecurityAttributesToken;
#else
    XPC_QUERY_ATTRIBUTES provider = (XPC_QUERY_ATTRIBUTES)(ULONG_PTR)
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySecurityAttributesToken");
#endif
    return query_claims(provider, token, package_name, package_bytes, app_id,
                        app_bytes, dynamic_id, claim, present);
}

#ifdef XPC_TEST
NTSTATUS XpcQueryPackageClaimsWithProvider(
    XPC_QUERY_ATTRIBUTES provider, HANDLE token, PWSTR package_name,
    SIZE_T *package_bytes, PWSTR app_id, SIZE_T *app_bytes, GUID *dynamic_id,
    XPC_PACKAGE_CLAIM *claim, ULONG64 *present)
{
    return query_claims(provider, token, package_name, package_bytes, app_id,
                        app_bytes, dynamic_id, claim, present);
}
#endif
