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
 * usage: shellhost-broker.exe [seconds=240] [width=1280] [height=800] [window_type=6]
 *                             [factory=coreui|twinapi] [aumid] [contract=Windows.Launch] [viewId|-] [flags=0x140]
 *                             [nav|nonav|shellvm]
 *
 * shellvm: nav plus a ShellViewManager client that navigates new views at level 0 (see shellvm_thread).
 * nav (default): also host the genuine CoreUIComponents navigation server the way sihost.exe does (see
 * start_nav_server below); nonav reproduces br43-br60, where no process served it.
 *
 * factory=coreui (default): like native ActivationManager (ViewActivator::CreateCoreWindowFactory, measured live
 * in sihost on 26100), slot 5 returns CoreUIComponents' own CoreWindowFactory from
 * CoreUICreateICoreWindowFactoryEx(aumid, GUID 0, id, contract, "", h1, h2, flags) (MsgString arguments). Native passed two equal
 * handle-like values (h1 = h2) whose meaning was not resolved; this host passes 0. The factory marshals itself
 * (handler {B243A9FD-C57A-4D3E-A7CF-21CAED64CB5A}) and its proxy attaches the CoreUI NavigationClient.
 * factory=twinapi: the earlier host factory below (br35-br42), kept for reproduction. */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <inspectable.h>
#include <hstring.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

static const CLSID CLSID_Broker = {0x3480a401,0xbde9,0x4407,{0xbc,0x02,0x79,0x8a,0x86,0x6a,0xc0,0x51}};
static const IID IID_ServiceHostBrokerProvider = {0x0f4accb1,0xd8f9,0x4011,{0xba,0x37,0x25,0x57,0x92,0x5a,0x78,0xcf}};
static const IID IID_ApplicationActivationBroker = {0xd98fd14a,0x522a,0x4d59,{0xb8,0x75,0x81,0x1e,0x83,0x91,0x9a,0x9e}};
static const CLSID CLSID_WindowFactoryProxy = {0x14de3806,0x5d5b,0x405c,{0xab,0x89,0x4a,0xc9,0x36,0xbc,0xbf,0x48}};
/* Windows.Internal.ApplicationModel.WindowingEnvironment.IPresenterBroker: CoreUIComponents
 * NavigationClientPresenterClientAdapter::RuntimeClassInitialize asks the ShellServiceHostBrokerProvider for it
 * (service == riid) and fails NavigationClient init without it. Native server: WindowManagement.dll PresenterBroker;
 * slots 6 TryApplyAsync, 7 RevertAsync, 8 GetAppliedPresenter(WindowId, &kind) resolve the WindowId in the shell's
 * WindowManagement data model, which this broker does not have; they stay E_NOTIMPL until measured callers need them. */
static const IID IID_PresenterBroker = {0x4d79a826,0xbc52,0x4374,{0x9f,0xa2,0x5c,0x9a,0x5a,0x68,0x44,0x32}};
static const IID IID_ConfigureWindowFactory = {0x601d51e3,0x801e,0x49c9,{0xbb,0xfa,0xfe,0x29,0xa6,0x62,0xae,0xad}};
static const IID IID_Inspectable = {0xaf86e2e0,0xb12d,0x4c6a,{0x9c,0x5a,0xd7,0xaa,0x65,0x10,0x1e,0x90}};
static const IID IID_CoreWindowFactory = {0xcd292360,0x2763,0x4085,{0x8a,0x9f,0x74,0xb2,0x24,0xa2,0x91,0x75}};

static LONG width = 1280, height = 800, window_type = 6;
/* CoreUI factory inputs: defaults are the values measured natively in sihost
 * (ActivationManager ViewActivator::CreateCoreWindowFactory -> CoreUICreateICoreWindowFactoryEx). */
