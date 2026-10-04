/* SPDX-License-Identifier: GPL-3.0-only */
#include "windows_compat.h"
#ifdef XODUS_SHIM_CHECK
#include "shim_check_api.h"
#include "xuser.h"
#else
#include <XAsyncProvider.h>
#endif
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>

static decltype(&XAsyncBegin) begin_async;
static decltype(&XAsyncSchedule) schedule_async;
static decltype(&XAsyncComplete) complete_async;
static decltype(&XAsyncGetStatus) get_status;
static decltype(&XAsyncGetResult) get_result;
static decltype(&XAsyncCancel) cancel_async;
static decltype(&XTaskQueueCreate) create_queue;
static decltype(&XTaskQueueDispatch) dispatch_queue;
static decltype(&XTaskQueueTerminate) terminate_queue;
static decltype(&XTaskQueueCloseHandle) close_queue;

struct Context
{
    std::atomic<unsigned> work{0}, cleanup{0}, cancel{0}, callback{0}, terminated{0};
    uint32_t delay{0};
};

template<typename T> static bool load(HMODULE module, const char *name, T &target)
{
    FARPROC procedure = GetProcAddress(module, name);
    static_assert(sizeof(procedure) == sizeof(target), "Unexpected Windows function representation");
    if (!procedure)
    {
        std::fprintf(stderr, "The owned async DLL does not export %s.\n", name);
        return false;
    }
    std::memcpy(&target, &procedure, sizeof(target));
    return true;
}

static HRESULT CALLBACK provider(XAsyncOp operation, const XAsyncProviderData *data)
{
    auto *context = static_cast<Context *>(data->context);
    switch (operation)
    {
    case XAsyncOp::Begin:
        return schedule_async(data->async, context->delay);
    case XAsyncOp::DoWork:
        ++context->work;
        complete_async(data->async, S_OK, sizeof(uint32_t));
        return S_OK;
    case XAsyncOp::GetResult:
        if (data->bufferSize != sizeof(uint32_t)) return E_UNEXPECTED;
        *static_cast<uint32_t *>(data->buffer) = 0x58445358;
        return S_OK;
    case XAsyncOp::Cancel:
        ++context->cancel;
        complete_async(data->async, E_ABORT, 0);
        return S_OK;
    case XAsyncOp::Cleanup:
        ++context->cleanup;
        return S_OK;
    }
    return E_UNEXPECTED;
}

static void CALLBACK completion(XAsyncBlock *block)
{
    ++static_cast<Context *>(block->context)->callback;
}

static void CALLBACK termination(void *value)
{
    ++static_cast<Context *>(value)->terminated;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !std::strcmp(argv[1], "--bootstrap"))
    {
        std::puts("Isolated Windows check started; no credentials requested.");
        return 0;
    }
    if (argc != 2 || (std::strcmp(argv[1], "manual") &&
                      std::strcmp(argv[1], "threadpool") &&
                      std::strcmp(argv[1], "delayed") &&
                      std::strcmp(argv[1], "cancel")
#ifdef XODUS_SHIM_CHECK
                      && std::strcmp(argv[1], "rps-malformed")
                      && std::strcmp(argv[1], "rps-expired")
                      && std::strcmp(argv[1], "rps-missing")
                      && std::strcmp(argv[1], "core-missing")
                      && std::strcmp(argv[1], "core-bad-abi")
                      && std::strcmp(argv[1], "core-missing-export")
#endif
                      ))
    {
        std::fputs("Usage: xodus-async-smoke.exe manual|threadpool|delayed|cancel\n", stderr);
        return 2;
    }
    const bool threaded = !std::strcmp(argv[1], "threadpool");
    const bool cancelled = !std::strcmp(argv[1], "cancel");
    const bool user_operation = !std::strncmp(argv[1], "rps-", 4);
#ifdef XODUS_SHIM_CHECK
    IXUserImpl6 *user = nullptr;
