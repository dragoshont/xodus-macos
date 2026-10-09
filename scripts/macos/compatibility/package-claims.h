#ifndef XODUS_PACKAGE_CLAIMS_H
#define XODUS_PACKAGE_CLAIMS_H

#include <windows.h>
#include <winternl.h>

/* Public phnt ABI: sizes are bytes, and the claim is exactly eight bytes. */
typedef struct XPC_PACKAGE_CLAIM {
    ULONG Flags;
    ULONG Origin;
} XPC_PACKAGE_CLAIM;

typedef NTSTATUS (NTAPI *XPC_QUERY_CLAIMS)(
    HANDLE, PWSTR, SIZE_T *, PWSTR, SIZE_T *, GUID *,
    XPC_PACKAGE_CLAIM *, ULONG64 *);

#ifdef XPC_BUILD_DLL
#define XPC_EXPORT __declspec(dllexport)
#else
#define XPC_EXPORT
#endif

XPC_EXPORT NTSTATUS NTAPI RtlQueryPackageClaims(
    HANDLE token, PWSTR package_name, SIZE_T *package_bytes,
    PWSTR app_id, SIZE_T *app_bytes, GUID *dynamic_id,
    XPC_PACKAGE_CLAIM *claim, ULONG64 *attributes_present);

/* Native token attribute layout, not the Win32 CLAIM_SECURITY_ATTRIBUTE layout. */
typedef struct XPC_ATTRIBUTE {
    UNICODE_STRING Name;
    USHORT ValueType;
    USHORT Reserved;
    ULONG Flags;
    ULONG ValueCount;
    union {
        ULONG64 *Uint64;
        UNICODE_STRING *String;
        void *Pointer;
    } Values;
} XPC_ATTRIBUTE;

typedef struct XPC_ATTRIBUTES {
    USHORT Version;
    USHORT Reserved;
    ULONG AttributeCount;
    XPC_ATTRIBUTE *AttributeV1;
} XPC_ATTRIBUTES;

typedef NTSTATUS (NTAPI *XPC_QUERY_ATTRIBUTES)(
    HANDLE, UNICODE_STRING *, ULONG, void *, ULONG, ULONG *);

#ifdef XPC_LOCAL_ATTRIBUTE_PROVIDER
XPC_EXPORT NTSTATUS NTAPI NtQuerySecurityAttributesToken(
    HANDLE token, UNICODE_STRING *attributes, ULONG count,
    void *buffer, ULONG bytes, ULONG *required);
#endif

#ifdef XPC_TEST
NTSTATUS XpcQueryPackageClaimsWithProvider(
    XPC_QUERY_ATTRIBUTES provider, HANDLE token, PWSTR package_name,
    SIZE_T *package_bytes, PWSTR app_id, SIZE_T *app_bytes,
    GUID *dynamic_id, XPC_PACKAGE_CLAIM *claim, ULONG64 *attributes_present);
#endif

#endif
