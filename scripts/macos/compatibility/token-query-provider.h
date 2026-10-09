#ifndef XODUS_TOKEN_QUERY_PROVIDER_H
#define XODUS_TOKEN_QUERY_PROVIDER_H

#include "package-claims.h"

typedef NTSTATUS (NTAPI *XPC_QUERY_INFORMATION)(
    HANDLE, TOKEN_INFORMATION_CLASS, void *, ULONG, ULONG *);

XPC_EXPORT NTSTATUS NTAPI NtQuerySecurityAttributesToken(
    HANDLE token, UNICODE_STRING *attributes, ULONG count,
    void *buffer, ULONG bytes, ULONG *required);

#ifdef XPC_TEST
NTSTATUS XpcQueryAttributesFromInformation(
    XPC_QUERY_INFORMATION provider, HANDLE token,
    UNICODE_STRING *attributes, ULONG count, void *buffer,
    ULONG bytes, ULONG *required);
#endif

#endif