#endif
    Context context;
    context.delay = cancelled ? 60000 : (!std::strcmp(argv[1], "delayed") ? 100 : 0);
    WCHAR filename[32768];
    DWORD length = GetModuleFileNameW(nullptr, filename, ARRAYSIZE(filename));
    if (!length || length >= ARRAYSIZE(filename))
    {
        std::fputs("Cannot resolve the owned Windows async check path.\n", stderr);
        return 1;
    }
    WCHAR *last = std::wcsrchr(filename, L'\\');
    if (!last || static_cast<size_t>(last - filename + 1) + 16 >= ARRAYSIZE(filename))
    {
        std::fputs("The owned async check has no bounded parent directory.\n", stderr);
        return 1;
    }
    std::wcscpy(last + 1,
#ifdef XODUS_SHIM_CHECK
        L"xgameruntime.dll"
#else
        L"xodus_async.dll"
#endif
    );
    HMODULE module = LoadLibraryExW(filename, nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module)
    {
        std::fprintf(stderr, "Cannot load the explicit sibling async DLL: Windows error %lu.\n",
                     static_cast<unsigned long>(GetLastError()));
        return 1;
    }
    /* Queue termination callbacks do not synchronize worker return. Retain loaded modules until process exit. */
#ifdef XODUS_SHIM_CHECK
    using QueryApi = HRESULT (WINAPI *)(REFCLSID, REFIID, void **);
    QueryApi query;
    if (!load(module, "QueryApiImpl", query) ||
        FAILED(query(CLSID_XThreadingImpl, IID_IXThreadingImpl,
                     reinterpret_cast<void **>(&threading))) || !threading)
    {
        std::fputs("Cannot obtain the real public gaming threading interface.\n", stderr);
        return 1;
    }
    begin_async = XAsyncBegin;
    schedule_async = XAsyncSchedule;
    complete_async = XAsyncComplete;
    get_status = XAsyncGetStatus;
    get_result = XAsyncGetResult;
    cancel_async = XAsyncCancel;
    create_queue = XTaskQueueCreate;
    dispatch_queue = XTaskQueueDispatch;
    terminate_queue = XTaskQueueTerminate;
    close_queue = XTaskQueueCloseHandle;
    if (IXThreadingImpl_XThreadIsTimeSensitive(threading) ||
        FAILED(IXThreadingImpl_XThreadSetTimeSensitive(threading, TRUE)) ||
        !IXThreadingImpl_XThreadIsTimeSensitive(threading) ||
        FAILED(IXThreadingImpl_XThreadSetTimeSensitive(threading, FALSE)) ||
        IXThreadingImpl_XThreadIsTimeSensitive(threading))
    {
        std::fputs("Public gaming time-sensitive thread state was not preserved.\n", stderr);
        IXThreadingImpl_Release(threading);
        return 1;
    }
    IXThreadingImpl_XThreadAssertNotTimeSensitive(threading);
    if (!std::strncmp(argv[1], "core-", 5))
    {
        const DWORD error = !std::strcmp(argv[1], "core-missing") ? ERROR_MOD_NOT_FOUND
            : (!std::strcmp(argv[1], "core-bad-abi") ? ERROR_REVISION_MISMATCH
               : ERROR_PROC_NOT_FOUND);
        XAsyncBlock empty{};
        bool rejected = true;
        for (unsigned attempt = 0; attempt < 2; ++attempt)
        {
            XTaskQueueHandle absent = nullptr;
            HRESULT result = create_queue(XTaskQueueDispatchMode::Manual,
                                          XTaskQueueDispatchMode::Manual, &absent);
            if (result != HRESULT_FROM_WIN32(error) || absent ||
                get_status(&empty, FALSE) != result)
            {
                std::fputs("The explicit gaming core refusal was not stable and exact.\n", stderr);
                rejected = false;
                break;
            }
        }
        IXThreadingImpl_Release(threading);
        if (GetModuleHandleW(filename) != module) rejected = false;
        if (rejected) std::printf("Isolated Windows async %s outcome passed.\n", argv[1]);
        return rejected ? 0 : 1;
    }
    if (user_operation)
    {
        struct InitializeOptions { UINT32 reserved; BOOL isInline; const char *config; };
        using Initialize = HRESULT (WINAPI *)(ULONG, ULONG, char, const InitializeOptions *);
        Initialize initialize;
        char config[32768];
        DWORD config_length = GetModuleFileNameA(nullptr, config, ARRAYSIZE(config));
        char *separator = config_length && config_length < ARRAYSIZE(config)
            ? std::strrchr(config, '\\') : nullptr;
        if (!separator || static_cast<size_t>(separator - config + 1) +
            sizeof("MicrosoftGame.config") > sizeof(config))
        {
            std::fputs("Cannot resolve the explicit owned synthetic game configuration.\n", stderr);
            return 1;
        }
        std::strcpy(separator + 1, "MicrosoftGame.config");
        InitializeOptions options{0, FALSE, config};
        if (!load(module, "InitializeApiImplEx2", initialize) ||
            FAILED(initialize(0, 0, 0, &options)) ||
            FAILED(query(CLSID_XUserImpl, IID_IXUserImpl6,
                         reinterpret_cast<void **>(&user))) || !user)
        {
            std::fputs("Cannot initialize the explicit synthetic public user interface.\n", stderr);
            return 1;
        }
    }