static BOOL use_coreui = TRUE;
static WCHAR coreui_aumid[256] = L"Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.AppL";
static WCHAR coreui_contract[128] = L"Windows.Launch";
static DWORD coreui_flags = 0x140;
static UINT64 coreui_view_id = ~(UINT64)0;

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
    IUnknown *inner;
    HRESULT hr;
    if (!f->std_inner)
    {
        if (FAILED(hr = CoGetStdMarshalEx((IUnknown *)&f->IUnknown_iface, SMEXF_SERVER, &inner)))
        {
            blog("CoGetStdMarshalEx(SMEXF_SERVER) -> %#lx", hr);
            return hr;
        }
        /* concurrent MTA marshal calls: keep the first inner marshaler, drop ours */
        if (InterlockedCompareExchangePointer((void **)&f->std_inner, inner, NULL))
            IUnknown_Release(inner);
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

static struct object broker, presenter_broker;

/* IServiceHostBrokerProvider slot 3: QueryService-shaped (guidService, riid, ppv) */
static HRESULT WINAPI provider_QueryService(struct object *o, REFGUID service, REFIID riid, void **out)
{
    char g1[40], g2[40];
    HRESULT hr;
    *out = NULL;
    if (IsEqualGUID(service, &IID_ApplicationActivationBroker))
        hr = obj_QueryInterface(&broker, riid, out);
    else if (IsEqualGUID(service, &IID_PresenterBroker))
        hr = obj_QueryInterface(&presenter_broker, riid, out);
    else
        hr = E_NOINTERFACE;
    blog("provider slot3 service=%s riid=%s -> %#lx", guidstr(service, g1), guidstr(riid, g2), hr);
    return hr;
}

/* IApplicationActivationBroker slot 5: (UINT64 aamId, out 32-byte results, out IUnknown **factory)
 * The layout of the 32-byte results block was not measured natively; it is zero-filled as an explicit
 * host assumption (recorded in README "Recorded deviations"), not a reproduction of native content. */
/* CoreUICreateICoreWindowFactoryEx takes CoreMessaging MsgString handles (CoreUIComponents MsgStringGetData), not
 * HSTRINGs; native ActivationManager creates them with CoreMessaging!MsgStringCreateShared(str, -1, &out) and frees
 * them with MsgRelease after the call. */
static HRESULT coreui_factory(UINT64 id, IUnknown **out)
{
    typedef HRESULT (WINAPI *msg_create_fn)(const WCHAR *, INT32, void **);
    typedef void (WINAPI *msg_release_fn)(void *);
    typedef HRESULT (WINAPI *create_ex_fn)(void *, const GUID *, UINT64, void *, void *, UINT64, UINT64,
                                           DWORD, IUnknown **);
    typedef HRESULT (WINAPI *set_view_fn)(IUnknown *, UINT64);
    static const IID IID_CoreWindowFactoryViewConfig =
        {0x118b4ce1,0xee64,0x4f90,{0xb4,0xc2,0x45,0x28,0x0a,0xfb,0xf3,0x8b}};
    HMODULE msg = LoadLibraryW(L"CoreMessaging.dll"), coreui = LoadLibraryW(L"CoreUIComponents.dll");
    msg_create_fn msg_create = msg ? (void *)GetProcAddress(msg, "MsgStringCreateShared") : NULL;
    msg_release_fn msg_release = msg ? (void *)GetProcAddress(msg, "MsgRelease") : NULL;
    create_ex_fn create_ex = coreui ? (void *)GetProcAddress(coreui, "CoreUICreateICoreWindowFactoryEx") : NULL;
    void *aumid = NULL, *contract = NULL, *empty = NULL;
    IUnknown *config = NULL;
    GUID zero = {0};
    HRESULT hr;

    *out = NULL;
    if (!msg_create || !msg_release || !create_ex)
    {
        blog("coreui: CoreMessaging=%p CoreUIComponents=%p MsgStringCreateShared=%p CoreUICreateICoreWindowFactoryEx=%p (err %lu)",
             msg, coreui, msg_create, create_ex, GetLastError());
        return E_NOTIMPL;
    }
    if (FAILED(hr = msg_create(coreui_aumid, -1, &aumid)) || FAILED(hr = msg_create(coreui_contract, -1, &contract))
        || FAILED(hr = msg_create(L"", -1, &empty)))
    {
        blog("MsgStringCreateShared -> %#lx", hr);
        goto done;
    }
    hr = create_ex(aumid, &zero, id, contract, empty, 0, 0, coreui_flags, out);
    blog("CoreUICreateICoreWindowFactoryEx(aumid=%ls, guid=0, id=%#llx, contract=%ls, \"\", 0, 0, flags=%#lx) -> %#lx factory=%p",
         coreui_aumid, (unsigned long long)id, coreui_contract, coreui_flags, hr, *out);
    if (SUCCEEDED(hr) && coreui_view_id != ~(UINT64)0)
    {
        HRESULT hr2 = IUnknown_QueryInterface(*out, &IID_CoreWindowFactoryViewConfig, (void **)&config);
        if (SUCCEEDED(hr2))
        {
            hr2 = ((set_view_fn)(*(void ***)config)[3])(config, coreui_view_id);
            IUnknown_Release(config);
        }
        blog("factory {118B4CE1} vtbl[3](viewId=%#llx) -> %#lx", (unsigned long long)coreui_view_id, hr2);
        if (FAILED(hr2)) { IUnknown_Release(*out); *out = NULL; hr = hr2; }
    }
done:
    if (aumid) msg_release(aumid);
    if (contract) msg_release(contract);
    if (empty) msg_release(empty);
    return hr;
}

static HRESULT WINAPI broker_GetWindowFactory(struct object *o, UINT64 id, BYTE *results, IUnknown **out)
{
    struct factory *f;
    blog("broker slot5 aamId=%#llx results=%p out=%p", (unsigned long long)id, results, out);
    if (results) memset(results, 0, 32);
    if (!out) return E_POINTER;
    if (use_coreui)
    {
        HRESULT hr = coreui_factory(id, out);
        blog("broker slot5 (CoreUI factory) -> %#lx factory=%p", hr, *out);
        return hr;
    }
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

static void *provider_vtbl[NSLOTS], *broker_vtbl[NSLOTS], *presenter_broker_vtbl[NSLOTS];
static struct object provider = { provider_vtbl, 1, &IID_ServiceHostBrokerProvider, "provider" };
static struct object broker = { broker_vtbl, 1, &IID_ApplicationActivationBroker, "broker" };
static struct object presenter_broker = { presenter_broker_vtbl, 1, &IID_PresenterBroker, "presenterbroker" };

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

/* ---- CoreUI navigation server ----
 * Native Windows 11 26100 hosts the CoreUI navigation server ("System\NavigationServer_ClientViewCreator", which
 * CoreUIComponents!CreateServerTaskEx looks up through the CoreMessaging registrar from the app's
 * CoreWindowFactoryProxy::CreateCoreWindow) inside sihost.exe. Measured sihost.exe 10.0.26100.9278 sequence
 * (CNavigationServerComponent::v_PublishServices / NavigationServerThread):
 *   CreateEventW(SDDL "D:(A;;GA;;;SY)(A;;0x001F0003;;;WD)(A;;0x001F0003;;;AC)", manual reset, unsignaled,
 *                L"NavigationServer Started") and the same for L"Stop NavigationServer";
 *   CreateThread -> SetThreadPriority(THREAD_PRIORITY_HIGHEST), CoreUICreate(&p) [CoreMessaging], CoreUIFactoryCreate(&p),
 *                   CoreUIServerCreate(&server), server->vtbl[3] (IExportServerFactory::RunNavigationServer)
 *                   (L"NavigationServer Started", L"Stop NavigationServer"), which runs until the stop event is set;
 *   WaitForMultipleObjects({started, thread}, any, INFINITE).
 * All of the server logic is the genuine CoreUIComponents.dll; this only reproduces the host sequence. */
static HANDLE nav_started, nav_stop, nav_thread;

static DWORD WINAPI nav_server_thread(void *arg)
{
    typedef HRESULT (WINAPI *create_fn)(IUnknown **);
    typedef HRESULT (WINAPI *run_fn)(IUnknown *, const WCHAR *, const WCHAR *);
    /* sihost imports CoreUICreate from CoreMessaging.dll and the other two from CoreUIComponents.dll. */
    HMODULE msg = LoadLibraryW(L"CoreMessaging.dll"), coreui = LoadLibraryW(L"CoreUIComponents.dll");
    create_fn coreui_create = msg ? (void *)GetProcAddress(msg, "CoreUICreate") : NULL;
    create_fn factory_create = coreui ? (void *)GetProcAddress(coreui, "CoreUIFactoryCreate") : NULL;
    create_fn server_create = coreui ? (void *)GetProcAddress(coreui, "CoreUIServerCreate") : NULL;
    IUnknown *core = NULL, *factory = NULL, *server = NULL;
    HRESULT hr;

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    if (!coreui_create || !factory_create || !server_create)
    {
        blog("navserver: CoreMessaging=%p CoreUIComponents=%p CoreUICreate=%p CoreUIFactoryCreate=%p CoreUIServerCreate=%p (err %lu)",
             msg, coreui, coreui_create, factory_create, server_create, GetLastError());
        return E_NOTIMPL;
    }
    if (FAILED(hr = coreui_create(&core))) { blog("navserver: CoreUICreate -> %#lx", hr); goto done; }
    if (FAILED(hr = factory_create(&factory))) { blog("navserver: CoreUIFactoryCreate -> %#lx", hr); goto done; }
    if (FAILED(hr = server_create(&server))) { blog("navserver: CoreUIServerCreate -> %#lx", hr); goto done; }
    blog("navserver: RunNavigationServer start");
    hr = ((run_fn)(*(void ***)server)[3])(server, L"NavigationServer Started", L"Stop NavigationServer");
    blog("navserver: RunNavigationServer -> %#lx", hr);
done:
    if (server) IUnknown_Release(server);
    if (factory) IUnknown_Release(factory);
    if (core) IUnknown_Release(core);
    return hr;
}

static void start_nav_server(void)
{
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, FALSE };
    HANDLE wait[2];
    DWORD ret, code = 0;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;GA;;;SY)(A;;0x001F0003;;;WD)(A;;0x001F0003;;;AC)",
                                                              SDDL_REVISION_1, &sa.lpSecurityDescriptor, NULL))
    {
        blog("navserver: SDDL conversion failed %lu", GetLastError());
        return;
    }
    nav_started = CreateEventW(&sa, TRUE, FALSE, L"NavigationServer Started");
    nav_stop = CreateEventW(&sa, TRUE, FALSE, L"Stop NavigationServer");
    LocalFree(sa.lpSecurityDescriptor);
    if (!nav_started || !nav_stop)
    {
        blog("navserver: CreateEventW failed %lu", GetLastError());
        return;
    }
    if (!(nav_thread = CreateThread(NULL, 0, nav_server_thread, NULL, 0, NULL)))
    {
        blog("navserver: CreateThread failed %lu", GetLastError());
        return;
    }
    wait[0] = nav_started;
    wait[1] = nav_thread;
    ret = WaitForMultipleObjects(2, wait, FALSE, 60000);
    if (ret == WAIT_OBJECT_0 + 1) GetExitCodeThread(nav_thread, &code);
    blog("navserver: wait -> %lu (0=started, 1=thread exited code %#lx, %lu=timeout)", ret, code, WAIT_TIMEOUT);
}

