#define COBJMACROS
#include <stdio.h>
#include "windows.h"
#include "objbase.h"
#include "objidl.h"
#include "cguid.h"
#include "initguid.h"
#include "windows.foundation.h"

struct handler
{
    __FIEventHandler_1_IInspectable iface;
    LONG refs, calls;
    DWORD called_on;
    BOOL agile;
    IUnknown *marshaler;
};

static struct handler *impl(__FIEventHandler_1_IInspectable *iface)
{
    return CONTAINING_RECORD(iface, struct handler, iface);
}

static HRESULT WINAPI handler_qi(__FIEventHandler_1_IInspectable *iface, REFIID iid, void **out)
{
    struct handler *handler = impl(iface);
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID___FIEventHandler_1_IInspectable)
        || (handler->agile && IsEqualIID(iid, &IID_IAgileObject)))
    {
        *out = iface;
        InterlockedIncrement(&handler->refs);
        return S_OK;
    }
    if (handler->agile && handler->marshaler && IsEqualIID(iid, &IID_IMarshal))
        return IUnknown_QueryInterface(handler->marshaler, iid, out);
    return E_NOINTERFACE;
}

static ULONG WINAPI handler_addref(__FIEventHandler_1_IInspectable *iface)
{
    return InterlockedIncrement(&impl(iface)->refs);
}

static ULONG WINAPI handler_release(__FIEventHandler_1_IInspectable *iface)
{
    return InterlockedDecrement(&impl(iface)->refs);
}

static HRESULT WINAPI handler_invoke(__FIEventHandler_1_IInspectable *iface,
                                    IInspectable *sender, IInspectable *args)
{
    struct handler *handler = impl(iface);
    if (sender || args) return E_INVALIDARG;
    handler->called_on = GetCurrentThreadId();
    InterlockedIncrement(&handler->calls);
    return S_OK;
}

static const __FIEventHandler_1_IInspectableVtbl handler_vtbl =
    {handler_qi, handler_addref, handler_release, handler_invoke};

struct context
{
    DWORD cookies[2], thread_id;
    HRESULT got[2], invoked[2];
    HANDLE done;
};

static DWORD WINAPI worker(void *argument)
{
    struct context *context = argument;
    IGlobalInterfaceTable *git = NULL;
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    unsigned int i;

    context->thread_id = GetCurrentThreadId();
    if (SUCCEEDED(hr))
    {
        hr = CoCreateInstance(&CLSID_StdGlobalInterfaceTable, NULL, CLSCTX_INPROC_SERVER,
                              &IID_IGlobalInterfaceTable, (void **)&git);
        for (i = 0; i < 2; i++)
        {
            __FIEventHandler_1_IInspectable *handler = NULL;
            if (!context->cookies[i])
            {
                printf("EVENT_HANDLER_MODE=%s get=NOT_RUN invoke=NOT_RUN (registration failed)\n",
                       i ? "non-agile" : "agile");
                continue;
            }
            context->got[i] = hr;
            if (SUCCEEDED(hr))
                context->got[i] = IGlobalInterfaceTable_GetInterfaceFromGlobal(
                    git, context->cookies[i], &IID___FIEventHandler_1_IInspectable, (void **)&handler);
            if (handler)
            {
                context->invoked[i] = __FIEventHandler_1_IInspectable_Invoke(handler, NULL, NULL);
                __FIEventHandler_1_IInspectable_Release(handler);
            }
            printf("EVENT_HANDLER_MODE=%s get=%08lx invoke=%08lx\n",
                   i ? "non-agile" : "agile", context->got[i], context->invoked[i]);
        }
        if (git) IGlobalInterfaceTable_Release(git);
        CoUninitialize();
    }
    else context->got[0] = context->got[1] = hr;
    SetEvent(context->done);
    return 0;
}

int main(void)
{
    static const IID expected_iid =
        {0xc50898f6,0xc536,0x5f47,{0x85,0x83,0x8b,0x2c,0x24,0x38,0xa1,0x3b}};
    struct handler handlers[2] =
        {{{&handler_vtbl}, 1, 0, 0, TRUE, NULL}, {{&handler_vtbl}, 1, 0, 0, FALSE, NULL}};
    struct context context = {{0, 0}, 0, {E_FAIL, E_FAIL}, {E_FAIL, E_FAIL}, NULL};
    IGlobalInterfaceTable *git = NULL;
    DWORD owner = GetCurrentThreadId(), index;
    HRESULT registered[2] = {E_FAIL, E_FAIL}, hr;
    HANDLE thread;
    unsigned int i, failures = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (!IsEqualIID(&IID___FIEventHandler_1_IInspectable, &expected_iid))
        return 1;
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) { printf("EVENT_HANDLER_INITIALIZE=%08lx\n", hr); return 1; }
    hr = CoCreateFreeThreadedMarshaler((IUnknown *)&handlers[0].iface, &handlers[0].marshaler);
    if (FAILED(hr)) { printf("EVENT_HANDLER_FTM=%08lx\n", hr); CoUninitialize(); return 1; }
    hr = CoCreateInstance(&CLSID_StdGlobalInterfaceTable, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IGlobalInterfaceTable, (void **)&git);
    if (SUCCEEDED(hr))
    {
        for (i = 0; i < 2; i++)
        {
            registered[i] = IGlobalInterfaceTable_RegisterInterfaceInGlobal(
                git, (IUnknown *)&handlers[i].iface, &IID___FIEventHandler_1_IInspectable, &context.cookies[i]);
            printf("EVENT_HANDLER_MODE=%s register=%08lx\n", i ? "non-agile" : "agile", registered[i]);
        }
        context.done = CreateEventW(NULL, TRUE, FALSE, NULL);
        thread = context.done ? CreateThread(NULL, 0, worker, &context, 0, NULL) : NULL;
        if (!thread) ++failures;
        else
        {
            hr = CoWaitForMultipleHandles(0, 8000, 1, &context.done, &index);
            if (FAILED(hr) || WaitForSingleObject(thread, 2000) != WAIT_OBJECT_0) ExitProcess(2);
            CloseHandle(thread);
        }
        for (i = 0; i < 2; i++)
        {
            if (registered[i] != S_OK || context.got[i] != S_OK || context.invoked[i] != S_OK
                || handlers[i].calls != 1 || handlers[i].called_on != (i ? owner : context.thread_id))
                ++failures;
            if (context.cookies[i] && FAILED(IGlobalInterfaceTable_RevokeInterfaceFromGlobal(git, context.cookies[i])))
                ++failures;
        }
        if (context.done) CloseHandle(context.done);
        IGlobalInterfaceTable_Release(git);
    }
    else { printf("EVENT_HANDLER_GIT=%08lx\n", hr); ++failures; }
    IUnknown_Release(handlers[0].marshaler);
    if (handlers[0].refs != 1 || handlers[1].refs != 1) ++failures;
    CoUninitialize();
    printf("RESULT event_handler_failures=%u agile_calls=%ld non_agile_calls=%ld\n",
           failures, handlers[0].calls, handlers[1].calls);
    return failures ? 1 : 0;
}
