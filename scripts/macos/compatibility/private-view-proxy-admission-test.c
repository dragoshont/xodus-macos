#define COBJMACROS
#include <stdio.h>
#include <wchar.h>
#include "windows.h"
#include "objbase.h"
#include "rpcproxy.h"
#include "cguid.h"
#include "initguid.h"
#include "inspectable.h"
#include "winstring.h"
#include "eventtoken.h"

static const CLSID proxy_class =
    {0x95e15d0a,0x66e6,0x93d9,{0xc5,0x3c,0x76,0xe6,0x21,0x9d,0x33,0x41}};
static const IID private_view =
    {0x2e5500b6,0x66ad,0x467f,{0xab,0xb5,0x02,0x2a,0x64,0x28,0x3d,0x88}};

typedef struct private_object private_object;
struct private_vtbl
{
    HRESULT (WINAPI *QueryInterface)(private_object *, REFIID, void **);
    ULONG (WINAPI *AddRef)(private_object *);
    ULONG (WINAPI *Release)(private_object *);
    HRESULT (WINAPI *GetIids)(private_object *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(private_object *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(private_object *, TrustLevel *);
    HRESULT (WINAPI *properties)(private_object *, void **);
    HRESULT (WINAPI *window)(private_object *, IUnknown *, void **, void **);
    HRESULT (WINAPI *load)(private_object *, HSTRING);
    HRESULT (WINAPI *activate)(private_object *, IUnknown *, IUnknown *);
    HRESULT (WINAPI *notify)(private_object *);
    HRESULT (WINAPI *add)(private_object *, IUnknown *, EventRegistrationToken *);
    HRESULT (WINAPI *remove)(private_object *, EventRegistrationToken);
};
struct private_object
{
    const struct private_vtbl *lpVtbl;
    LONG ref;
    DWORD owner;
    LONG calls, wrong_thread, base_calls;
};

static HRESULT WINAPI object_qi(private_object *object, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IInspectable)
        && !IsEqualIID(iid, &private_view)) return E_NOINTERFACE;
    *out = object;
    object->lpVtbl->AddRef(object);
    return S_OK;
}
static ULONG WINAPI object_addref(private_object *object) { return InterlockedIncrement(&object->ref); }
static ULONG WINAPI object_release(private_object *object) { return InterlockedDecrement(&object->ref); }
static HRESULT WINAPI object_iids(private_object *object, ULONG *count, IID **ids)
{
    if (!count || !ids) return E_POINTER;
    if (GetCurrentThreadId() != object->owner) InterlockedIncrement(&object->wrong_thread);
    InterlockedIncrement(&object->base_calls);
    *count = 1;
    *ids = CoTaskMemAlloc(sizeof(**ids));
    if (!*ids) { *count = 0; return E_OUTOFMEMORY; }
    **ids = private_view;
    return S_OK;
}
static HRESULT WINAPI object_name(private_object *object, HSTRING *name)
{ (void)object; if (!name) return E_POINTER; *name = NULL; return S_OK; }
static HRESULT WINAPI object_trust(private_object *object, TrustLevel *trust)
{ (void)object; if (!trust) return E_POINTER; *trust = BaseTrust; return S_OK; }
static HRESULT WINAPI object_properties(private_object *object, void **value)
{ (void)object; if (value) *value = NULL; return E_NOTIMPL; }
static HRESULT WINAPI object_window(private_object *object, IUnknown *factory, void **window, void **dispatcher)
{ (void)object; (void)factory; if (window) *window = NULL; if (dispatcher) *dispatcher = NULL; return E_NOTIMPL; }
static HRESULT WINAPI object_load(private_object *object, HSTRING value)
{
    UINT32 length;
    const WCHAR *text = WindowsGetStringRawBuffer(value, &length);
    if (GetCurrentThreadId() != object->owner) InterlockedIncrement(&object->wrong_thread);
    InterlockedIncrement(&object->calls);
    return length == 12 && !wmemcmp(text, L"private-view", length) ? S_OK : E_INVALIDARG;
}
static HRESULT WINAPI object_activate(private_object *object, IUnknown *dispatcher, IUnknown *args)
{ (void)object; (void)dispatcher; (void)args; return E_NOTIMPL; }
static HRESULT WINAPI object_notify(private_object *object)
{
    if (GetCurrentThreadId() != object->owner) InterlockedIncrement(&object->wrong_thread);
    InterlockedIncrement(&object->calls);
    return S_OK;
}
static HRESULT WINAPI object_add(private_object *object, IUnknown *handler, EventRegistrationToken *token)
{ (void)object; (void)handler; if (token) token->value = 0; return E_NOTIMPL; }
static HRESULT WINAPI object_remove(private_object *object, EventRegistrationToken token)
{ (void)object; (void)token; return E_NOTIMPL; }
static const struct private_vtbl object_vtbl =
{
    object_qi, object_addref, object_release, object_iids, object_name, object_trust,
    object_properties, object_window, object_load, object_activate, object_notify, object_add, object_remove
};

