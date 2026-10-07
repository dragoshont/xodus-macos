#!/usr/bin/env python3
# Adds RoGetDesignMode (#90) / RoGetDesignModeV2 (#157) to Wine combase,
# following the native Windows 11 26100 combase implementation.
import os, sys
R = sys.argv[1] if len(sys.argv) > 1 else '.'
MARK = 'xodus-designmode'

def patch(path, old, new):
    path = os.path.join(R, path)
    src = open(path).read()
    if MARK in src:
        print('already patched', path); return
    if not os.path.exists(path + '.pre-designmode'):
        open(path + '.pre-designmode', 'w').write(src)
    if src.count(old) != 1:
        sys.exit('anchor not unique in %s' % path)
    open(path, 'w').write(src.replace(old, new))
    print('patched', path)

CODE = r'''
/* xodus-designmode: native combase derives the XAML design-mode state from the
 * WIN://DESIGN_MODE[_V2] token security attributes (process token, then the
 * thread token while impersonating). */
static LONG design_mode_state; /* 0 unknown, 1 design mode, 2 design mode v2, 3 none */

static HRESULT token_has_security_attribute(HANDLE token, const WCHAR *name, BOOL *present)
{
    struct { USHORT version, reserved; ULONG count; } *info;
    UNICODE_STRING str;
    ULONG len = 0;
    NTSTATUS status;
    HRESULT hr = S_OK;

    *present = FALSE;
    RtlInitUnicodeString(&str, name);
    status = NtQuerySecurityAttributesToken(token, &str, 1, NULL, 0, &len);
    if (status == STATUS_NOT_FOUND) return S_OK;
    if (!(info = HeapAlloc(GetProcessHeap(), 0, len))) return E_OUTOFMEMORY;
    status = NtQuerySecurityAttributesToken(token, &str, 1, info, len, &len);
    if (status) hr = HRESULT_FROM_WIN32(RtlNtStatusToDosError(status));
    else *present = info->count != 0;
    HeapFree(GetProcessHeap(), 0, info);
    return hr;
}

static HRESULT get_design_mode_state(LONG *state)
{
    BOOL present;
    HRESULT hr;

    if ((*state = ReadNoFence(&design_mode_state))) return S_OK;
    if (FAILED(hr = token_has_security_attribute(GetCurrentProcessToken(), L"WIN://DESIGN_MODE_V2", &present)))
        return hr;
    if (present) *state = 2;
    else
    {
        if (FAILED(hr = token_has_security_attribute(GetCurrentProcessToken(), L"WIN://DESIGN_MODE", &present)))
            return hr;
        *state = present ? 1 : 3;
    }
    WriteNoFence(&design_mode_state, *state);
    return S_OK;
}

/***********************************************************************
 *      RoGetDesignMode (combase.90)
 */
HRESULT WINAPI RoGetDesignMode(BOOL *design_mode)
{
    BOOL present = FALSE;
    LONG state;
    HRESULT hr;

    TRACE("%p.\n", design_mode);

    if (!design_mode) return E_INVALIDARG;
    *design_mode = FALSE;
    if (FAILED(hr = get_design_mode_state(&state))) return hr;
    if (state == 1 || state == 2) present = TRUE;
    else if (NtCurrentTeb()->IsImpersonating)
    {
        if (FAILED(hr = token_has_security_attribute(GetCurrentThreadToken(), L"WIN://DESIGN_MODE_V2", &present)))
            return hr;
        if (!present && FAILED(hr = token_has_security_attribute(GetCurrentThreadToken(), L"WIN://DESIGN_MODE", &present)))
            return hr;
    }
    *design_mode = present;
    return S_OK;
}

/***********************************************************************
 *      RoGetDesignModeV2 (combase.157)
 */
HRESULT WINAPI RoGetDesignModeV2(BOOL *design_mode)
{
    BOOL present = FALSE;
    LONG state;
    HRESULT hr;

    TRACE("%p.\n", design_mode);

    if (!design_mode) return E_INVALIDARG;
    *design_mode = FALSE;
    if (FAILED(hr = get_design_mode_state(&state))) return hr;
    if (state == 2) present = TRUE;
    else if (NtCurrentTeb()->IsImpersonating &&
             FAILED(hr = token_has_security_attribute(GetCurrentThreadToken(), L"WIN://DESIGN_MODE_V2", &present)))
        return hr;
    *design_mode = present;
    return S_OK;
}

/***********************************************************************
 *      RoGetApartmentIdentifier (combase.@)'''

patch('dlls/combase/roapi.c',
      '\n/***********************************************************************\n *      RoGetApartmentIdentifier (combase.@)',
      CODE)
patch('dlls/combase/combase.spec',
      '\n550 stdcall RoGetApartmentIdentifier(ptr)',
      '\n# xodus-designmode: native NONAME ordinals\n90 stdcall -noname RoGetDesignMode(ptr)\n157 stdcall -noname RoGetDesignModeV2(ptr)\n550 stdcall RoGetApartmentIdentifier(ptr)')
