/* Window broker for the original Xbox app under Wine (Xodus shell-host lane).
 *
 * Natively, twinapi.appcore's GetWindowFactory (26100.9278) does
 *   CoCreateInstance({3480A401-BDE9-4407-BC02-798A866AC051}, CLSCTX_LOCAL_SERVER|NO_CODE_DOWNLOAD,
 *                    IServiceHostBrokerProvider)
 *   provider->vtbl[3](IID_IApplicationActivationBroker, IID_IApplicationActivationBroker, &broker)
 *   broker->vtbl[5](UINT64 aamId, out 32-byte results, out IUnknown **factory)
 *   factory->QueryInterface(ICoreWindowFactory {CD292360-2763-4085-8A9F-74B224A29175})
 * The factory is custom-marshaled with unmarshal class {14DE3806-5D5B-405C-AB89-4AC936BCBF48}
 * (CImmersiveWindowFactoryProxy, twinapi.appcore). Wire format, measured from
 * CWrlLightweightHandlerBase::MarshalInterface and CImmersiveWindowFactoryBase::v_MarshalAdditionalData:
 *   standard OBJREF (SMEXF_SERVER std marshaler) | u32 size | additional data
 * Additional data: RECT, u32 window type, u8 nav flag, u8 0, u8 rotation present [, u32 rotation],
 *   u8 prop present [, u32 prop], u32 navigation id, u8 b.
 *
 * This is not Windows' broker. It supplies no window band, no ApplicationFrameHost and no shell
 * policy; the client-side proxy creates the CoreWindow in the app process. The geometry and window
 * type are this host's choices and are logged. Methods whose native contract was not measured fail
 * with E_NOTIMPL.
 *
 * usage: shellhost-broker.exe [seconds=240] [width=1280] [height=800] [window_type=6] */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <inspectable.h>

static const CLSID CLSID_Broker = {0x3480a401,0xbde9,0x4407,{0xbc,0x02,0x79,0x8a,0x86,0x6a,0xc0,0x51}};
static const IID IID_ServiceHostBrokerProvider = {0x0f4accb1,0xd8f9,0x4011,{0xba,0x37,0x25,0x57,0x92,0x5a,0x78,0xcf}};
static const IID IID_ApplicationActivationBroker = {0xd98fd14a,0x522a,0x4d59,{0xb8,0x75,0x81,0x1e,0x83,0x91,0x9a,0x9e}};
static const CLSID CLSID_WindowFactoryProxy = {0x14de3806,0x5d5b,0x405c,{0xab,0x89,0x4a,0xc9,0x36,0xbc,0xbf,0x48}};
static const IID IID_ConfigureWindowFactory = {0x601d51e3,0x801e,0x49c9,{0xbb,0xfa,0xfe,0x29,0xa6,0x62,0xae,0xad}};
static const IID IID_Inspectable = {0xaf86e2e0,0xb12d,0x4c6a,{0x9c,0x5a,0xd7,0xaa,0x65,0x10,0x1e,0x90}};
static const IID IID_CoreWindowFactory = {0xcd292360,0x2763,0x4085,{0x8a,0x9f,0x74,0xb2,0x24,0xa2,0x91,0x75}};

static LONG width = 1280, height = 800, window_type = 6;