struct stream_context
{
    IStream *stream;
    HANDLE done;
    HRESULT get, notify, load, iids, client3_notify, client3_load;
};
static DWORD WINAPI stream_worker(void *argument)
{
    struct stream_context *context = argument;
    private_object *proxy = NULL;
    HSTRING string = NULL;
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    context->get = hr;
    if (SUCCEEDED(hr))
    {
        context->get = CoGetInterfaceAndReleaseStream(context->stream, &private_view, (void **)&proxy);
        context->stream = NULL;
        printf("PRIVATE_VIEW_STAGE=unmarshal HRESULT=%08lx\n", context->get);
        fflush(stdout);
        if (SUCCEEDED(context->get))
        {
            CLIENT_CALL_RETURN (CDECL *client3)(MIDL_STUBLESS_PROXY_INFO *, ULONG, void *, ...);
            void **methods = *(void ***)proxy;
            MIDL_STUBLESS_PROXY_INFO *info = methods[-2];
            ULONG count = 0;
            IID *ids = NULL;
            context->iids = proxy->lpVtbl->GetIids(proxy, &count, &ids);
            if (SUCCEEDED(context->iids) && (count != 1 || !ids || !IsEqualIID(ids, &private_view)))
                context->iids = E_FAIL;
            printf("PRIVATE_VIEW_STAGE=slot3 HRESULT=%08lx\n", context->iids);
            CoTaskMemFree(ids);
            context->notify = proxy->lpVtbl->notify(proxy);
            printf("PRIVATE_VIEW_STAGE=slot10 HRESULT=%08lx\n", context->notify);
            fflush(stdout);
            hr = WindowsCreateString(L"private-view", 12, &string);
            if (SUCCEEDED(hr)) context->load = proxy->lpVtbl->load(proxy, string);
            else context->load = hr;
            printf("PRIVATE_VIEW_STAGE=slot8 HRESULT=%08lx\n", context->load);
            client3 = (void *)GetProcAddress(GetModuleHandleW(L"rpcrt4.dll"), "NdrClientCall3");
            if (client3 && info && SUCCEEDED(hr))
            {
                context->client3_notify = client3(info, 10, NULL, proxy).Simple;
                context->client3_load = client3(info, 8, NULL, proxy, string).Simple;
            }
            printf("PRIVATE_VIEW_STAGE=client3_slot10 HRESULT=%08lx\n", context->client3_notify);
            printf("PRIVATE_VIEW_STAGE=client3_slot8 HRESULT=%08lx\n", context->client3_load);
            WindowsDeleteString(string);
            proxy->lpVtbl->Release(proxy);
        }
        CoUninitialize();
    }
    SetEvent(context->done);
    return 0;
}