/* ---- ShellViewManager client (nav mode "shellvm") ----
 * Under ViewManager phaseout (TRUE for the Desktop device family in CoreUIComponents and WindowManagement), the
 * navigation server creates the app's task on CreateView but does not navigate it; ViewNavigationLevel (s_EnumNames 0x1e8f00) is
 * 0 Closed, 1 Inactive, 2 Idle, 3 Obscured, 4 Visible, 5 Active; SessionLayer::NavigateToView (0x3294c) maps Active to
 * state 5 and Session::BeginActivation, while Closed maps to state 10 and CloseSessionInternal (br95:
 * OnNavigateAwayFromView right after a level-0 navigate).
 * Natively the navigating actor is explorer's WindowManagement.dll ViewManagerBridge (Connect 0x47c18, OnConnected
 * 0x480a0, 26100): CoreUIFactoryCreate; FindTypeID({490C6BC3}) ; CreateMessageProxy(
 * L"System\\NavigationServer_ShellViewManager", type) ; AddProxyListener ; QI {490C6BC3}; on OnConnected
 * AddEventListener, GetViews and AddListListener, then IShellViewManager::NavigateToView(viewId, level, direction 0,
 * animation 1) from its data model. This reproduces only the connect and the navigate: it polls GetViews instead of a
 * list listener (recorded deviation) and navigates each newly listed view once to Active. On OnConnected it also
 * registers a logging IRemoteShellViewManagerListener (AddEventListener, as the bridge does) and logs GetActiveView
 * before and after the navigate (G0 discriminator).
 * The native trigger for the
 * navigate inside WindowManagement was not pinned down. All server logic is the genuine CoreUIComponents.dll. */