static void blog(const char *fmt, ...)
{
    va_list ap;
    SYSTEMTIME t;
    GetLocalTime(&t);
    printf("[broker %02u:%02u:%02u.%03u] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    printf("\n"); fflush(stdout);
}

static const char *guidstr(REFGUID g, char *buf)
{
    sprintf(buf, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", g->Data1, g->Data2, g->Data3,
            g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
    return buf;
}

/* ---- window factory: server object, custom-marshaled to CImmersiveWindowFactoryProxy ---- */
struct factory
{
    IInspectable IUnknown_iface;  /* the broker returns the factory as IInspectable */
    IMarshal IMarshal_iface;
    IStdMarshalInfo IStdMarshalInfo_iface;
    IUnknown IConfigureWindowFactory_iface;
    IUnknown ICoreWindowFactory_iface;
    LONG ref;
    DWORD view_id;
    IUnknown *std_inner;
};

static struct factory *factory_from_IUnknown(IInspectable *i) { return CONTAINING_RECORD(i, struct factory, IUnknown_iface); }
static struct factory *factory_from_IMarshal(IMarshal *i) { return CONTAINING_RECORD(i, struct factory, IMarshal_iface); }

static HRESULT WINAPI factory_QueryInterface(IInspectable *iface, REFIID riid, void **out)
{
    struct factory *f = factory_from_IUnknown(iface);
    char g[40];
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_Inspectable)) *out = &f->IUnknown_iface;
    else if (IsEqualIID(riid, &IID_IMarshal)) *out = &f->IMarshal_iface;
    else if (IsEqualIID(riid, &IID_IStdMarshalInfo)) *out = &f->IStdMarshalInfo_iface;
    else if (IsEqualIID(riid, &IID_ConfigureWindowFactory)) *out = &f->IConfigureWindowFactory_iface;
    else if (IsEqualIID(riid, &IID_CoreWindowFactory)) *out = &f->ICoreWindowFactory_iface;
    else
    {
        *out = NULL;
        blog("factory QI %s -> E_NOINTERFACE", guidstr(riid, g));
        return E_NOINTERFACE;
    }
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}
static ULONG WINAPI factory_AddRef(IInspectable *iface) { return InterlockedIncrement(&factory_from_IUnknown(iface)->ref); }
static ULONG WINAPI factory_Release(IInspectable *iface)
{
    struct factory *f = factory_from_IUnknown(iface);
    ULONG r = InterlockedDecrement(&f->ref);
    if (!r)
    {
        blog("factory released");
        if (f->std_inner) IUnknown_Release(f->std_inner);
        free(f);
    }
    return r;
}
static HRESULT WINAPI factory_GetIids(IInspectable *iface, ULONG *count, IID **iids) { blog("factory GetIids -> E_NOTIMPL"); return E_NOTIMPL; }
static HRESULT WINAPI factory_GetRuntimeClassName(IInspectable *iface, HSTRING *name) { blog("factory GetRuntimeClassName -> E_NOTIMPL"); return E_NOTIMPL; }
static HRESULT WINAPI factory_GetTrustLevel(IInspectable *iface, TrustLevel *level) { blog("factory GetTrustLevel -> E_NOTIMPL"); return E_NOTIMPL; }
static const IInspectableVtbl factory_vtbl = { factory_QueryInterface, factory_AddRef, factory_Release,
    factory_GetIids, factory_GetRuntimeClassName, factory_GetTrustLevel };

/* CWrlLightweightHandlerServer::GetClassForHandler: the client-side handler is twinapi.appcore's
 * CImmersiveWindowFactoryProxy (SMEXF_HANDLER). */
static struct factory *factory_from_IStdMarshalInfo(IStdMarshalInfo *i) { return CONTAINING_RECORD(i, struct factory, IStdMarshalInfo_iface); }
static HRESULT WINAPI fsi_QueryInterface(IStdMarshalInfo *iface, REFIID riid, void **out) { return factory_QueryInterface(&factory_from_IStdMarshalInfo(iface)->IUnknown_iface, riid, out); }
static ULONG WINAPI fsi_AddRef(IStdMarshalInfo *iface) { return factory_AddRef(&factory_from_IStdMarshalInfo(iface)->IUnknown_iface); }
static ULONG WINAPI fsi_Release(IStdMarshalInfo *iface) { return factory_Release(&factory_from_IStdMarshalInfo(iface)->IUnknown_iface); }
static HRESULT WINAPI fsi_GetClassForHandler(IStdMarshalInfo *iface, DWORD ctx, void *ctxdata, CLSID *clsid)
{
    blog("factory GetClassForHandler ctx=%lu", ctx);
    *clsid = CLSID_WindowFactoryProxy;
    return S_OK;
}
static const IStdMarshalInfoVtbl factory_stdinfo_vtbl = { fsi_QueryInterface, fsi_AddRef, fsi_Release, fsi_GetClassForHandler };

