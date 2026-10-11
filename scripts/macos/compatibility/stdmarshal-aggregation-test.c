#define COBJMACROS
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include "windows.h"
#include "objbase.h"
#include "cguid.h"

/* STDMSHLFLAGS values from combaseapi.h; absent from the pinned Wine headers. */
#define SMEXF_HANDLER 0
#define SMEXF_SERVER 1

static HRESULT (WINAPI *create_marshaler)(IUnknown *, DWORD, IUnknown **);
static unsigned checks, failures;
static LONG created, destroyed;
static const CLSID test_class = {0xafd8f603,0x9ce1,0x42fb,{0xac,0xc2,0x3a,0xc9,0xc7,0x28,0xf8,0x2b}};
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; printf("FAIL line %u: %s\n", __LINE__, #expr); } } while (0)

struct outer
{
    IPersistFile IPersistFile_iface;
    LONG ref;
    IUnknown *inner;
    DWORD owner, method_thread;
    LONG calls;
};

static struct outer *impl_from_IPersistFile(IPersistFile *iface)
{
    return CONTAINING_RECORD(iface, struct outer, IPersistFile_iface);
}

static HRESULT WINAPI outer_qi(IPersistFile *iface, REFIID iid, void **out)
{
    struct outer *impl = impl_from_IPersistFile(iface);
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IPersist) || IsEqualIID(iid, &IID_IPersistFile))
    {
        *out = iface;
        IPersistFile_AddRef(iface);
        return S_OK;
    }
    if (IsEqualIID(iid, &IID_IMarshal))
        return IUnknown_QueryInterface(impl->inner, iid, out);
    return E_NOINTERFACE;
}

static ULONG WINAPI outer_addref(IPersistFile *iface)
{
    return InterlockedIncrement(&impl_from_IPersistFile(iface)->ref);
}