#else
    if (!load(module, "XAsyncBegin", begin_async) ||
        !load(module, "XAsyncSchedule", schedule_async) ||
        !load(module, "XAsyncComplete", complete_async) ||
        !load(module, "XAsyncGetStatus", get_status) ||
        !load(module, "XAsyncGetResult", get_result) ||
        !load(module, "XAsyncCancel", cancel_async) ||
        !load(module, "XTaskQueueCreate", create_queue) ||
        !load(module, "XTaskQueueDispatch", dispatch_queue) ||
        !load(module, "XTaskQueueTerminate", terminate_queue) ||
        !load(module, "XTaskQueueCloseHandle", close_queue))
    {
        return 1;
    }
    using Validate = HRESULT (WINAPI *)(UINT32, SIZE_T, SIZE_T, SIZE_T, SIZE_T, SIZE_T);
    Validate validate;
    if (!load(module, "XodusAsyncValidateAbi", validate) ||
        validate(1, sizeof(XAsyncBlock), sizeof(XAsyncProviderData),
                 offsetof(XAsyncProviderData, bufferSize),
                 offsetof(XAsyncProviderData, buffer),
                 offsetof(XAsyncProviderData, context)) != S_OK)
    {
        std::fputs("Async ABI validation accepted mismatched layouts or rejected its exact ABI.\n", stderr);
        return 1;
    }
    for (unsigned field = 0; field < 6; ++field)
    {
        SIZE_T values[] = {1, sizeof(XAsyncBlock), sizeof(XAsyncProviderData),
                          offsetof(XAsyncProviderData, bufferSize),
                          offsetof(XAsyncProviderData, buffer),
                          offsetof(XAsyncProviderData, context)};
        ++values[field];
        if (validate(static_cast<UINT32>(values[0]), values[1], values[2], values[3],
                     values[4], values[5]) != HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH))
        {
            std::fputs("Async ABI validation did not reject an exact mismatched field.\n", stderr);
            return 1;
        }
    }
#endif
    XTaskQueueHandle queue = nullptr;
    HRESULT result = create_queue(
        threaded ? XTaskQueueDispatchMode::ThreadPool : XTaskQueueDispatchMode::Manual,
        XTaskQueueDispatchMode::Manual, &queue);
    if (FAILED(result) || !queue)
    {
        std::fprintf(stderr, "Cannot create the owned task queue: HRESULT %#lx.\n",
                     static_cast<unsigned long>(result));
        return 1;
    }
    XAsyncBlock block{};
    block.queue = queue;
    block.context = &context;
    block.callback = completion;
    const char identity[] = "owned async fixture";
    const ULONGLONG started = GetTickCount64();