/* IConfigureWindowFactory {601D51E3-801E-49C9-BBFA-FE29A662AEAD}: method order from twinapi.appcore's
 * CImmersiveWindowFactoryBase vtable (PDB); proxy/stub {95E15D0A-...} in OneCoreUAPCommonProxyStub.dll.
 * CoreApplication::ApplyViewActivationResults calls CreateSplashScreen and ignores its HRESULT.
 * This host has no splash-screen or frame surface, so those methods fail honestly. */
struct configure_window_factory_vtbl
{
    HRESULT (WINAPI *QueryInterface)(IUnknown *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IUnknown *);
    ULONG (WINAPI *Release)(IUnknown *);
    HRESULT (WINAPI *Initialize)(IUnknown *, IUnknown *monitor, const WCHAR *name);
    HRESULT (WINAPI *InitializeWithPosition)(IUnknown *, IUnknown *monitor, const WCHAR *name, const RECT *rect);
    HRESULT (WINAPI *CreateSplashScreen)(IUnknown *, int flags, IUnknown **splash);
    HRESULT (WINAPI *GetAppWindow)(IUnknown *, HWND *hwnd);
    HRESULT (WINAPI *HasExistingSplashScreen)(IUnknown *, BOOL *exists);
    HRESULT (WINAPI *SetViewId)(IUnknown *, DWORD id);
};
static struct factory *factory_from_IConfigure(IUnknown *i) { return CONTAINING_RECORD(i, struct factory, IConfigureWindowFactory_iface); }
static HRESULT WINAPI cwf_QueryInterface(IUnknown *iface, REFIID riid, void **out) { return factory_QueryInterface(&factory_from_IConfigure(iface)->IUnknown_iface, riid, out); }
static ULONG WINAPI cwf_AddRef(IUnknown *iface) { return factory_AddRef(&factory_from_IConfigure(iface)->IUnknown_iface); }
static ULONG WINAPI cwf_Release(IUnknown *iface) { return factory_Release(&factory_from_IConfigure(iface)->IUnknown_iface); }
static HRESULT WINAPI cwf_Initialize(IUnknown *iface, IUnknown *monitor, const WCHAR *name)
{
    blog("IConfigureWindowFactory::Initialize monitor=%p name=%ls -> E_NOTIMPL", monitor, name ? name : L"(null)");
    return E_NOTIMPL;
}
static HRESULT WINAPI cwf_InitializeWithPosition(IUnknown *iface, IUnknown *monitor, const WCHAR *name, const RECT *rect)
{
    blog("IConfigureWindowFactory::InitializeWithPosition monitor=%p name=%ls -> E_NOTIMPL", monitor, name ? name : L"(null)");
    return E_NOTIMPL;
}
static HRESULT WINAPI cwf_CreateSplashScreen(IUnknown *iface, int flags, IUnknown **splash)
{
    if (splash) *splash = NULL;
    blog("IConfigureWindowFactory::CreateSplashScreen flags=%#x -> E_NOTIMPL (no splash surface)", flags);
    return E_NOTIMPL;
}
static HRESULT WINAPI cwf_GetAppWindow(IUnknown *iface, HWND *hwnd)
{
    if (hwnd) *hwnd = NULL;
    blog("IConfigureWindowFactory::GetAppWindow -> E_NOTIMPL");
    return E_NOTIMPL;
}
static HRESULT WINAPI cwf_HasExistingSplashScreen(IUnknown *iface, BOOL *exists)
{
    if (!exists) return E_POINTER;
    *exists = FALSE;
    blog("IConfigureWindowFactory::HasExistingSplashScreen -> S_OK FALSE");
    return S_OK;
}
static HRESULT WINAPI cwf_SetViewId(IUnknown *iface, DWORD id)
{
    factory_from_IConfigure(iface)->view_id = id;
    blog("IConfigureWindowFactory::SetViewId %lu -> S_OK", id);
    return S_OK;
}
static const struct configure_window_factory_vtbl factory_configure_vtbl = { cwf_QueryInterface, cwf_AddRef, cwf_Release,
    cwf_Initialize, cwf_InitializeWithPosition, cwf_CreateSplashScreen, cwf_GetAppWindow, cwf_HasExistingSplashScreen, cwf_SetViewId };

