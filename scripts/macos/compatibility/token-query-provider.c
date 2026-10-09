/*
 * Experimental import-target DLL provider, not an ntdll replacement.
 * Exports the native filtered query ABI using REAL class-39 token data.
 * Copyright 2026 Xodus contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Combine with package-claims.c using -DXPC_BUILD_DLL
 * -DXPC_LOCAL_ATTRIBUTE_PROVIDER. Both XAML imports can deliberately target
 * this DLL; RtlQueryPackageClaims calls this local provider without trying to
 * resolve a nonexistent ntdll filtered-query export. Other ntdll calls stay
 * untouched. Do not install this DLL as ntdll or override core DLLs globally.
 *
 * The TokenType query first invokes real token handle/access validation.
 * A missing/stubbed class 39 is NOT_SUPPORTED, NEVER a fabricated empty store.
 * An empty result is possible ONLY after class 39 successfully returns an
 * actual version-1/count-0 buffer. No package activation or setter is added.
 * Native 32-on-64 class-39 layout is not assumed: when an existing native
 * filtered API is available on x86, it is used; incompatible raw layouts fail.
 */
#include "token-query-provider.h"
#include <string.h>

#define SUCCESS ((NTSTATUS)0)
#define INVALID_PARAMETER ((NTSTATUS)0xc000000d)
#define TOO_SMALL ((NTSTATUS)0xc0000023)
#define NOT_FOUND ((NTSTATUS)0xc0000225)
#define NOT_SUPPORTED ((NTSTATUS)0xc00000bb)
#define NO_MEMORY ((NTSTATUS)0xc0000017)
#define MAX_BYTES (1024u * 1024u)
#define MAX_ATTRIBUTES 256u

typedef struct { ULONG64 Version; UNICODE_STRING Name; } FQBN_VALUE;
typedef struct { void *Value; ULONG Bytes; } OCTET_VALUE;
typedef BOOLEAN (NTAPI *EQUAL_STRING)(const UNICODE_STRING *, const UNICODE_STRING *, BOOLEAN);

static NTSTATUS supported_status(NTSTATUS status)
{
    if (status == (NTSTATUS)0xc0000002 || status == (NTSTATUS)0xc0000003)
        return NOT_SUPPORTED;
    return status;
}

static int range(const void *base, ULONG bytes, const void *p, SIZE_T n)
{
    ULONG_PTR start = (ULONG_PTR)base, at = (ULONG_PTR)p;
    return at >= start && at - start <= bytes && n <= bytes - (at - start);
}

static int valid_string(const void *base, ULONG bytes, const UNICODE_STRING *s)
{
    return !(s->Length & 1) && s->Length <= 65532 &&
        (!s->Length || range(base, bytes, s->Buffer, s->Length));
}

static SIZE_T value_bytes(USHORT type)
{
    switch (type) {
    case 1: case 2: case 6: return sizeof(ULONG64);
    case 3: return sizeof(UNICODE_STRING);
    case 4: return sizeof(FQBN_VALUE);
    case 5: case 16: return sizeof(OCTET_VALUE);
    default: return 0;
    }
}

static int reserve(SIZE_T *at, SIZE_T n, SIZE_T alignment, SIZE_T *offset)
{
    SIZE_T aligned;
    if (*at > MAX_BYTES || alignment - 1 > MAX_BYTES - *at) return 0;
    aligned = (*at + alignment - 1) & ~(alignment - 1);
    if (n > MAX_BYTES - aligned) return 0;
    *offset = aligned; *at = aligned + n;
    return 1;
}

static int string_output(SIZE_T *at, const UNICODE_STRING *source,
                         BYTE *output, UNICODE_STRING *dest)
{
    SIZE_T offset;
    if (!reserve(at, (SIZE_T)source->Length + sizeof(WCHAR), sizeof(WCHAR), &offset)) return 0;
    if (output) {
        dest->Length = source->Length;
        dest->MaximumLength = source->Length + sizeof(WCHAR);
        dest->Buffer = (PWSTR)(output + offset);
        if (source->Length) memcpy(dest->Buffer, source->Buffer, source->Length);
        dest->Buffer[source->Length / sizeof(WCHAR)] = 0;
    }
    return 1;
}