#ifdef XODUS_SHIM_CHECK
    result = user_operation
        ? IXUserImpl6_XUserAddAsync(user, XUserAddOptions::AddDefaultUserSilently, &block)
        : begin_async(&block, &context, identity, identity, provider);
#else
    result = begin_async(&block, &context, identity, identity, provider);
#endif
    bool started_operation = SUCCEEDED(result);
    if (started_operation && cancelled) cancel_async(&block);
    while (started_operation && GetTickCount64() - started < 5000)
    {
        if (!threaded) dispatch_queue(queue, XTaskQueuePort::Work, 10);
        dispatch_queue(queue, XTaskQueuePort::Completion, 10);
        result = get_status(&block, false);
        if (result != E_PENDING && context.callback.load() == 1) break;
        Sleep(1);
    }
    int exit_code = 1;
    uint32_t value = 0;
    size_t used = 0;
    if (user_operation)
    {
#ifdef XODUS_SHIM_CHECK
        XUserHandle handle = nullptr;
        const HRESULT expected = !std::strcmp(argv[1], "rps-missing")
            ? HRESULT_FROM_WIN32(ERROR_BAD_CONFIGURATION) : E_UNEXPECTED;
        if (started_operation && result == expected && context.callback.load() == 1 &&
            IXUserImpl6_XUserAddResult(user, &block, &handle) == expected && !handle)
            exit_code = 0;
        if (handle) IXUserImpl6_XUserCloseHandle(user, handle);
#endif
    }
    else if (started_operation && result == (cancelled ? E_ABORT : S_OK))
    {
        HRESULT returned = get_result(&block, identity, sizeof(value), &value, &used);
        if (cancelled)
        {
            if (returned == E_ABORT && context.cancel.load() == 1 &&
                context.work.load() == 0 && context.callback.load() == 1)
                exit_code = 0;
        }
        else if (returned == S_OK && value == 0x58445358 &&
                 used == sizeof(value) && context.work.load() == 1 &&
                 context.callback.load() == 1 &&
                 (context.delay == 0 || GetTickCount64() - started >= context.delay))
            exit_code = 0;
    }
    if (exit_code)
    {
        std::fprintf(stderr, "Owned async operation failed its exact %s outcome: HRESULT %#lx.\n",
                     argv[1], static_cast<unsigned long>(result));
        if (started_operation) cancel_async(&block);
    }
    HRESULT terminated = terminate_queue(queue, false, &context, termination);
    const ULONGLONG stop_started = GetTickCount64();
    while (GetTickCount64() - stop_started < 5000)
    {
        bool progress = dispatch_queue(queue, XTaskQueuePort::Work, 10);
        progress |= dispatch_queue(queue, XTaskQueuePort::Completion, 10);
        if (!progress && context.terminated.load() == 1 &&
            (user_operation || !started_operation || context.cleanup.load() == 1)) break;
        Sleep(1);
    }
    if (FAILED(terminated) || context.terminated.load() != 1 ||
        (!user_operation && started_operation && context.cleanup.load() != 1))
    {
        std::fputs("The owned async provider/queue did not reconcile cleanup exactly once.\n", stderr);
        return 1;
    }
    close_queue(queue);
#ifdef XODUS_SHIM_CHECK
    if (user) IXUserImpl6_Release(user);
    IXThreadingImpl_Release(threading);
#endif
    if (GetModuleHandleW(filename) != module)
    {
        std::fputs("The owned async module was not retained through queue cleanup.\n", stderr);
        return 1;
    }
    if (!exit_code) std::printf("Isolated Windows async %s outcome passed.\n", argv[1]);
    return exit_code;
}
