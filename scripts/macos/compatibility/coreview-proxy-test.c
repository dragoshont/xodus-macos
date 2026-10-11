#define COBJMACROS
#define WIDL_using_Windows_ApplicationModel_Core
#define WIDL_using_Windows_UI_Core
#include <stdio.h>
#include "windows.h"
#include "objbase.h"
#include "cguid.h"
#include "initguid.h"
#include "windows.applicationmodel.core.h"

typedef __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationView_Windows__CApplicationModel__CActivation__CIActivatedEventArgs activated_handler;
#define HANDLER_IID IID___FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationView_Windows__CApplicationModel__CActivation__CIActivatedEventArgs

static unsigned checks, failures;
static const CLSID proxy_class = {0x00000320,0,0,{0xc0,0,0,0,0,0,0,0x46}};
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; printf("FAIL line %u: %s\n", __LINE__, #expr); } } while (0)

struct view
{
    ICoreApplicationView iface;
    LONG ref;
    DWORD owner;
    LONG calls;
};

static HRESULT WINAPI view_qi(ICoreApplicationView *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IInspectable)
        && !IsEqualIID(iid, &IID_ICoreApplicationView)) return E_NOINTERFACE;
    *out = iface;
    ICoreApplicationView_AddRef(iface);
    return S_OK;
}
static ULONG WINAPI view_addref(ICoreApplicationView *iface)
{
    return InterlockedIncrement(&CONTAINING_RECORD(iface, struct view, iface)->ref);
}
static ULONG WINAPI view_release(ICoreApplicationView *iface)
{
    return InterlockedDecrement(&CONTAINING_RECORD(iface, struct view, iface)->ref);
}
static HRESULT WINAPI view_iids(ICoreApplicationView *iface, ULONG *count, IID **ids)
{
    (void)iface;
    if (!count || !ids) return E_POINTER;
    *count = 1;
    *ids = CoTaskMemAlloc(sizeof(**ids));
    if (!*ids) { *count = 0; return E_OUTOFMEMORY; }
    **ids = IID_ICoreApplicationView;
    return S_OK;
}
static HRESULT WINAPI view_name(ICoreApplicationView *iface, HSTRING *name)
{
    (void)iface;
    if (!name) return E_POINTER;
    *name = NULL;
    return S_OK;
}
static HRESULT WINAPI view_trust(ICoreApplicationView *iface, TrustLevel *trust)
{
    (void)iface;
    if (!trust) return E_POINTER;
    *trust = BaseTrust;
    return S_OK;
}
static void record_call(ICoreApplicationView *iface)
{
    struct view *view = CONTAINING_RECORD(iface, struct view, iface);
    CHECK(GetCurrentThreadId() == view->owner);
    InterlockedIncrement(&view->calls);
}
static HRESULT WINAPI view_window(ICoreApplicationView *iface, ICoreWindow **window)
{
    record_call(iface);
    if (!window) return E_POINTER;
    *window = NULL;
    return S_OK;
}
static HRESULT WINAPI view_activated(ICoreApplicationView *iface, activated_handler *handler,
                                     EventRegistrationToken *token)
{
    HRESULT hr;
    record_call(iface);
    if (!handler || !token) return E_POINTER;
    token->value = 0x123456789abcdef;
    /* Exercise the reverse proxy without fabricating activation arguments. */
    hr = handler->lpVtbl->Invoke(handler, NULL, NULL);
    return hr;
}
static HRESULT WINAPI view_remove(ICoreApplicationView *iface, EventRegistrationToken token)
{
    record_call(iface);
    return token.value == 0x123456789abcdef ? S_OK : E_INVALIDARG;
}
static HRESULT WINAPI view_main(ICoreApplicationView *iface, boolean *value)
{
    record_call(iface);
    if (!value) return E_POINTER;
    *value = TRUE;
    return S_OK;
}
static HRESULT WINAPI view_hosted(ICoreApplicationView *iface, boolean *value)
{
    record_call(iface);
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}
static const ICoreApplicationViewVtbl view_vtbl =
{
    view_qi, view_addref, view_release, view_iids, view_name, view_trust,
    view_window, view_activated, view_remove, view_main, view_hosted
};

struct handler
{
    activated_handler iface;
    LONG ref, calls;
};
static HRESULT WINAPI handler_qi(activated_handler *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &HANDLER_IID)) return E_NOINTERFACE;
    *out = iface;
    iface->lpVtbl->AddRef(iface);
    return S_OK;
}
static ULONG WINAPI handler_addref(activated_handler *iface)
{
    return InterlockedIncrement(&CONTAINING_RECORD(iface, struct handler, iface)->ref);
}
static ULONG WINAPI handler_release(activated_handler *iface)
{
    return InterlockedDecrement(&CONTAINING_RECORD(iface, struct handler, iface)->ref);
}
static HRESULT WINAPI handler_invoke(activated_handler *iface, ICoreApplicationView *sender,
                                    __x_ABI_CWindows_CApplicationModel_CActivation_CIActivatedEventArgs *args)
{
    if (sender || args) return E_INVALIDARG;
    InterlockedIncrement(&CONTAINING_RECORD(iface, struct handler, iface)->calls);
    return S_OK;
}
static const __FITypedEventHandler_2_Windows__CApplicationModel__CCore__CCoreApplicationView_Windows__CApplicationModel__CActivation__CIActivatedEventArgsVtbl handler_vtbl =
{
    handler_qi, handler_addref, handler_release, handler_invoke
};