static const IID IID_ShellViewManager = {0x490c6bc3,0x8ab2,0x4528,{0xb9,0x74,0x70,0x9a,0x9e,0xf2,0xd7,0x2d}};
static const IID IID_RemoteShellView = {0x884eb994,0x3fdf,0x40ff,{0x88,0x00,0xaa,0x11,0x76,0xf0,0xf4,0x5e}};
static HANDLE svm_connected;

static HRESULT WINAPI pl_QueryInterface(IUnknown *iface, REFIID riid, void **out)
{
    char g[40];
    if (IsEqualIID(riid, &IID_IUnknown)) { *out = iface; return S_OK; }
    blog("shellvm: listener QI %s -> E_NOINTERFACE", guidstr(riid, g));
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI pl_AddRef(IUnknown *iface) { return 2; }
static ULONG WINAPI pl_Release(IUnknown *iface) { return 1; }
/* ICallbackMessageProxyListener$X__ComVTable slots 3-5, from the CallbackAdapter dispatch (CoreUI 0x166e8 -> +0x18,
 * 0x1764c -> +0x20, OnPropertyChanged third). */
static void WINAPI pl_OnConnected(IUnknown *iface, void *proxy)
{
    blog("shellvm: OnConnected proxy=%p", proxy);
    SetEvent(svm_connected);
}
static void WINAPI pl_OnDisconnected(IUnknown *iface, void *proxy) { blog("shellvm: OnDisconnected proxy=%p", proxy); }
static void WINAPI pl_OnPropertyChanged(IUnknown *iface, void *proxy, USHORT id) { blog("shellvm: OnPropertyChanged %u", id); }
static void *pl_vtbl[] = { pl_QueryInterface, pl_AddRef, pl_Release, pl_OnConnected, pl_OnDisconnected, pl_OnPropertyChanged };
static IUnknown proxy_listener = { (IUnknownVtbl *)pl_vtbl };

#define VCALL(obj, slot, type) ((type)(*(void ***)(obj))[slot])

/* G0 discriminator: logging IRemoteShellViewManagerListener {89263C96-9582-4156-99EE-6252CE508EBC} (CoreUIComponents
 * s_defType 0x224ea0, 0x15 methods). The proxy's AddEventListener (slot 9, 0x5d0f0) takes (listener, proxied interface type id),
 * checks the type against the proxy's interface and maps it to the event (listener) type, AddRefs the listener through slot 1 and wraps it in a CallbackAdapter
 * that calls the COM slots directly (no QI): e.g. OnNavigateToViewFailed adapter 0x102904 calls +0x60 (slot 12) with
 * (this, ViewInstanceId, int reason). Each slot returns HRESULT; all slots log their first three register arguments. */
static const IID IID_RemoteShellViewManagerListener = {0x89263c96,0x9582,0x4156,{0x99,0xee,0x62,0x52,0xce,0x50,0x8e,0xbc}};
static const char * const svml_names[24] = { NULL, NULL, NULL,
    "OnRequestNavigateToView", "OnRequestBeginPresentView", "OnRequestEndPresentView", "OnNavigateAwayFromView",
    "OnServerWindowCreated", "OnShutdownComplete", "OnSystemKeyPressedComplete", "OnUserLogoffComplete",
    "OnViewLimitReached", "OnNavigateToViewFailed", "OnRequestShowStandardSystemOverlays", "OnCloseRequestedComplete",
    "OnRequestConsolidateView", "OnUIAConnectComplete", "OnSystemKeyClientPressed", "OnRequestShowStatusBar",
    "OnRequestHideStatusBar", "OnRequestClearPersistedState", "OnLayoutCompleted", "OnRequestHideWindow",
    "OnTrySetTopMost" };
/* OnRequestBeginPresentView(ViewInstanceId, bool) / OnRequestEndPresentView(ViewInstanceId, bool, ...) are requests
 * to the shell: ViewManager::NotifyRequestBeginPresentView (0x6cb64) starts a navigation timeout, and the shell answers
 * with IShellViewManager::BeginPresentView / EndPresentView (ExportAdapter slots 17/18, same view and bool), which
 * ServerTask::BeginPresentView (0x50738) records before stopping the timeout and resuming the task state machine.
 * Answered from the poll thread, outside the callback. */
static struct { volatile LONG pending; UINT32 view; BOOLEAN flag; } svm_present[2];
static HRESULT svml_event(int slot, UINT_PTR a, UINT_PTR b, UINT_PTR c)
{
    blog("shellvm: listener %s (slot %d) a=%#x b=%#Ix c=%#Ix", svml_names[slot], slot, (UINT32)a, b, c);
    if (slot == 4 || slot == 5)
    {
        svm_present[slot - 4].view = (UINT32)a;
        svm_present[slot - 4].flag = (BOOLEAN)(b & 0xff);
        InterlockedExchange(&svm_present[slot - 4].pending, 1);
    }
    return S_OK;
}
#define SVML(n) static HRESULT WINAPI svml_##n(IUnknown *iface, UINT_PTR a, UINT_PTR b, UINT_PTR c) { return svml_event(n, a, b, c); }
SVML(3) SVML(4) SVML(5) SVML(6) SVML(7) SVML(8) SVML(9) SVML(10) SVML(11) SVML(12) SVML(13)
SVML(14) SVML(15) SVML(16) SVML(17) SVML(18) SVML(19) SVML(20) SVML(21) SVML(22) SVML(23)
static void *svml_vtbl[] = { pl_QueryInterface, pl_AddRef, pl_Release, svml_3, svml_4, svml_5, svml_6, svml_7,
    svml_8, svml_9, svml_10, svml_11, svml_12, svml_13, svml_14, svml_15, svml_16, svml_17, svml_18, svml_19, svml_20,
    svml_21, svml_22, svml_23 };
static IUnknown svm_listener = { (IUnknownVtbl *)svml_vtbl };

static void svm_log_active_view(IUnknown *svm, const char *when)
{
    IUnknown *view = NULL;
    UINT32 id = 0, pid = 0, level = 0;
    HRESULT hr = VCALL(svm, 3, HRESULT (WINAPI *)(IUnknown *, IUnknown **))(svm, &view);
    if (SUCCEEDED(hr) && view)
    {
        VCALL(view, 19, HRESULT (WINAPI *)(IUnknown *, UINT32 *))(view, &id);
        VCALL(view, 12, HRESULT (WINAPI *)(IUnknown *, UINT32 *))(view, &pid);
        VCALL(view, 9, HRESULT (WINAPI *)(IUnknown *, UINT32 *))(view, &level);
        IUnknown_Release(view);
    }
    blog("shellvm: GetActiveView (%s) -> %#lx view=%p id=%#x pid=%u level=%u", when, hr, view, id, pid, level);
}

/* DIAGNOSTIC ONLY (read-only, not part of the candidate): the navigation server lives in this process, so locate
 * ServerTask objects by their vtable (??_7ServerTask 0x1b82d8, CoreUIComponents 26100.9278) and log the fields
 * the state machine gates on: +0x2f8 connected IRemoteTask proxy (ConnectNavigationTask 0x3349c), +0x3b8 current
 * state (SetCurrentState 0x396bc), +0x3bc target state (SetTargetState 0x373ce), +0x3c4 navigation level,
 * +0x3e9/+0x3eb BeginPresentView flags (0x50738). */
static void svm_diag_tasks(const char *when)
{
    HMODULE coreui = GetModuleHandleW(L"CoreUIComponents.dll");
    UINT_PTR vt = (UINT_PTR)coreui + 0x1b82d8;
    MEMORY_BASIC_INFORMATION mbi;
    BYTE *p = NULL;
    int found = 0;
    if (!coreui) return;
    while (VirtualQuery(p, &mbi, sizeof(mbi)) && found < 8)
    {
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE && mbi.Protect == PAGE_READWRITE)
        {
            UINT_PTR *q = mbi.BaseAddress, *e = (UINT_PTR *)((BYTE *)mbi.BaseAddress + mbi.RegionSize);
            for (; q + 0x80 < e && found < 8; q++)
            {
                BYTE *t = (BYTE *)q;
                if (*q != vt) continue;
                found++;
                blog("shellvm: DIAG ServerTask(%s) %p remoteTask=%p cur=%u target=%u navlevel=%u +3cc=%u present=%u/%u "
                     "readyToNavigate(+3e6)=%u running(+3f4)=%u completed(+3f5)=%u +45b=%u",
                     when, t, *(void **)(t + 0x2f8), *(UINT32 *)(t + 0x3b8), *(UINT32 *)(t + 0x3bc),
                     *(UINT32 *)(t + 0x3c4), *(UINT32 *)(t + 0x3cc), t[0x3e9], t[0x3eb], t[0x3e6], t[0x3f4], t[0x3f5],
                     t[0x45b]);
            }
        }
        p = (BYTE *)mbi.BaseAddress + mbi.RegionSize;
    }
    if (!found) blog("shellvm: DIAG ServerTask(%s) none found", when);
}

