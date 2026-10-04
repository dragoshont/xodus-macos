/* SPDX-License-Identifier: GPL-3.0-only */
/* Deliberately incomplete DLL for loader refusal tests, never a runtime candidate. */
#include "windows_compat.h"

extern "C" __declspec(dllexport) HRESULT WINAPI XodusAsyncValidateAbi(
    UINT32, SIZE_T, SIZE_T, SIZE_T, SIZE_T, SIZE_T)
{
#ifdef XODUS_REJECT_ABI
    return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
#else
    return S_OK;
#endif
}
