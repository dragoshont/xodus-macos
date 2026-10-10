/* Reads configured COM policy only. No default permissions are invented when
 * the resolver-backed defaults are unavailable in this runtime. */
#include <windows.h>
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(ole);

static BOOL valid_policy_descriptor( const SECURITY_DESCRIPTOR_RELATIVE *descriptor, DWORD bytes )
{
    const DWORD offsets[] = {descriptor->Owner, descriptor->Group, descriptor->Sacl, descriptor->Dacl};
    unsigned int i;

    if (bytes < sizeof(*descriptor) || descriptor->Revision != SECURITY_DESCRIPTOR_REVISION ||
        !(descriptor->Control & SE_SELF_RELATIVE)) return FALSE;
    for (i = 0; i < ARRAY_SIZE(offsets); i++)
    {
        const BYTE *field;
        DWORD available;
        if (!offsets[i]) continue;
        if (offsets[i] < sizeof(*descriptor) || offsets[i] > bytes || offsets[i] % sizeof(DWORD))
            return FALSE;
        available = bytes - offsets[i];
        field = (const BYTE *)descriptor + offsets[i];
        if (i < 2)
        {
            const SID *sid = (const SID *)field;
            if (available < FIELD_OFFSET(SID, SubAuthority) ||
                sid->SubAuthorityCount > SID_MAX_SUB_AUTHORITIES ||
                sid->SubAuthorityCount > (available - FIELD_OFFSET(SID, SubAuthority)) / sizeof(DWORD) ||
                !IsValidSid( (SID *)sid )) return FALSE;
        }
        else
        {
            const ACL *acl = (const ACL *)field;
            if (available < sizeof(*acl) || acl->AclSize < sizeof(*acl) || acl->AclSize > available ||
                !IsValidAcl( (ACL *)acl )) return FALSE;
        }
    }
    return TRUE;
}

HRESULT WINAPI CoGetSystemSecurityPermissions( int kind, PSECURITY_DESCRIPTOR *output )
{
    static const WCHAR *const names[] = {
        L"DefaultLaunchPermission", L"DefaultAccessPermission",
        L"MachineLaunchRestriction", L"MachineAccessRestriction",
    };
    PSECURITY_DESCRIPTOR descriptor;
    HKEY key;
    DWORD bytes = 0, type;
    LONG status;

    TRACE( "(%d %p)\n", kind, output );
    if (!output || (unsigned int)kind >= ARRAY_SIZE(names)) return E_INVALIDARG;
    status = RegOpenKeyExW( HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Ole", 0, KEY_READ, &key );
    if (status)
    {
        WARN( "COM policy key unavailable, error %ld; no resolver default is implemented\n", status );
        return E_FAIL;
    }
    status = RegQueryValueExW( key, names[kind], NULL, &type, NULL, &bytes );
    if (status || type != REG_BINARY || bytes < sizeof(SECURITY_DESCRIPTOR_RELATIVE))
    {
        RegCloseKey( key );
        WARN( "COM policy %s unavailable or malformed, error %ld; refusing invented permissions\n",
              debugstr_w(names[kind]), status );
        return E_FAIL;
    }
    if (!(descriptor = LocalAlloc( LMEM_FIXED, bytes )))
    {
        RegCloseKey( key );
        return E_OUTOFMEMORY;
    }
    status = RegQueryValueExW( key, names[kind], NULL, &type, descriptor, &bytes );
    RegCloseKey( key );
    if (status || type != REG_BINARY || !valid_policy_descriptor( descriptor, bytes ))
    {
        LocalFree( descriptor );
        WARN( "COM policy %s is invalid, error %ld\n", debugstr_w(names[kind]), status );
        return E_FAIL;
    }
    *output = descriptor;
    return S_OK;
}