static void svm_pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    for (;;)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        if ((LONG)(end - GetTickCount()) <= 0) break;
        MsgWaitForMultipleObjects(0, NULL, FALSE, 50, QS_ALLINPUT);
    }
}

static DWORD WINAPI shellvm_thread(void *arg)
{
    typedef HRESULT (WINAPI *create_fn)(IUnknown **);
    HMODULE msg = LoadLibraryW(L"CoreMessaging.dll"), coreui = LoadLibraryW(L"CoreUIComponents.dll");
    create_fn coreui_create = msg ? (void *)GetProcAddress(msg, "CoreUICreate") : NULL;
    create_fn factory_create = coreui ? (void *)GetProcAddress(coreui, "CoreUIFactoryCreate") : NULL;
    IUnknown *core = NULL, *factory = NULL, *proxy = NULL, *svm = NULL;
    UINT32 type = 0, ltype = 0, navigated[64];
    int nnav = 0, polls, watch_active = 0;
    BOOL listening = FALSE;
    HRESULT hr;

    if (!coreui_create || !factory_create) { blog("shellvm: CoreUICreate=%p CoreUIFactoryCreate=%p", coreui_create, factory_create); return 1; }
    hr = coreui_create(&core);
    blog("shellvm: CoreUICreate -> %#lx", hr);
    if (FAILED(hr = factory_create(&factory))) { blog("shellvm: CoreUIFactoryCreate -> %#lx", hr); goto done; }
    hr = VCALL(factory, 3, HRESULT (WINAPI *)(IUnknown *, const GUID *, UINT32 *))(factory, &IID_ShellViewManager, &type);
    blog("shellvm: FindTypeID({490C6BC3}) -> %#lx type=%#x", hr, type);
    if (FAILED(hr)) goto done;
    hr = VCALL(factory, 5, HRESULT (WINAPI *)(IUnknown *, const WCHAR *, UINT32, IUnknown **))(factory,
            L"System\\NavigationServer_ShellViewManager", type, &proxy);
    blog("shellvm: CreateMessageProxy(System\\NavigationServer_ShellViewManager) -> %#lx proxy=%p", hr, proxy);
    if (FAILED(hr)) goto done;
    hr = VCALL(proxy, 7, HRESULT (WINAPI *)(IUnknown *, IUnknown *))(proxy, &proxy_listener);
    blog("shellvm: AddProxyListener -> %#lx", hr);
    hr = IUnknown_QueryInterface(proxy, &IID_ShellViewManager, (void **)&svm);
    blog("shellvm: QI {490C6BC3} -> %#lx", hr);
    if (FAILED(hr)) goto done;
    for (polls = 0; polls < 600 && WaitForSingleObject(nav_stop, 0) == WAIT_TIMEOUT; polls++)
    {
        IUnknown *list = NULL;
        INT32 count = 0, i;
        svm_pump(500);
        if (WaitForSingleObject(svm_connected, 0) == WAIT_TIMEOUT) { if (polls % 20 == 0) blog("shellvm: waiting for OnConnected"); continue; }
        if (!listening)
        {
            /* native bridge: AddEventListener in OnConnected, before GetViews */
            listening = TRUE;
            /* AddEventListener's type is the proxied interface type, not the listener's: CreateInstance (0xd595)
             * stores GetReflection(interface def) at proxy+0x98, AddEventListener compares the def of its type
             * argument against it, then FindTypeMap(that def) (s_messageTypeMap entry 100) yields the event type
             * IRemoteShellViewManagerListener used to validate the listener. */
            ltype = type;
            hr = S_OK;
            {
                hr = VCALL(proxy, 9, HRESULT (WINAPI *)(IUnknown *, IUnknown *, UINT32))(proxy, &svm_listener, ltype);
                blog("shellvm: AddEventListener(type %#x) -> %#lx", ltype, hr);
            }
            svm_log_active_view(svm, "connected");
        }
        for (i = 0; i < 2; i++)
        {
            if (!InterlockedExchange(&svm_present[i].pending, 0)) continue;
            hr = VCALL(svm, 17 + i, HRESULT (WINAPI *)(IUnknown *, UINT32, BOOLEAN))(svm, svm_present[i].view, svm_present[i].flag);
            blog("shellvm: %s(id=%#x, %u) -> %#lx", i ? "EndPresentView" : "BeginPresentView", svm_present[i].view,
                 svm_present[i].flag, hr);
            svm_log_active_view(svm, "after present");
        }
        if (watch_active && (watch_active++ <= 20 || polls % 20 == 0))
        {
            svm_log_active_view(svm, "after navigate");
            svm_diag_tasks("after navigate");
        }
        hr = VCALL(svm, 5, HRESULT (WINAPI *)(IUnknown *, IUnknown **))(svm, &list);
        if (SUCCEEDED(hr) && list) hr = VCALL(list, 6, HRESULT (WINAPI *)(IUnknown *, INT32 *))(list, &count);
        if (FAILED(hr) || polls % 20 == 0) blog("shellvm: GetViews -> %#lx list=%p count=%d", hr, list, count);
        for (i = 0; SUCCEEDED(hr) && i < count; i++)
        {
            IUnknown *item = NULL, *view = NULL;
            UINT32 id = 0, pid = 0, level = 0, j;
            HRESULT hr2 = VCALL(list, 7, HRESULT (WINAPI *)(IUnknown *, INT32, IUnknown **))(list, i, &item);
            if (SUCCEEDED(hr2) && item) hr2 = IUnknown_QueryInterface(item, &IID_RemoteShellView, (void **)&view);
            if (SUCCEEDED(hr2))
            {
                VCALL(view, 19, HRESULT (WINAPI *)(IUnknown *, UINT32 *))(view, &id);
                VCALL(view, 12, HRESULT (WINAPI *)(IUnknown *, UINT32 *))(view, &pid);
                VCALL(view, 9, HRESULT (WINAPI *)(IUnknown *, UINT32 *))(view, &level);
                for (j = 0; j < nnav && navigated[j] != id; j++);
                if (j == nnav && nnav < ARRAY_SIZE(navigated))
                {
                    blog("shellvm: view[%d] id=%#x pid=%u level=%u", i, id, pid, level);
                    svm_diag_tasks("before navigate");
                    navigated[nnav++] = id;
                    hr2 = VCALL(svm, 14, HRESULT (WINAPI *)(IUnknown *, UINT32, INT32, INT32, INT32))(svm, id, 5, 0, 1);
                    blog("shellvm: NavigateToView(id=%#x, level 5 Active, direction 0, animation 1) -> %#lx", id, hr2);
                    svm_log_active_view(svm, "immediately after navigate");
                    watch_active = 1;
                }
            }
            else blog("shellvm: GetItem(%d)/QI IRemoteShellView -> %#lx", i, hr2);
            if (view) IUnknown_Release(view);
            if (item) IUnknown_Release(item);
        }
        if (list) IUnknown_Release(list);
    }
done:
    if (svm) IUnknown_Release(svm);
    if (proxy) IUnknown_Release(proxy);
    if (factory) IUnknown_Release(factory);
    if (core) IUnknown_Release(core);
    return 0;
}

