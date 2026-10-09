/*
 * Explicit local-catalog activation transport; no identity supplied by client.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#if 0
#pragma makedep unix
#endif
#include <stdarg.h>
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"
#include "unix_private.h"

static int registration_strings( const UNICODE_STRING *package, const UNICODE_STRING *app )
{
    return package && app && package->Buffer && app->Buffer && package->Length && app->Length &&
           !(package->Length & 1) && !(app->Length & 1) &&
           package->Length <= 2048 && app->Length <= 2048;
}

NTSTATUS WINAPI NtXodusQueryRegisteredPackage( const UNICODE_STRING *package,
                                              const UNICODE_STRING *app,
                                              void *image, ULONG *bytes )
{
    NTSTATUS status;
    ULONG required;
    if (!registration_strings( package, app ) || !bytes || (*bytes && !image))
        return STATUS_INVALID_PARAMETER;
    SERVER_START_REQ( query_registered_package )
    {
        req->package_bytes = package->Length;
        wine_server_add_data( req, package->Buffer, package->Length );
        wine_server_add_data( req, app->Buffer, app->Length );
        if (*bytes) wine_server_set_reply( req, image, *bytes );
        status = wine_server_call( req );
        required = reply->image_bytes;
    }
    SERVER_END_REQ;
    if (!status || status == STATUS_BUFFER_TOO_SMALL) *bytes = required;
    return status;
}

NTSTATUS WINAPI NtXodusActivateRegisteredPackage( HANDLE process,
                                                 const UNICODE_STRING *package,
                                                 const UNICODE_STRING *app )
{
    NTSTATUS status;
    if (!registration_strings( package, app )) return STATUS_INVALID_PARAMETER;
    SERVER_START_REQ( activate_registered_package )
    {
        req->process = wine_server_obj_handle( process );
        req->package_bytes = package->Length;
        wine_server_add_data( req, package->Buffer, package->Length );
        wine_server_add_data( req, app->Buffer, app->Length );
        status = wine_server_call( req );
    }
    SERVER_END_REQ;
    return status;
}
