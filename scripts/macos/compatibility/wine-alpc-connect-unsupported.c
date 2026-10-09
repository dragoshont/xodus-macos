/*
 * Explicit unsupported boundary for an absent ALPC export.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */
#define WIN32_NO_STATUS
#include <stdarg.h>
#include <windef.h>
#include <winternl.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include "wine/debug.h"

struct _ALPC_PORT_ATTRIBUTES;
struct _PORT_MESSAGE;
struct _ALPC_MESSAGE_ATTRIBUTES;

WINE_DEFAULT_DEBUG_CHANNEL(alpc);

static void trace_requirement_shape(PSECURITY_DESCRIPTOR descriptor)
{
    SECURITY_DESCRIPTOR_CONTROL control;
    ULONG revision, i;
    BOOLEAN present, defaulted;
    ACL *dacl;
    NTSTATUS status;
    if (!descriptor || !TRACE_ON(alpc)) return;
    status = RtlGetControlSecurityDescriptor(descriptor, &control, &revision);
    if (status)
    {
        WARN("Cannot query server-requirement shape, status %#lx.\n", status);
        return;
    }
    status = RtlGetDaclSecurityDescriptor(descriptor, &present, &dacl, &defaulted);
    if (status)
    {
        WARN("Cannot query server-requirement DACL shape, status %#lx.\n", status);
        return;
    }
    TRACE("Server requirement revision %lu control %#x DACL present %u null %u ACE_count %u.\n",
          revision, control, present, !dacl, present && dacl ? dacl->AceCount : 0);
    if (!present || !dacl) return;
    for (i = 0; i < dacl->AceCount; ++i)
    {
        ACE_HEADER *ace;
        status = RtlGetAce(dacl, i, (void **)&ace);
        if (status)
        {
            WARN("Cannot query server-requirement ACE shape, status %#lx.\n", status);
            return;
        }
        if (ace->AceType == ACCESS_ALLOWED_ACE_TYPE || ace->AceType == ACCESS_DENIED_ACE_TYPE)
            TRACE("Server requirement ACE %lu type %u flags %#x mask %#lx.\n",
                  i, ace->AceType, ace->AceFlags, ((ACCESS_ALLOWED_ACE *)ace)->Mask);
        else TRACE("Server requirement ACE %lu unsupported type %u flags %#x.\n",
                   i, ace->AceType, ace->AceFlags);
    }
}

static void trace_send_attributes(const void *attributes)
{
    struct { ULONG allocated, valid; } header;
    struct
    {
        ULONG flags;
        SECURITY_QUALITY_OF_SERVICE *qos;
        HANDLE context;
    } security;
    SIZE_T read;
    NTSTATUS status;
    if (!attributes || !TRACE_ON(alpc)) return;
    status = NtReadVirtualMemory((HANDLE)(LONG_PTR)-1, attributes, &header, sizeof(header), &read);
    if (status || read != sizeof(header))
    {
        WARN("Cannot read send-attribute header, status %#lx.\n", status);
        return;
    }
    TRACE("Send attributes allocated %#lx valid %#lx.\n", header.allocated, header.valid);
    if (!(header.valid & 0x80000000)) return;
    status = NtReadVirtualMemory((HANDLE)(LONG_PTR)-1,
        (const BYTE *)attributes + sizeof(header), &security, sizeof(security), &read);
    if (status || read != sizeof(security))
    {
        WARN("Cannot read security-attribute layout, status %#lx.\n", status);
        return;
    }
    TRACE("Security attribute flags %#lx qos_present %u context_present %u.\n",
          security.flags, !!security.qos, !!security.context);
    if (security.context)
    {
        union { ULONGLONG alignment; BYTE bytes[2048]; } information;
        ULONG needed;
        status = NtQueryObject(security.context, ObjectTypeInformation,
                               information.bytes, sizeof(information.bytes), &needed);
        TRACE("Security context process_token_pseudo %u thread_token_pseudo %u type_query %#lx.\n",
              security.context == (HANDLE)(LONG_PTR)-4,
              security.context == (HANDLE)(LONG_PTR)-5, status);
        if (!status)
        {
            UNICODE_STRING *name = (UNICODE_STRING *)information.bytes;
            TRACE("Security context object type %s.\n", debugstr_wn(name->Buffer, name->Length / sizeof(WCHAR)));
        }
    }
    if (security.qos)
    {
        SECURITY_QUALITY_OF_SERVICE qos;
        status = NtReadVirtualMemory((HANDLE)(LONG_PTR)-1, security.qos, &qos, sizeof(qos), &read);
        if (status || read != sizeof(qos))
            WARN("Cannot read security QoS, status %#lx.\n", status);
        else
            TRACE("Security QoS length %lu impersonation %u tracking %u effective_only %u.\n",
                  qos.Length, qos.ImpersonationLevel, qos.ContextTrackingMode, qos.EffectiveOnly);
    }
}

NTSTATUS WINAPI NtAlpcConnectPortEx(HANDLE *port, OBJECT_ATTRIBUTES *connection,
    OBJECT_ATTRIBUTES *client, struct _ALPC_PORT_ATTRIBUTES *port_attributes,
    ULONG flags, PSECURITY_DESCRIPTOR security_requirements,
    struct _PORT_MESSAGE *message, SIZE_T *message_size,
    struct _ALPC_MESSAGE_ATTRIBUTES *send_attributes,
    struct _ALPC_MESSAGE_ATTRIBUTES *receive_attributes, LARGE_INTEGER *timeout)
{
    trace_requirement_shape(security_requirements);
    trace_send_attributes(send_attributes);
    FIXME("Unsupported ALPC connection: %p %p %p %p %#lx %p %p %p %p %p %p.\n",
          port, connection, client, port_attributes, flags, security_requirements,
          message, message_size, send_attributes, receive_attributes, timeout);
    return STATUS_NOT_IMPLEMENTED;
}