static void stop_nav_server(void)
{
    if (!nav_thread) return;
    SetEvent(nav_stop);
    blog("navserver: stop -> wait %lu", WaitForSingleObject(nav_thread, 10000));
}

int main(int argc, char **argv)
{
    int seconds = argc > 1 ? atoi(argv[1]) : 240;
    BOOL nav_server = TRUE, shellvm = FALSE;
    DWORD cookie;
    HRESULT hr;

    if (argc > 2) width = atol(argv[2]);
    if (argc > 3) height = atol(argv[3]);
    if (argc > 4) window_type = atol(argv[4]);
    if (argc > 5) use_coreui = strcmp(argv[5], "twinapi") != 0;
    if (argc > 6) MultiByteToWideChar(CP_UTF8, 0, argv[6], -1, coreui_aumid, ARRAY_SIZE(coreui_aumid));
    if (argc > 7) MultiByteToWideChar(CP_UTF8, 0, argv[7], -1, coreui_contract, ARRAY_SIZE(coreui_contract));
    if (argc > 8 && strcmp(argv[8], "-")) coreui_view_id = _strtoui64(argv[8], NULL, 0);
    if (argc > 9) coreui_flags = strtoul(argv[9], NULL, 0);
    if (argc > 10) nav_server = strcmp(argv[10], "nonav") != 0;
    if (argc > 10) shellvm = !strcmp(argv[10], "shellvm");
    fill(provider_vtbl);
    fill(broker_vtbl);
    fill(presenter_broker_vtbl);
    provider_vtbl[3] = provider_QueryService;
    broker_vtbl[5] = broker_GetWindowFactory;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    blog("start pid=%lu CoInitializeEx -> %#lx seconds=%d", GetCurrentProcessId(), hr, seconds);
    if (nav_server) start_nav_server();
    if (nav_server && shellvm && nav_stop)
    {
        svm_connected = CreateEventW(NULL, TRUE, FALSE, NULL);
        CloseHandle(CreateThread(NULL, 0, shellvm_thread, NULL, 0, NULL));
    }
    hr = CoRegisterClassObject(&CLSID_Broker, (IUnknown *)&cf, CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE, &cookie);
    blog("CoRegisterClassObject {3480A401-BDE9-4407-BC02-798A866AC051} -> %#lx", hr);
    if (FAILED(hr)) return 1;
    Sleep(seconds * 1000);
    CoRevokeClassObject(cookie);
    stop_nav_server();
    blog("exit");
    CoUninitialize();
    return 0;
}