int main(void)
{
    HRESULT (WINAPI *get_class)(REFCLSID, REFIID, void **);
    IPSFactoryBuffer *factory = NULL;
    IRpcProxyBuffer *proxy = NULL;
    IUnknown *iface = NULL;
    IRpcStubBuffer *stub = NULL;
    private_object object = {&object_vtbl, 1, 0, 0, 0, 0};
    struct stream_context context = {NULL, NULL, E_FAIL, E_FAIL, E_FAIL, E_FAIL, E_FAIL, E_FAIL};
    HANDLE thread;
    DWORD index;
    HMODULE module;
    HRESULT hr;
    WCHAR path[MAX_PATH];
    unsigned failures = 0;
    setvbuf(stdout, NULL, _IONBF, 0);

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
    {
        printf("PRIVATE_VIEW_STAGE=initialize HRESULT=%08lx\n", hr);
        return 1;
    }
    module = LoadLibraryW(L"C:\\windows\\system32\\OneCoreUAPCommonProxyStub.dll");
    if (!module)
    {
        printf("PRIVATE_VIEW_STAGE=load WIN32_ERROR=%lu\n", GetLastError());
        CoUninitialize();
        return 1;
    }
    if (!GetModuleFileNameW(module, path, MAX_PATH))
    {
        printf("PRIVATE_VIEW_STAGE=provider WIN32_ERROR=%lu\n", GetLastError());
        FreeLibrary(module);
        CoUninitialize();
        return 1;
    }
    printf("PRIVATE_VIEW_PROXY_PROVIDER=%ls\n", path);
    get_class = (void *)GetProcAddress(module, "DllGetClassObject");
    if (!get_class)
    {
        printf("PRIVATE_VIEW_STAGE=export WIN32_ERROR=%lu\n", GetLastError());
        FreeLibrary(module);
        CoUninitialize();
        return 1;
    }
    hr = get_class(&proxy_class, &IID_IPSFactoryBuffer, (void **)&factory);
    printf("PRIVATE_VIEW_STAGE=factory HRESULT=%08lx\n", hr);
    if (FAILED(hr) || !factory)
        ++failures;
    else
    {
        hr = IPSFactoryBuffer_CreateProxy(factory, NULL, &private_view, &proxy, (void **)&iface);
        printf("PRIVATE_VIEW_STAGE=create_proxy HRESULT=%08lx\n", hr);
        if (FAILED(hr) || !proxy || !iface) ++failures;
        if (iface)
        {
            void **methods = *(void ***)iface;
            unsigned int i;
            ULONG_PTR stride = (ULONG_PTR)methods[7] - (ULONG_PTR)methods[6];
            for (i = 0; i < 13; i++)
                if (!methods[i] || methods[i] == (void *)-1 || methods[i] == (void *)-2) ++failures;
            if (!stride) ++failures;
            for (i = 6; i < 13; i++)
                if ((ULONG_PTR)methods[i] != (ULONG_PTR)methods[6] + (i - 6) * stride) ++failures;
            for (i = 3; i < 6; i++)
                if ((ULONG_PTR)methods[i] >= (ULONG_PTR)methods[6]
                    && (ULONG_PTR)methods[i] <= (ULONG_PTR)methods[12]) ++failures;
            printf("PRIVATE_VIEW_PROXY_SHAPE=13 slots 3-5 delegated 6-12 stubless failures=%u\n", failures);
        }
        if (iface) IUnknown_Release(iface);
        if (proxy) IRpcProxyBuffer_Release(proxy);
        object.owner = GetCurrentThreadId();
        hr = IPSFactoryBuffer_CreateStub(factory, &private_view, (IUnknown *)&object, &stub);
        printf("PRIVATE_VIEW_STAGE=create_stub HRESULT=%08lx\n", hr);
        if (FAILED(hr) || !stub) ++failures;
        if (stub)
        {
            const CInterfaceStubHeader *header =
                &CONTAINING_RECORD(stub->lpVtbl, CInterfaceStubVtbl, Vtbl)->header;
            PRPC_STUB_FUNCTION forwarding = (void *)GetProcAddress(GetModuleHandleW(L"rpcrt4.dll"),
                                                                   "NdrStubForwardingFunction");
            unsigned int i;
            if (header->DispatchTableCount != 13 || !header->pDispatchTable
                || header->pDispatchTable == (void *)-2 || !forwarding) ++failures;
            else
            {
                for (i = 3; i < 6; i++)
                    if (header->pDispatchTable[i] != forwarding) ++failures;
                for (i = 6; i < 13; i++)
                    if (!header->pDispatchTable[i] || header->pDispatchTable[i] == forwarding) ++failures;
            }
            printf("PRIVATE_VIEW_STUB_SHAPE=13 slots dispatch synthesized failures=%u\n", failures);
        }
        if (stub) IRpcStubBuffer_Release(stub);
        IPSFactoryBuffer_Release(factory);
    }
    if (!failures)
    {
        hr = CoMarshalInterThreadInterfaceInStream(&private_view, (IUnknown *)&object, &context.stream);
        printf("PRIVATE_VIEW_STAGE=marshal HRESULT=%08lx\n", hr);
        if (FAILED(hr)) ++failures;
        else
        {
            context.done = CreateEventW(NULL, TRUE, FALSE, NULL);
            thread = context.done ? CreateThread(NULL, 0, stream_worker, &context, 0, NULL) : NULL;
            if (!thread)
            {
                LARGE_INTEGER start = {.QuadPart = 0};
                IStream_Seek(context.stream, start, STREAM_SEEK_SET, NULL);
                CoReleaseMarshalData(context.stream);
                IStream_Release(context.stream);
                ++failures;
            }
            else
            {
                hr = CoWaitForMultipleHandles(0, 8000, 1, &context.done, &index);
                if (FAILED(hr) || WaitForSingleObject(thread, 2000) != WAIT_OBJECT_0)
                    ExitProcess(2);
                CloseHandle(thread);
                if (context.stream)
                {
                    LARGE_INTEGER start = {.QuadPart = 0};
                    IStream_Seek(context.stream, start, STREAM_SEEK_SET, NULL);
                    CoReleaseMarshalData(context.stream);
                    IStream_Release(context.stream);
                    context.stream = NULL;
                    ++failures;
                }
                if (context.get != S_OK || context.notify != S_OK || context.load != S_OK || context.iids != S_OK
                    || context.client3_notify != S_OK || context.client3_load != S_OK
                    || object.calls != 4 || !object.base_calls || object.wrong_thread || object.ref != 1) ++failures;
            }
            if (context.done) CloseHandle(context.done);
        }
    }
    FreeLibrary(module);
    CoUninitialize();
    printf("RESULT private_view_proxy_loader_failures=%u calls=%ld wrong_thread=%ld ref=%ld\n",
           failures, object.calls, object.wrong_thread, object.ref);
    return failures ? 1 : 0;
}
