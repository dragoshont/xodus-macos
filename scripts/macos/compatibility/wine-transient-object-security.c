/*
 * Transient-object security descriptor lookup.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */
#define WIN32_NO_STATUS
#define WINADVAPI
#include <stdarg.h>
#include "windef.h"
#include "winbase.h"
#include "winternl.h"
#include "sddl.h"
#undef WIN32_NO_STATUS
#include "ntstatus.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(service);

static const WCHAR *transient_object_type(ULONG type)
{
    switch (type)
    {
    case 0: return L"Mutex";
    case 1: return L"Event";
    case 2: return L"Semaphore";
    case 3: return L"WaitableTimer";
    case 4: return L"JobObject";
    case 5: return L"FileMapping";
    case 6: return L"NamedPipe";
    case 8: return L"AlpcPort";
    case 9: return L"Rpc";
    case 10: return L"Wnf";
    case 11: return L"Template";
    case 12: return L"Private";
    default: return NULL;
    }
}

void WINAPI FreeTransientObjectSecurityDescriptor(void *descriptor)
{
    if (descriptor) HeapFree(GetProcessHeap(), 0, descriptor);
}

NTSTATUS WINAPI QueryTransientObjectSecurityDescriptor(ULONG type, const WCHAR *name,
                                                        SECURITY_DESCRIPTOR **descriptor)
{
    static const WCHAR root[] =
        L"\\Registry\\Machine\\Software\\Microsoft\\SecurityManager\\TransientObjects\\";
    static const WCHAR prefix[] = L"%5C%5C.%5C";
    const WCHAR *kind;
    WCHAR *path, *cursor;
    SIZE_T name_length, path_length, i;
    UNICODE_STRING key_name, value_name;
    OBJECT_ATTRIBUTES attributes;
    KEY_VALUE_PARTIAL_INFORMATION header, *value = NULL;
    HANDLE key = NULL;
    ULONG bytes;
    SECURITY_DESCRIPTOR *converted = NULL, *owned = NULL;
    DWORD converted_bytes;
    NTSTATUS status;

    if (!name || !descriptor || !(kind = transient_object_type(type)))
        return STATUS_INVALID_PARAMETER;
    name_length = wcslen(name);
    path_length = ARRAY_SIZE(root) - 1 + ARRAY_SIZE(prefix) - 1 + wcslen(kind) + 3;
    for (i = 0; i < name_length; ++i)
    {
        if (path_length > 32766 - (name[i] == '\\' ? 3 : 1))
            return STATUS_NAME_TOO_LONG;
        path_length += name[i] == '\\' ? 3 : 1;
    }
    if (!(path = HeapAlloc(GetProcessHeap(), 0, (path_length + 1) * sizeof(WCHAR))))
        return STATUS_NO_MEMORY;
    wcscpy(path, root);
    wcscat(path, prefix);
    wcscat(path, kind);
    wcscat(path, L"%5C");
    cursor = path + wcslen(path);
    for (i = 0; i < name_length; ++i)
    {
        if (name[i] == '\\')
        {
            memcpy(cursor, L"%5C", 3 * sizeof(WCHAR));
            cursor += 3;
        }
        else *cursor++ = name[i];
    }
    *cursor = 0;
    RtlInitUnicodeString(&key_name, path);
    InitializeObjectAttributes(&attributes, &key_name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    status = NtOpenKey(&key, GENERIC_READ, &attributes);
    TRACE("Transient-object lookup name %s type %lu status %#lx.\n", debugstr_w(name), type, status);
    HeapFree(GetProcessHeap(), 0, path);
    if (status) goto done;

    RtlInitUnicodeString(&value_name, L"SecurityDescriptor");
    status = NtQueryValueKey(key, &value_name, KeyValuePartialInformation,
                            &header, sizeof(header), &bytes);
    if (status != STATUS_BUFFER_OVERFLOW && status != STATUS_BUFFER_TOO_SMALL)
    {
        /* Matching native code rejects values fitting its initial 16-byte probe. */
        if (!status) status = (NTSTATUS)0xc000090b;
        goto done;
    }
    if (bytes < FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data))
    {
        status = (NTSTATUS)0xc000090b;
        goto done;
    }
    if (!(value = HeapAlloc(GetProcessHeap(), 0, bytes)))
    {
        status = STATUS_NO_MEMORY;
        goto done;
    }
    status = NtQueryValueKey(key, &value_name, KeyValuePartialInformation, value, bytes, &bytes);
    if (status) goto done;
    if (bytes < FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) ||
        value->DataLength > bytes - FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data))
    {
        status = (NTSTATUS)0xc000090b;
        goto done;
    }
    if (value->Type == REG_BINARY)
    {
        if (!(owned = HeapAlloc(GetProcessHeap(), 0, value->DataLength)))
        {
            status = STATUS_NO_MEMORY;
            goto done;
        }
        memcpy(owned, value->Data, value->DataLength);
    }
    else if (value->Type == REG_SZ)
    {
        WCHAR *text = (WCHAR *)value->Data;
        SIZE_T characters = value->DataLength / sizeof(WCHAR);
        if (!characters || value->DataLength % sizeof(WCHAR) || text[characters - 1])
        {
            status = (NTSTATUS)0xc000090b;
            goto done;
        }
        /* Native domain-qualified strings require a separate, unreached resolver. */
        if (*text == '{')
        {
            status = STATUS_NOT_SUPPORTED;
            goto done;
        }
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                text, SDDL_REVISION_1, (PSECURITY_DESCRIPTOR *)&converted, &converted_bytes))
        {
            DWORD error = GetLastError();
            status = error == ERROR_NOT_ENOUGH_MEMORY ? STATUS_NO_MEMORY : STATUS_INVALID_PARAMETER;
            WARN("Transient descriptor conversion failed, error %lu.\n", error);
            goto done;
        }
        if (!(owned = HeapAlloc(GetProcessHeap(), 0, converted_bytes)))
        {
            status = STATUS_NO_MEMORY;
            goto done;
        }
        memcpy(owned, converted, converted_bytes);
    }
    else
    {
        status = (NTSTATUS)0xc000090b;
        goto done;
    }
    *descriptor = owned;
    owned = NULL;
    TRACE("Resolved configured transient-object descriptor for type %lu.\n", type);
done:
    if (converted) LocalFree(converted);
    FreeTransientObjectSecurityDescriptor(owned);
    if (value) HeapFree(GetProcessHeap(), 0, value);
    if (key) NtClose(key);
    if (status) WARN("Transient-object descriptor lookup failed, type %lu status %#lx.\n", type, status);
    return status;
}