/* ICoreWindowFactory {CD292360-2763-4085-8A9F-74B224A29175}, server side. The app's
 * CImmersiveWindowFactoryProxy implements both methods locally; the server object must still
 * answer the QI so the proxy can be re-marshaled across apartments (CoreApplicationView::
 * CreateCoreWindow). Semantics measured from native CImmersiveWindowFactoryBase (twinapi.appcore
 * 0xf5e90, 0x15f7b0): CreateCoreWindow clears *window and returns E_NOTIMPL; get_WindowReuseAllowed
 * returns E_POINTER for NULL, otherwise the reuse flag (this host never reuses windows). */
struct core_window_factory_vtbl
{
    HRESULT (WINAPI *QueryInterface)(IUnknown *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IUnknown *);
    ULONG (WINAPI *Release)(IUnknown *);
    HRESULT (WINAPI *GetIids)(IUnknown *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IUnknown *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IUnknown *, TrustLevel *);
    HRESULT (WINAPI *CreateCoreWindow)(IUnknown *, HSTRING title, IUnknown **window);
    HRESULT (WINAPI *get_WindowReuseAllowed)(IUnknown *, BOOLEAN *value);
};
static struct factory *factory_from_ICoreWindowFactory(IUnknown *i) { return CONTAINING_RECORD(i, struct factory, ICoreWindowFactory_iface); }
static HRESULT WINAPI ccwf_QueryInterface(IUnknown *iface, REFIID riid, void **out) { return factory_QueryInterface(&factory_from_ICoreWindowFactory(iface)->IUnknown_iface, riid, out); }
static ULONG WINAPI ccwf_AddRef(IUnknown *iface) { return factory_AddRef(&factory_from_ICoreWindowFactory(iface)->IUnknown_iface); }
static ULONG WINAPI ccwf_Release(IUnknown *iface) { return factory_Release(&factory_from_ICoreWindowFactory(iface)->IUnknown_iface); }
static HRESULT WINAPI ccwf_GetIids(IUnknown *iface, ULONG *count, IID **iids) { return factory_GetIids(&factory_from_ICoreWindowFactory(iface)->IUnknown_iface, count, iids); }
static HRESULT WINAPI ccwf_GetRuntimeClassName(IUnknown *iface, HSTRING *name) { return factory_GetRuntimeClassName(&factory_from_ICoreWindowFactory(iface)->IUnknown_iface, name); }
static HRESULT WINAPI ccwf_GetTrustLevel(IUnknown *iface, TrustLevel *level) { return factory_GetTrustLevel(&factory_from_ICoreWindowFactory(iface)->IUnknown_iface, level); }
static HRESULT WINAPI ccwf_CreateCoreWindow(IUnknown *iface, HSTRING title, IUnknown **window)
{
    if (window) *window = NULL;
    blog("ICoreWindowFactory::CreateCoreWindow (server) -> E_NOTIMPL");
    return E_NOTIMPL;
}
static HRESULT WINAPI ccwf_get_WindowReuseAllowed(IUnknown *iface, BOOLEAN *value)
{
    if (!value) return E_POINTER;
    *value = FALSE;
    blog("ICoreWindowFactory::get_WindowReuseAllowed (server) -> S_OK FALSE");
    return S_OK;
}
static const struct core_window_factory_vtbl factory_core_window_vtbl = { ccwf_QueryInterface, ccwf_AddRef, ccwf_Release,
    ccwf_GetIids, ccwf_GetRuntimeClassName, ccwf_GetTrustLevel, ccwf_CreateCoreWindow, ccwf_get_WindowReuseAllowed };