static NTSTATUS validate_attribute(const XPC_ATTRIBUTES *info, ULONG bytes,
                                   const XPC_ATTRIBUTE *a)
{
    SIZE_T item = value_bytes(a->ValueType);
    ULONG i;
    if (!item) return NOT_SUPPORTED;
    if (!valid_string(info, bytes, &a->Name) || a->Reserved ||
        a->ValueCount > MAX_BYTES / item ||
        (a->ValueCount && !range(info, bytes, a->Values.Pointer, (SIZE_T)a->ValueCount * item)))
        return NOT_SUPPORTED;
    for (i = 0; i < a->ValueCount; ++i) {
        if (a->ValueType == 3 &&
            !valid_string(info, bytes, &a->Values.String[i])) return NOT_SUPPORTED;
        if (a->ValueType == 4 &&
            !valid_string(info, bytes, &((FQBN_VALUE *)a->Values.Pointer)[i].Name))
            return NOT_SUPPORTED;
        if (a->ValueType == 5 || a->ValueType == 16) {
            OCTET_VALUE *v = &((OCTET_VALUE *)a->Values.Pointer)[i];
            if (v->Bytes > MAX_BYTES || (v->Bytes && !range(info, bytes, v->Value, v->Bytes)))
                return NOT_SUPPORTED;
        }
    }
    return SUCCESS;
}

static NTSTATUS marshal(const XPC_ATTRIBUTES *info, ULONG input_bytes,
                        const ULONG *selected, ULONG count,
                        void *buffer, ULONG bytes, ULONG *required)
{
    SIZE_T at = sizeof(XPC_ATTRIBUTES) + count * sizeof(XPC_ATTRIBUTE), offset;
    ULONG i, j, pass;
    XPC_ATTRIBUTES *out = buffer;
    NTSTATUS status;
    for (i = 0; i < count; ++i)
        if ((status = validate_attribute(info, input_bytes, &info->AttributeV1[selected[i]])) < 0)
            return status;
    for (pass = 0; pass < 2; ++pass) {
        BYTE *output = pass ? buffer : NULL;
        at = sizeof(XPC_ATTRIBUTES) + count * sizeof(XPC_ATTRIBUTE);
        if (pass) {
            memset(buffer, 0, *required);
            out->Version = 1; out->AttributeCount = count;
            out->AttributeV1 = count ? (XPC_ATTRIBUTE *)(out + 1) : NULL;
        }
        for (i = 0; i < count; ++i) {
            const XPC_ATTRIBUTE *source = &info->AttributeV1[selected[i]];
            XPC_ATTRIBUTE *dest = pass ? &out->AttributeV1[i] : NULL;
            SIZE_T item = value_bytes(source->ValueType);
            SIZE_T alignment = (source->ValueType == 1 || source->ValueType == 2 ||
                                source->ValueType == 4 || source->ValueType == 6) ?
                sizeof(ULONG64) : sizeof(void *);
            if (!string_output(&at, &source->Name, output, dest ? &dest->Name : NULL) ||
                !reserve(&at, source->ValueCount * item, alignment, &offset))
                return NOT_SUPPORTED;
            if (pass) {
                dest->ValueType = source->ValueType;
                dest->Flags = source->Flags; dest->ValueCount = source->ValueCount;
                dest->Values.Pointer = source->ValueCount ? output + offset : NULL;
                if (source->ValueCount)
                    memcpy(dest->Values.Pointer, source->Values.Pointer, source->ValueCount * item);
            }
            for (j = 0; j < source->ValueCount; ++j) {
                if (source->ValueType == 3) {
                    if (!string_output(&at, &source->Values.String[j], output,
                                       pass ? &dest->Values.String[j] : NULL)) return NOT_SUPPORTED;
                } else if (source->ValueType == 4) {
                    FQBN_VALUE *s = &((FQBN_VALUE *)source->Values.Pointer)[j];
                    FQBN_VALUE *d = pass ? &((FQBN_VALUE *)dest->Values.Pointer)[j] : NULL;
                    if (!string_output(&at, &s->Name, output, d ? &d->Name : NULL))
                        return NOT_SUPPORTED;
                } else if (source->ValueType == 5 || source->ValueType == 16) {
                    OCTET_VALUE *s = &((OCTET_VALUE *)source->Values.Pointer)[j];
                    if (!reserve(&at, s->Bytes, 1, &offset)) return NOT_SUPPORTED;
                    if (pass) {
                        OCTET_VALUE *d = &((OCTET_VALUE *)dest->Values.Pointer)[j];
                        d->Value = s->Bytes ? output + offset : NULL;
                        if (s->Bytes) memcpy(d->Value, s->Value, s->Bytes);
                    }
                }
            }
        }
        if (!pass) {
            *required = (ULONG)at;
            if (bytes < at) return TOO_SMALL;
            if (!buffer) return INVALID_PARAMETER;
        }
    }
    return SUCCESS;
}

