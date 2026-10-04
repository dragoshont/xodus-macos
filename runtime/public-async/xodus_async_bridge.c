/* SPDX-License-Identifier: GPL-3.0-only */
#include "private.h"
#include <stddef.h>
#include <string.h>
#include "xodus_async_bridge.h"

WINE_DEFAULT_DEBUG_CHANNEL(xgameruntime);

static INIT_ONCE initialized = INIT_ONCE_STATIC_INIT;
static HMODULE core;
static HRESULT initialization_result = E_PENDING;
static const WCHAR module_anchor;

static BOOL CALLBACK initialize_core(INIT_ONCE *once, void *parameter, void **context)
{
    typedef HRESULT (WINAPI *validate_abi)(UINT32, SIZE_T, SIZE_T, SIZE_T, SIZE_T, SIZE_T);
    static const char *const required[] = {
        "XAsyncGetStatus", "XAsyncGetResultSize", "XAsyncCancel", "XAsyncRun",
        "XAsyncBegin", "XAsyncSchedule", "XAsyncComplete", "XAsyncGetResult",
        "XTaskQueueCreate", "XTaskQueueCreateComposite", "XTaskQueueGetPort",
        "XTaskQueueDuplicateHandle", "XTaskQueueDispatch", "XTaskQueueCloseHandle",
        "XTaskQueueSubmitCallback", "XTaskQueueSubmitDelayedCallback",
        "XTaskQueueRegisterWaiter", "XTaskQueueUnregisterWaiter",
        "XTaskQueueTerminate", "XTaskQueueRegisterMonitor", "XTaskQueueUnregisterMonitor",
        "XTaskQueueGetCurrentProcessTaskQueue", "XTaskQueueSetCurrentProcessTaskQueue",
    };
    HMODULE module = NULL, loaded = NULL;
    WCHAR filename[32768], *last;
    validate_abi validate;
    _Static_assert(sizeof(validate) == sizeof(FARPROC), "Unexpected Windows function representation");
    FARPROC procedure;
    DWORD length;
    SIZE_T i;

    (void)once;
    (void)parameter;
    (void)context;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           &module_anchor, &module))
        goto windows_error;
    length = GetModuleFileNameW(module, filename, ARRAY_SIZE(filename));
    if (!length || length >= ARRAY_SIZE(filename))
    {
        initialization_result = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        goto failed;
    }
    if (!(last = wcsrchr(filename, '\\')) ||
        (SIZE_T)(last - filename + 1) + ARRAY_SIZE(L"xodus_async.dll") > ARRAY_SIZE(filename))
    {
        initialization_result = HRESULT_FROM_WIN32(ERROR_BAD_CONFIGURATION);
        goto failed;
    }
    wcscpy(last + 1, L"xodus_async.dll");
    loaded = LoadLibraryExW(filename, NULL,
                           LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!loaded) goto windows_error;
    procedure = GetProcAddress(loaded, "XodusAsyncValidateAbi");
    if (!procedure) goto windows_error;
    memcpy(&validate, &procedure, sizeof(validate));
    initialization_result = validate(1, sizeof(XAsyncBlock), sizeof(XAsyncProviderData),
        offsetof(XAsyncProviderData, bufferSize), offsetof(XAsyncProviderData, buffer),
        offsetof(XAsyncProviderData, context));
    if (FAILED(initialization_result)) goto failed;
    for (i = 0; i < ARRAY_SIZE(required); ++i)
    {
        if (!GetProcAddress(loaded, required[i])) goto windows_error;
    }
    core = loaded;
    return TRUE;

windows_error:
    initialization_result = HRESULT_FROM_WIN32(GetLastError());
    if (SUCCEEDED(initialization_result)) initialization_result = E_UNEXPECTED;
failed:
    if (loaded) FreeLibrary(loaded);
    ERR("The explicit sibling async core could not initialize: HRESULT %#lx.\n",
        initialization_result);
    return TRUE;
}

HRESULT xodus_async_get_proc(const char *name, FARPROC *procedure)
{
    *procedure = NULL;
    if (!InitOnceExecuteOnce(&initialized, initialize_core, NULL, NULL))
    {
        HRESULT result = HRESULT_FROM_WIN32(GetLastError());
        ERR("Async core initialization synchronization failed: HRESULT %#lx.\n", result);
        return FAILED(result) ? result : E_UNEXPECTED;
    }
    if (FAILED(initialization_result)) return initialization_result;
    if (!(*procedure = GetProcAddress(core, name)))
    {
        HRESULT result = HRESULT_FROM_WIN32(GetLastError());
        ERR("A validated async core entry point became unavailable: HRESULT %#lx.\n", result);
        return FAILED(result) ? result : E_UNEXPECTED;
    }
    return S_OK;
}