static HRESULT get_std(struct factory *f, IMarshal **m)
{
    HRESULT hr;
    if (!f->std_inner && FAILED(hr = CoGetStdMarshalEx((IUnknown *)&f->IUnknown_iface, SMEXF_SERVER, &f->std_inner)))
    {
        blog("CoGetStdMarshalEx(SMEXF_SERVER) -> %#lx", hr);
        return hr;
    }
    return IUnknown_QueryInterface(f->std_inner, &IID_IMarshal, (void **)m);
}

#define EXTRA_SIZE 29u

static void write_extra(BYTE *p)
{
    RECT rc = { 0, 0, width, height };
    UINT32 type = window_type, nav = 0;
    memcpy(p, &rc, 16); p += 16;
    memcpy(p, &type, 4); p += 4;
    *p++ = 0;   /* navigation-client flag */
    *p++ = 0;
    *p++ = 0;   /* no rotation preference */
    *p++ = 0;   /* no ImmersiveApplication property */
    memcpy(p, &nav, 4); p += 4;
    *p++ = 0;
}

static HRESULT WINAPI fm_QueryInterface(IMarshal *iface, REFIID riid, void **out) { return factory_QueryInterface(&factory_from_IMarshal(iface)->IUnknown_iface, riid, out); }
static ULONG WINAPI fm_AddRef(IMarshal *iface) { return factory_AddRef(&factory_from_IMarshal(iface)->IUnknown_iface); }
static ULONG WINAPI fm_Release(IMarshal *iface) { return factory_Release(&factory_from_IMarshal(iface)->IUnknown_iface); }

static HRESULT WINAPI fm_GetUnmarshalClass(IMarshal *iface, REFIID riid, void *pv, DWORD ctx, void *ctxdata, DWORD flags, CLSID *clsid)
{
    /* Native CWrlLightweightHandlerBase::GetUnmarshalClass delegates to the SMEXF_SERVER std marshaler,
     * which asks IStdMarshalInfo for the handler class and reports CLSID_AggStdMarshal. */
    struct factory *f = factory_from_IMarshal(iface);
    IMarshal *m;
    HRESULT hr;
    char g[40];
    if (FAILED(hr = get_std(f, &m))) return hr;
    hr = IMarshal_GetUnmarshalClass(m, riid, pv, ctx, ctxdata, flags, clsid);
    IMarshal_Release(m);
    blog("factory GetUnmarshalClass ctx=%lu flags=%#lx -> %#lx %s", ctx, flags, hr, SUCCEEDED(hr) ? guidstr(clsid, g) : "");
    return hr;
}

static HRESULT WINAPI fm_GetMarshalSizeMax(IMarshal *iface, REFIID riid, void *pv, DWORD ctx, void *ctxdata, DWORD flags, DWORD *size)
{
    struct factory *f = factory_from_IMarshal(iface);
    IMarshal *m;
    HRESULT hr;
    if (FAILED(hr = get_std(f, &m))) return hr;
    hr = IMarshal_GetMarshalSizeMax(m, riid, pv, ctx, ctxdata, flags, size);
    IMarshal_Release(m);
    if (SUCCEEDED(hr)) *size += 4 + EXTRA_SIZE;
    blog("factory GetMarshalSizeMax -> %#lx size=%lu", hr, SUCCEEDED(hr) ? *size : 0);
    return hr;
}

