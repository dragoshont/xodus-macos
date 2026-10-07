#!/usr/bin/env python3
"""ntdll RtlGetAppContainerNamedObjectPath, implemented against measured native
Windows 11 26100 behaviour (audit-acnop.exe); absent from Wine master,
wine-staging 2395d933, Proton experimental_10.0 and ReactOS.  Reached by the
genuine CoreMessaging.dll with (NULL, NULL, FALSE, &path)."""
import os, shutil, sys

root = sys.argv[1] if len(sys.argv) > 1 else "."

def edit(rel, old, new):
    p = os.path.join(root, rel); t = open(p).read()
    if new in t: return
    if old not in t: sys.exit("anchor missing " + rel)
    if not os.path.exists(p + ".pre-acnop"): shutil.copy2(p, p + ".pre-acnop")
    open(p, "w").write(t.replace(old, new, 1)); print("patched", rel)

edit("dlls/ntdll/ntdll.spec", "@ stdcall RtlGetAce(ptr long ptr)\n",
     "@ stdcall RtlGetAce(ptr long ptr)\n@ stdcall RtlGetAppContainerNamedObjectPath(ptr ptr long ptr)\n")

p = os.path.join(root, "dlls/ntdll/sec.c"); t = open(p).read()
if "RtlGetAppContainerNamedObjectPath" not in t:
    if not os.path.exists(p + ".pre-acnop"): shutil.copy2(p, p + ".pre-acnop")
    t += r'''
/******************************************************************************
 * RtlGetAppContainerNamedObjectPath   [NTDLL.@]
 *
 * xodus-acnop: measured native behaviour.  Without an AppContainer SID the
 * effective (or given) token is queried; a token that is not an AppContainer
 * yields success with an empty string.  An explicit SID must be an AppContainer
 * (8 RIDs) or child AppContainer (12 RIDs) package SID.  The result is
 * "[\Sessions\<id>\]AppContainerNamedObjects\<parent SID>[\<child RIDs>]",
 * allocated from the process heap.
 */
NTSTATUS WINAPI RtlGetAppContainerNamedObjectPath( HANDLE token, PSID sid, BOOLEAN relative, UNICODE_STRING *path )
{
    static const SID_IDENTIFIER_AUTHORITY package_authority = { SECURITY_APP_PACKAGE_AUTHORITY };
    BYTE container_buf[sizeof(TOKEN_APPCONTAINER_INFORMATION) + SECURITY_MAX_SID_SIZE];
    BYTE parent_buf[SECURITY_MAX_SID_SIZE];
    WCHAR prefix[40], child[48];
    UNICODE_STRING parent;
    SID *ac, *parent_sid = (SID *)parent_buf;
    NTSTATUS status;
    ULONG len;

    TRACE( "%p %p %d %p\n", token, sid, relative, path );

    if (!path) return STATUS_INVALID_PARAMETER;
    if (token && sid) return STATUS_INVALID_PARAMETER_MIX;

    if (!sid)
    {
        HANDLE query = token;
        TOKEN_TYPE type;
        DWORD is_container = 0;

        if (!query)
        {
            status = NtOpenThreadTokenEx( NtCurrentThread(), TOKEN_QUERY, TRUE, 0, &query );
            if (status == STATUS_NO_TOKEN) status = NtOpenProcessTokenEx( NtCurrentProcess(), TOKEN_QUERY, 0, &query );
            if (status) return status;
        }
        if (!(status = NtQueryInformationToken( query, TokenType, &type, sizeof(type), &len ))
            && !(status = NtQueryInformationToken( query, TokenIsAppContainer, &is_container, sizeof(is_container), &len ))
            && is_container)
            status = NtQueryInformationToken( query, TokenAppContainerSid, container_buf, sizeof(container_buf), &len );
        if (query != token) NtClose( query );
        if (status) return status;

        path->Length = path->MaximumLength = 0;
        path->Buffer = NULL;
        if (!is_container) return STATUS_SUCCESS;
        sid = ((TOKEN_APPCONTAINER_INFORMATION *)container_buf)->TokenAppContainer;
    }
    else
    {
        path->Length = path->MaximumLength = 0;
        path->Buffer = NULL;
    }

    ac = sid;
    if (!ac || !RtlValidSid( ac ) || memcmp( &ac->IdentifierAuthority, &package_authority, sizeof(package_authority) )
        || ac->SubAuthority[0] != SECURITY_APP_PACKAGE_BASE_RID
        || (ac->SubAuthorityCount != SECURITY_APP_PACKAGE_RID_COUNT
            && ac->SubAuthorityCount != SECURITY_CHILD_PACKAGE_RID_COUNT))
        return STATUS_NOT_APPCONTAINER;

    memcpy( parent_sid, ac, RtlLengthRequiredSid( SECURITY_APP_PACKAGE_RID_COUNT ) );
    parent_sid->SubAuthorityCount = SECURITY_APP_PACKAGE_RID_COUNT;
    if ((status = RtlConvertSidToUnicodeString( &parent, parent_sid, TRUE ))) return status;

    child[0] = 0;
    if (ac->SubAuthorityCount == SECURITY_CHILD_PACKAGE_RID_COUNT)
        _snwprintf( child, ARRAY_SIZE(child), L"\\%u-%u-%u-%u", ac->SubAuthority[8], ac->SubAuthority[9],
                    ac->SubAuthority[10], ac->SubAuthority[11] );
    prefix[0] = 0;
    if (!relative) _snwprintf( prefix, ARRAY_SIZE(prefix), L"\\Sessions\\%u\\", NtCurrentTeb()->Peb->SessionId );

    len = (wcslen( prefix ) + wcslen( L"AppContainerNamedObjects\\" ) + wcslen( child )) * sizeof(WCHAR) + parent.Length;
    if (!(path->Buffer = RtlAllocateHeap( GetProcessHeap(), 0, len + sizeof(WCHAR) )))
    {
        RtlFreeUnicodeString( &parent );
        return STATUS_NO_MEMORY;
    }
    path->Length = len;
    path->MaximumLength = len + sizeof(WCHAR);
    path->Buffer[0] = 0;
    wcscat( path->Buffer, prefix );
    wcscat( path->Buffer, L"AppContainerNamedObjects\\" );
    memcpy( path->Buffer + wcslen( path->Buffer ), parent.Buffer, parent.Length );
    path->Buffer[(len - wcslen( child ) * sizeof(WCHAR)) / sizeof(WCHAR)] = 0;
    wcscat( path->Buffer, child );
    RtlFreeUnicodeString( &parent );
    return STATUS_SUCCESS;
}
'''
    open(p, "w").write(t); print("patched dlls/ntdll/sec.c")