struct context { DWORD cookie; HANDLE done; struct view *view; };
static DWORD WINAPI worker(void *arg)
{
    struct context *ctx = arg;
    IGlobalInterfaceTable *git = NULL;
    ICoreApplicationView *proxy = NULL;
    ICoreWindow *window = (void *)0xdeadbeef;
    struct handler handler = {{&handler_vtbl}, 1, 0};
    EventRegistrationToken token = {0};
    boolean value;
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    CHECK(hr == S_OK);
    if (FAILED(hr)) { SetEvent(ctx->done); return 1; }
    hr = CoCreateInstance(&CLSID_StdGlobalInterfaceTable, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IGlobalInterfaceTable, (void **)&git);
    CHECK(hr == S_OK);
    if (SUCCEEDED(hr))
    {
        hr = IGlobalInterfaceTable_GetInterfaceFromGlobal(git, ctx->cookie,
                                                         &IID_ICoreApplicationView, (void **)&proxy);
        CHECK(hr == S_OK && proxy != NULL);
        if (SUCCEEDED(hr) && proxy)
        {
            CHECK(proxy != &ctx->view->iface);
            value = FALSE;
            CHECK(ICoreApplicationView_get_IsMain(proxy, &value) == S_OK && value == TRUE);
            value = TRUE;
            CHECK(ICoreApplicationView_get_IsHosted(proxy, &value) == S_OK && value == FALSE);
            CHECK(ICoreApplicationView_get_CoreWindow(proxy, &window) == S_OK && window == NULL);
            CHECK(ICoreApplicationView_add_Activated(proxy, &handler.iface, &token) == S_OK);
            CHECK(token.value == 0x123456789abcdef);
            CHECK(handler.calls == 1);
            CHECK(ICoreApplicationView_remove_Activated(proxy, token) == S_OK);
            ICoreApplicationView_Release(proxy);
        }
        IGlobalInterfaceTable_Release(git);
    }
    CoUninitialize();
    CHECK(handler.ref == 1);
    SetEvent(ctx->done);
    return 0;
}

int main(void)
{
    struct view view = {{&view_vtbl}, 1, 0, 0};
    struct context ctx = {0};
    IGlobalInterfaceTable *git = NULL;
    CLSID clsid;
    HANDLE thread;
    DWORD index, result;
    HRESULT hr;
    WCHAR path[MAX_PATH];
    HMODULE module;

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CHECK(hr == S_OK);
    if (FAILED(hr)) return 1;
    module = GetModuleHandleW(L"ole32.dll");
    CHECK(module && GetModuleFileNameW(module, path, MAX_PATH));
    if (module) printf("COREVIEW_PROXY_PROVIDER=%ls\n", path);
    CHECK(CoGetPSClsid(&IID_ICoreApplicationView, &clsid) == S_OK
          && IsEqualCLSID(&clsid, &proxy_class));
    CHECK(CoGetPSClsid(&HANDLER_IID, &clsid) == S_OK
          && IsEqualCLSID(&clsid, &proxy_class));
    view.owner = GetCurrentThreadId();
    ctx.view = &view;
    ctx.done = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(ctx.done != NULL);
    hr = CoCreateInstance(&CLSID_StdGlobalInterfaceTable, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IGlobalInterfaceTable, (void **)&git);
    CHECK(hr == S_OK);
    if (SUCCEEDED(hr) && ctx.done)
    {
        hr = IGlobalInterfaceTable_RegisterInterfaceInGlobal(git, (IUnknown *)&view.iface,
                                                             &IID_ICoreApplicationView, &ctx.cookie);
        CHECK(hr == S_OK);
        if (SUCCEEDED(hr))
        {
            thread = CreateThread(NULL, 0, worker, &ctx, 0, NULL);
            CHECK(thread != NULL);
            if (thread)
            {
                hr = CoWaitForMultipleHandles(0, 8000, 1, &ctx.done, &index);
                CHECK(hr == S_OK);
                /* Fail closed: never leave a worker accessing stack objects. */
                if (FAILED(hr)) ExitProcess(2);
                result = WaitForSingleObject(thread, 2000);
                CHECK(result == WAIT_OBJECT_0);
                if (result != WAIT_OBJECT_0) ExitProcess(2);
                CloseHandle(thread);
            }
            CHECK(IGlobalInterfaceTable_RevokeInterfaceFromGlobal(git, ctx.cookie) == S_OK);
        }
        IGlobalInterfaceTable_Release(git);
    }
    if (ctx.done) CloseHandle(ctx.done);
    CoUninitialize();
    CHECK(view.calls == 5);
    CHECK(view.ref == 1);
    printf("RESULT coreview_proxy_checks=%u failures=%u calls=%ld\n", checks, failures, view.calls);
    return failures ? 1 : 0;
}