static NTSTATUS query_from_information(
    XPC_QUERY_INFORMATION provider, HANDLE token, UNICODE_STRING *names,
    ULONG count, void *buffer, ULONG bytes, ULONG *required)
{
    XPC_ATTRIBUTES *info = NULL;
    ULONG type = 0, needed = 0, allocated = 0, selected[MAX_ATTRIBUTES];
    ULONG i, j, n = 0, attempt;
    NTSTATUS status;
    EQUAL_STRING equal;
    if (!required || count > MAX_ATTRIBUTES || (count && !names) ||
        (sizeof(void *) == 8 && !count && names)) return INVALID_PARAMETER;
    for (i = 0; i < count; ++i)
        if (!names[i].Buffer || !names[i].Length || (names[i].Length & 1))
            return INVALID_PARAMETER;
    if (!provider) return NOT_SUPPORTED;
    /* Class 39 implementations may be stubs before checking handles. */
    status = provider(token, TokenType, &type, sizeof(type), &needed);
    if (status < 0) return supported_status(status);
    if (needed != sizeof(type) || (type != TokenPrimary && type != TokenImpersonation))
        return NOT_SUPPORTED;
    status = supported_status(provider(token, (TOKEN_INFORMATION_CLASS)39, NULL, 0, &needed));
    if (status != TOO_SMALL) return status < 0 ? status : NOT_SUPPORTED;
    for (attempt = 0; attempt < 3; ++attempt) {
        allocated = needed;
        if (allocated < sizeof(*info) || allocated > MAX_BYTES) return NOT_SUPPORTED;
        info = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, allocated);
        if (!info) return NO_MEMORY;
        status = supported_status(provider(token, (TOKEN_INFORMATION_CLASS)39, info, allocated, &needed));
        if (status != TOO_SMALL) break;
        HeapFree(GetProcessHeap(), 0, info); info = NULL;
    }
    if (status < 0) goto done;
    if (!info || needed < sizeof(*info) || needed > allocated || info->Version != 1 || info->Reserved ||
        info->AttributeCount > MAX_ATTRIBUTES ||
        (info->AttributeCount &&
         !range(info, needed, info->AttributeV1,
                (SIZE_T)info->AttributeCount * sizeof(XPC_ATTRIBUTE)))) {
        status = NOT_SUPPORTED; goto done;
    }
    if (count) {
        if (!info->AttributeCount) {
            *required = 0; status = NOT_FOUND; goto done;
        }
        equal = (EQUAL_STRING)(ULONG_PTR)GetProcAddress(
            GetModuleHandleW(L"ntdll.dll"), "RtlEqualUnicodeString");
        if (!equal) { status = NOT_SUPPORTED; goto done; }
        for (i = 0; i < count; ++i) {
            for (j = 0; j < info->AttributeCount; ++j) {
                UNICODE_STRING *name = &info->AttributeV1[j].Name;
                if (!valid_string(info, needed, name)) { status = NOT_SUPPORTED; goto done; }
                if (equal(name, &names[i], TRUE)) break;
            }
            if (j == info->AttributeCount) {
                *required = 0; status = NOT_FOUND; goto done;
            }
            selected[n++] = j;
        }
    } else {
        for (i = 0; i < info->AttributeCount; ++i) selected[n++] = i;
    }
    status = marshal(info, needed, selected, n, buffer, bytes, required);
done:
    if (info) HeapFree(GetProcessHeap(), 0, info);
    return status;
}

NTSTATUS NTAPI NtQuerySecurityAttributesToken(
    HANDLE token, UNICODE_STRING *names, ULONG count,
    void *buffer, ULONG bytes, ULONG *required)
{
    XPC_QUERY_INFORMATION info = (XPC_QUERY_INFORMATION)(ULONG_PTR)GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationToken");
#if !defined(_WIN64) && !defined(XPC_FORCE_CLASS39)
    XPC_QUERY_ATTRIBUTES native = (XPC_QUERY_ATTRIBUTES)(ULONG_PTR)GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "NtQuerySecurityAttributesToken");
    if (native && native != NtQuerySecurityAttributesToken) {
        ULONG type = 0, needed = 0;
        NTSTATUS status;
        if (!required || count > MAX_ATTRIBUTES || (count && !names)) return INVALID_PARAMETER;
        if (!info) return NOT_SUPPORTED;
        status = info(token, TokenType, &type, sizeof(type), &needed);
        if (status < 0) return supported_status(status);
        if (needed != sizeof(type) || (type != TokenPrimary && type != TokenImpersonation))
            return NOT_SUPPORTED;
        return supported_status(native(token, names, count, buffer, bytes, required));
    }
#endif
    return query_from_information(info, token, names, count, buffer, bytes, required);
}

#ifdef XPC_TEST
NTSTATUS XpcQueryAttributesFromInformation(
    XPC_QUERY_INFORMATION provider, HANDLE token, UNICODE_STRING *names,
    ULONG count, void *buffer, ULONG bytes, ULONG *required)
{
    return query_from_information(provider, token, names, count, buffer, bytes, required);
}
#endif