static HRESULT WINAPI fm_MarshalInterface(IMarshal *iface, IStream *stream, REFIID riid, void *pv, DWORD ctx, void *ctxdata, DWORD flags)
{
    struct factory *f = factory_from_IMarshal(iface);
    BYTE extra[EXTRA_SIZE];
    UINT32 size = EXTRA_SIZE;
    IMarshal *m;
    HRESULT hr;
    char g[40];
    if (FAILED(hr = get_std(f, &m))) return hr;
    hr = IMarshal_MarshalInterface(m, stream, riid, pv, ctx, ctxdata, flags);
    IMarshal_Release(m);
    blog("factory std MarshalInterface riid=%s ctx=%lu flags=%#lx -> %#lx", guidstr(riid, g), ctx, flags, hr);
    if (FAILED(hr)) return hr;
    write_extra(extra);
    if (FAILED(hr = IStream_Write(stream, &size, 4, NULL))) return hr;
    hr = IStream_Write(stream, extra, EXTRA_SIZE, NULL);
    blog("factory additional data rect=0,0,%ld,%ld type=%ld nav=0 rot=none prop=none -> %#lx", width, height, window_type, hr);
    return hr;
}

static HRESULT WINAPI fm_UnmarshalInterface(IMarshal *iface, IStream *stream, REFIID riid, void **out) { return E_NOTIMPL; }

static HRESULT WINAPI fm_ReleaseMarshalData(IMarshal *iface, IStream *stream)
{
    struct factory *f = factory_from_IMarshal(iface);
    IMarshal *m;
    HRESULT hr;
    if (FAILED(hr = get_std(f, &m))) return hr;
    hr = IMarshal_ReleaseMarshalData(m, stream);
    IMarshal_Release(m);
    return hr;
}

static HRESULT WINAPI fm_DisconnectObject(IMarshal *iface, DWORD reserved)
{
    struct factory *f = factory_from_IMarshal(iface);
    IMarshal *m;
    HRESULT hr;
    if (FAILED(hr = get_std(f, &m))) return hr;
    hr = IMarshal_DisconnectObject(m, reserved);
    IMarshal_Release(m);
    return hr;
}

static const IMarshalVtbl factory_marshal_vtbl =
{
    fm_QueryInterface, fm_AddRef, fm_Release, fm_GetUnmarshalClass, fm_GetMarshalSizeMax,
    fm_MarshalInterface, fm_UnmarshalInterface, fm_ReleaseMarshalData, fm_DisconnectObject,
};

/* ---- generic single-interface object with numbered unimplemented slots ---- */
#define NSLOTS 16
struct object
{
    void **vtbl;
    LONG ref;
    const IID *iid;
    const char *name;
};

static HRESULT WINAPI obj_QueryInterface(struct object *o, REFIID riid, void **out)
{
    char g[40];
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, o->iid))
    {
        *out = o; InterlockedIncrement(&o->ref);
        return S_OK;
    }
    *out = NULL;
    blog("%s QI %s -> E_NOINTERFACE", o->name, guidstr(riid, g));
    return E_NOINTERFACE;
}
static ULONG WINAPI obj_AddRef(struct object *o) { return InterlockedIncrement(&o->ref); }
static ULONG WINAPI obj_Release(struct object *o) { return InterlockedDecrement(&o->ref); }

#define NOTIMPL(n) static HRESULT WINAPI notimpl_##n(struct object *o) { blog("%s slot %d not implemented -> E_NOTIMPL", o->name, n); return E_NOTIMPL; }
NOTIMPL(3) NOTIMPL(4) NOTIMPL(5) NOTIMPL(6) NOTIMPL(7) NOTIMPL(8) NOTIMPL(9) NOTIMPL(10)
NOTIMPL(11) NOTIMPL(12) NOTIMPL(13) NOTIMPL(14) NOTIMPL(15)

static struct object broker;

/* IServiceHostBrokerProvider slot 3: QueryService-shaped (guidService, riid, ppv) */
static HRESULT WINAPI provider_QueryService(struct object *o, REFGUID service, REFIID riid, void **out)
{
    char g1[40], g2[40];
    HRESULT hr;
    *out = NULL;
    if (IsEqualGUID(service, &IID_ApplicationActivationBroker))
        hr = obj_QueryInterface(&broker, riid, out);
    else
        hr = E_NOINTERFACE;
    blog("provider slot3 service=%s riid=%s -> %#lx", guidstr(service, g1), guidstr(riid, g2), hr);
    return hr;
}