static ULONG WINAPI outer_release(IPersistFile *iface)
{
    struct outer *impl = impl_from_IPersistFile(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);
    if (!ref)
    {
        IUnknown_Release(impl->inner);
        InterlockedIncrement(&destroyed);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI outer_class(IPersistFile *iface, CLSID *clsid)
{
    struct outer *impl = impl_from_IPersistFile(iface);
    if (!clsid) return E_POINTER;
    *clsid = test_class;
    impl->method_thread = GetCurrentThreadId();
    InterlockedIncrement(&impl->calls);
    return S_OK;
}

static HRESULT WINAPI outer_dirty(IPersistFile *iface) { (void)iface; return S_FALSE; }
static HRESULT WINAPI outer_load(IPersistFile *iface, LPCOLESTR file, DWORD mode)
{ (void)iface; (void)file; (void)mode; return E_NOTIMPL; }
static HRESULT WINAPI outer_save(IPersistFile *iface, LPCOLESTR file, BOOL remember)
{ (void)iface; (void)file; (void)remember; return E_NOTIMPL; }
static HRESULT WINAPI outer_saved(IPersistFile *iface, LPCOLESTR file)
{ (void)iface; (void)file; return E_NOTIMPL; }
static HRESULT WINAPI outer_file(IPersistFile *iface, LPOLESTR *file)
{ (void)iface; if (file) *file = NULL; return E_NOTIMPL; }

static const IPersistFileVtbl outer_vtbl =
{
    outer_qi, outer_addref, outer_release, outer_class, outer_dirty,
    outer_load, outer_save, outer_saved, outer_file,
};

static struct outer *outer_create(void)
{
    struct outer *impl = calloc(1, sizeof(*impl));
    if (!impl) return NULL;
    impl->IPersistFile_iface.lpVtbl = &outer_vtbl;
    impl->ref = 1;
    impl->owner = GetCurrentThreadId();
    if (FAILED(create_marshaler((IUnknown *)&impl->IPersistFile_iface, SMEXF_SERVER, &impl->inner)))
    {
        free(impl);
        return NULL;
    }
    InterlockedIncrement(&created);
    return impl;
}

struct worker_data
{
    IStream *stream;
    DWORD cookie;
    IPersistFile *source;
    HRESULT initialize, acquire, invoke;
    BOOL proxy, class_matches;
};

static DWORD WINAPI worker(void *param)
{
    struct worker_data *data = param;
    IPersistFile *proxy = NULL;
    IGlobalInterfaceTable *git = NULL;
    CLSID clsid;

    data->initialize = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(data->initialize)) return 1;
    if (data->stream)
        data->acquire = CoUnmarshalInterface(data->stream, &IID_IPersistFile, (void **)&proxy);
    else
    {
        data->acquire = CoCreateInstance(&CLSID_StdGlobalInterfaceTable, NULL, CLSCTX_INPROC_SERVER,
                                        &IID_IGlobalInterfaceTable, (void **)&git);
        if (SUCCEEDED(data->acquire))
            data->acquire = IGlobalInterfaceTable_GetInterfaceFromGlobal(git, data->cookie,
                                                                        &IID_IPersistFile, (void **)&proxy);
    }
    if (SUCCEEDED(data->acquire) && proxy)
    {
        data->proxy = proxy != data->source;
        data->invoke = IPersistFile_GetClassID(proxy, &clsid);
        data->class_matches = SUCCEEDED(data->invoke) && IsEqualCLSID(&clsid, &test_class);
        IPersistFile_Release(proxy);
    }
    if (git) IGlobalInterfaceTable_Release(git);
    CoUninitialize();
    return 0;
}

static void run_worker(struct worker_data *data)
{
    HANDLE thread = CreateThread(NULL, 0, worker, data, 0, NULL);
    DWORD index = ~0u;
    HRESULT hr;
    CHECK(thread != NULL);
    if (!thread) return;
    hr = CoWaitForMultipleHandles(0, 8000, 1, &thread, &index);
    CHECK(hr == S_OK && index == 0);
    if (FAILED(hr))
    {
        fprintf(stderr, "Cross-apartment test timed out or failed: %08lx; refusing live-thread cleanup\n",
                (unsigned long)hr);
        ExitProcess(1);
    }
    CHECK(CloseHandle(thread));
    CHECK(SUCCEEDED(data->initialize));
    CHECK(data->acquire == S_OK);
    CHECK(data->proxy && data->invoke == S_OK && data->class_matches);
}

int main(void)
{
    HMODULE module = LoadLibraryW(L"combase.dll");
    WCHAR path[MAX_PATH];
    struct outer *object = NULL, *lifetime = NULL;
    IUnknown *inner = (void *)1, *identity = NULL, *unsupported = (void *)1;
    IMarshal *marshal = NULL;
    IStream *stream = NULL;
    IGlobalInterfaceTable *git = NULL;
    struct worker_data data = {0};
    LARGE_INTEGER zero = {{0}};
    CLSID clsid;
    DWORD size = 0, cookie = 0;
    LONG before;
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    CHECK(SUCCEEDED(hr));
    if (FAILED(hr)) return 1;
    CHECK(module != NULL);
    if (!module) goto done;
    CHECK(GetModuleFileNameW(module, path, MAX_PATH) != 0);
    printf("STDMARSHAL_PROVIDER=%ls\n", path);
    create_marshaler = (void *)GetProcAddress(module, "CoGetStdMarshalEx");
    CHECK(create_marshaler != NULL);
    CHECK((void *)create_marshaler == (void *)GetProcAddress(module, (LPCSTR)(ULONG_PTR)363));
    if (!create_marshaler) goto done;
    CHECK(create_marshaler(NULL, SMEXF_SERVER, &inner) == E_INVALIDARG && !inner);
    CHECK(create_marshaler(NULL, SMEXF_SERVER, NULL) == E_INVALIDARG);
    object = outer_create();
    CHECK(object != NULL);
    if (!object) goto done;
    CHECK(object->ref == 1);
    CHECK(create_marshaler((IUnknown *)&object->IPersistFile_iface, SMEXF_SERVER, NULL) == E_INVALIDARG);
    CHECK(create_marshaler((IUnknown *)&object->IPersistFile_iface, SMEXF_HANDLER, &inner) == E_NOTIMPL && !inner);
    CHECK(create_marshaler((IUnknown *)&object->IPersistFile_iface, 2, &inner) == E_NOTIMPL && !inner);
    CHECK(IUnknown_QueryInterface(object->inner, &IID_IUnknown, (void **)&identity) == S_OK && identity == object->inner);
    CHECK(object->ref == 1);
    CHECK(IUnknown_Release(identity) == 1);
    identity = NULL;
    CHECK(IUnknown_QueryInterface(object->inner, &IID_IPersistFile, (void **)&unsupported) == E_NOINTERFACE && !unsupported);
    CHECK(IUnknown_QueryInterface(object->inner, &IID_IMarshal, (void **)&marshal) == S_OK && marshal);
    CHECK(object->ref == 2);
    if (!marshal) goto done;
    CHECK(IMarshal_QueryInterface(marshal, &IID_IUnknown, (void **)&identity) == S_OK
          && identity == (IUnknown *)&object->IPersistFile_iface);
    CHECK(object->ref == 3);
    IUnknown_Release(identity);
    identity = NULL;
    CHECK(IMarshal_GetUnmarshalClass(marshal, &IID_IPersistFile, &object->IPersistFile_iface,
                                    MSHCTX_INPROC, NULL, MSHLFLAGS_NORMAL, &clsid) == S_OK
          && IsEqualCLSID(&clsid, &CLSID_StdMarshal));
    CHECK(IMarshal_GetMarshalSizeMax(marshal, &IID_IPersistFile, &object->IPersistFile_iface,
                                    MSHCTX_INPROC, NULL, MSHLFLAGS_NORMAL, &size) == S_OK && size);
    CHECK(IMarshal_Release(marshal) == 1);
    marshal = NULL;
    CHECK(CreateStreamOnHGlobal(NULL, TRUE, &stream) == S_OK && stream);
    if (!stream) goto done;
    CHECK(CoMarshalInterface(stream, &IID_IPersistFile, (IUnknown *)&object->IPersistFile_iface,
                             MSHCTX_INPROC, NULL, MSHLFLAGS_NORMAL) == S_OK);
    CHECK(IStream_Seek(stream, zero, STREAM_SEEK_SET, NULL) == S_OK);
    data.stream = stream; data.source = &object->IPersistFile_iface;
    run_worker(&data);
    CHECK(object->calls == 1 && object->method_thread == object->owner);
    IStream_Release(stream);
    stream = NULL;
    CHECK(CoCreateInstance(&CLSID_StdGlobalInterfaceTable, NULL, CLSCTX_INPROC_SERVER,
                           &IID_IGlobalInterfaceTable, (void **)&git) == S_OK && git);
    if (!git) goto done;
    CHECK(IGlobalInterfaceTable_RegisterInterfaceInGlobal(git, (IUnknown *)&object->IPersistFile_iface,
                                                         &IID_IPersistFile, &cookie) == S_OK && cookie);
    data = (struct worker_data){0};
    data.cookie = cookie; data.source = &object->IPersistFile_iface;
    run_worker(&data);
    CHECK(object->calls == 2 && object->method_thread == object->owner);
    CHECK(IGlobalInterfaceTable_RevokeInterfaceFromGlobal(git, cookie) == S_OK);
    cookie = 0;
    CHECK(IPersistFile_Release(&object->IPersistFile_iface) == 0);
    object = NULL;
    lifetime = outer_create();
    CHECK(lifetime != NULL);
    if (!lifetime) goto done;
    inner = lifetime->inner;
    CHECK(IUnknown_AddRef(inner) == 2);
    CHECK(IUnknown_Release(inner) == 1);
    CHECK(lifetime->ref == 1);
    CHECK(IPersistFile_Release(&lifetime->IPersistFile_iface) == 0);
    lifetime = NULL;
    lifetime = outer_create();
    CHECK(lifetime != NULL);
    if (!lifetime) goto done;
    inner = lifetime->inner;
    CHECK(IUnknown_AddRef(inner) == 2);
    before = destroyed;
    CHECK(IPersistFile_Release(&lifetime->IPersistFile_iface) == 0 && destroyed == before + 1);
    lifetime = NULL;
    CHECK(IUnknown_Release(inner) == 0);
    inner = NULL;
done:
    if (cookie && git) IGlobalInterfaceTable_RevokeInterfaceFromGlobal(git, cookie);
    if (git) IGlobalInterfaceTable_Release(git);
    if (stream) IStream_Release(stream);
    if (identity) IUnknown_Release(identity);
    if (marshal) IMarshal_Release(marshal);
    if (object) IPersistFile_Release(&object->IPersistFile_iface);
    if (lifetime) IPersistFile_Release(&lifetime->IPersistFile_iface);
    CoUninitialize();
    CHECK(created == destroyed);
    if (module) FreeLibrary(module);
    printf("stdmarshal_aggregation_checks=%u failures=%u objects=%ld destroyed=%ld\n",
           checks, failures, created, destroyed);
    return failures ? 1 : 0;
}
