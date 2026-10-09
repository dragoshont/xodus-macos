/* Explicitly unsupported in the x64-only experimental runtime.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <stdarg.h>
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"
#include "wow64_private.h"

NTSTATUS WINAPI wow64_NtXodusQueryRegisteredPackage( UINT *args )
{
    (void)args;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS WINAPI wow64_NtXodusActivateRegisteredPackage( UINT *args )
{
    (void)args;
    return STATUS_NOT_SUPPORTED;
}