/* IApplicationActivationBroker slot 5: (UINT64 aamId, out 32-byte results, out IUnknown **factory) */
static HRESULT WINAPI broker_GetWindowFactory(struct object *o, UINT64 id, BYTE *results, IUnknown **out)
{
    struct factory *f;
    blog("broker slot5 aamId=%#llx results=%p out=%p", (unsigned long long)id, results, out);
    if (results) memset(results, 0, 32);
    if (!out) return E_POINTER;
    if (!(f = calloc(1, sizeof(*f)))) return E_OUTOFMEMORY;
    f->IUnknown_iface.lpVtbl = &factory_vtbl;
    f->IMarshal_iface.lpVtbl = &factory_marshal_vtbl;
    f->IStdMarshalInfo_iface.lpVtbl = &factory_stdinfo_vtbl;
    f->IConfigureWindowFactory_iface.lpVtbl = (const IUnknownVtbl *)&factory_configure_vtbl;
    f->ICoreWindowFactory_iface.lpVtbl = (const IUnknownVtbl *)&factory_core_window_vtbl;
    f->ref = 1;
    *out = (IUnknown *)&f->IUnknown_iface;
    blog("broker slot5 -> S_OK factory=%p", f);
    return S_OK;
}

static void *provider_vtbl[NSLOTS], *broker_vtbl[NSLOTS];
static struct object provider = { provider_vtbl, 1, &IID_ServiceHostBrokerProvider, "provider" };
static struct object broker = { broker_vtbl, 1, &IID_ApplicationActivationBroker, "broker" };

static void fill(void **v)
{
    void *ni[NSLOTS] = { obj_QueryInterface, obj_AddRef, obj_Release, notimpl_3, notimpl_4, notimpl_5, notimpl_6,
        notimpl_7, notimpl_8, notimpl_9, notimpl_10, notimpl_11, notimpl_12, notimpl_13, notimpl_14, notimpl_15 };
    memcpy(v, ni, sizeof(ni));
}

/* ---- class factory ---- */
static HRESULT WINAPI cf_QueryInterface(IClassFactory *iface, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory)) { *out = iface; return S_OK; }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cf_AddRef(IClassFactory *iface) { return 2; }
static ULONG WINAPI cf_Release(IClassFactory *iface) { return 1; }
static HRESULT WINAPI cf_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID riid, void **out)
{
    char g[40];
    HRESULT hr = outer ? CLASS_E_NOAGGREGATION : obj_QueryInterface(&provider, riid, out);
    blog("CreateInstance riid=%s -> %#lx", guidstr(riid, g), hr);
    return hr;
}
static HRESULT WINAPI cf_LockServer(IClassFactory *iface, BOOL lock) { return S_OK; }
static const IClassFactoryVtbl cf_vtbl = { cf_QueryInterface, cf_AddRef, cf_Release, cf_CreateInstance, cf_LockServer };
static IClassFactory cf = { &cf_vtbl };

int main(int argc, char **argv)
{
    int seconds = argc > 1 ? atoi(argv[1]) : 240;
    DWORD cookie;
    HRESULT hr;

    if (argc > 2) width = atol(argv[2]);
    if (argc > 3) height = atol(argv[3]);
    if (argc > 4) window_type = atol(argv[4]);
    fill(provider_vtbl);
    fill(broker_vtbl);
    provider_vtbl[3] = provider_QueryService;
    broker_vtbl[5] = broker_GetWindowFactory;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    blog("start pid=%lu CoInitializeEx -> %#lx seconds=%d", GetCurrentProcessId(), hr, seconds);
    hr = CoRegisterClassObject(&CLSID_Broker, (IUnknown *)&cf, CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE, &cookie);
    blog("CoRegisterClassObject {3480A401-BDE9-4407-BC02-798A866AC051} -> %#lx", hr);
    if (FAILED(hr)) return 1;
    Sleep(seconds * 1000);
    CoRevokeClassObject(cookie);
    blog("exit");
    CoUninitialize();
    return 0;
}
