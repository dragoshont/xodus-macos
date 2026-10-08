/* Paired probe: custom unmarshal through an in-process handler registered only as InprocHandler32
 * (no ThreadingModel), like twinapi.appcore's {14DE3806-5D5B-405C-AB89-4AC936BCBF48}.
 * The handler aggregates CoGetStdMarshalEx(SMEXF_HANDLER) and delegates IMarshal to it, the way
 * CWrlLightweightHandlerClientImpl does; the server writes the standard OBJREF with
 * CoGetStdMarshalEx(SMEXF_SERVER) and then 4 extra bytes.
 *
 * build:  x86_64-w64-mingw32-gcc -DHPROBE_DLL -shared -o hprobe.dll handler-unmarshal-probe.c -lole32 -luuid
 *         x86_64-w64-mingw32-gcc -o hprobe.exe handler-unmarshal-probe.c -lole32 -luuid
 * register (per user): HKCU\Software\Classes\CLSID\{6B1C3B8E-4F3A-4C55-9D0E-2A7C1D5E9F01}\InprocHandler32 = <path>\hprobe.dll
 * run:    hprobe.exe server <file> [info]      (MTA, marshals MSHCTX_LOCAL into <file>, waits 30 s;
 *                                               'info' = native server shape: IStdMarshalInfo and
 *                                               GetUnmarshalClass delegated to the std marshaler)
 *         hprobe.exe client <file> mta|sta2    (main thread is the main STA; unmarshals on a worker
 *                                               thread in the MTA or in a second STA) */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>

static const CLSID CLSID_TestHandler = {0x6b1c3b8e,0x4f3a,0x4c55,{0x9d,0x0e,0x2a,0x7c,0x1d,0x5e,0x9f,0x01}};

#define LOG(...) do { printf("[%s tid=%lu] ", HPROBE_SIDE, GetCurrentThreadId()); printf(__VA_ARGS__); printf("\n"); fflush(stdout); } while (0)

#ifdef HPROBE_DLL
#define HPROBE_SIDE "handler"
struct handler
{
    IUnknown IUnknown_iface;
    IMarshal IMarshal_iface;
    LONG ref;
    IUnknown *inner;
    IUnknown *outer; /* controlling unknown: the aggregating outer, or our own IUnknown */
};
static struct handler *h_from_unk(IUnknown *i) { return CONTAINING_RECORD(i, struct handler, IUnknown_iface); }
static struct handler *h_from_marshal(IMarshal *i) { return CONTAINING_RECORD(i, struct handler, IMarshal_iface); }

static HRESULT WINAPI h_QueryInterface(IUnknown *iface, REFIID riid, void **out)
{
    struct handler *h = h_from_unk(iface);
    if (IsEqualIID(riid, &IID_IUnknown)) *out = &h->IUnknown_iface;
    else if (IsEqualIID(riid, &IID_IMarshal)) *out = &h->IMarshal_iface;
    else if (h->inner) return IUnknown_QueryInterface(h->inner, riid, out);
    else { *out = NULL; return E_NOINTERFACE; }
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}
static ULONG WINAPI h_AddRef(IUnknown *iface) { return InterlockedIncrement(&h_from_unk(iface)->ref); }
static ULONG WINAPI h_Release(IUnknown *iface)
{
    struct handler *h = h_from_unk(iface);
    ULONG r = InterlockedDecrement(&h->ref);
    if (!r) { LOG("handler %p destroyed", h); if (h->inner) IUnknown_Release(h->inner); free(h); }
    return r;
}
static const IUnknownVtbl h_vtbl = { h_QueryInterface, h_AddRef, h_Release };

static HRESULT get_inner(struct handler *h, IMarshal **m)
{
    HRESULT hr;
    if (!h->inner)
    {
        hr = CoGetStdMarshalEx(h->outer, SMEXF_HANDLER, &h->inner);
        LOG("CoGetStdMarshalEx(SMEXF_HANDLER) -> %#lx", hr);
        if (FAILED(hr)) return hr;
    }
    return IUnknown_QueryInterface(h->inner, &IID_IMarshal, (void **)m);
}

static HRESULT WINAPI hm_QueryInterface(IMarshal *i, REFIID riid, void **out) { return IUnknown_QueryInterface(h_from_marshal(i)->outer, riid, out); }
static ULONG WINAPI hm_AddRef(IMarshal *i) { return IUnknown_AddRef(h_from_marshal(i)->outer); }
static ULONG WINAPI hm_Release(IMarshal *i) { return IUnknown_Release(h_from_marshal(i)->outer); }
static HRESULT WINAPI hm_GetUnmarshalClass(IMarshal *i, REFIID riid, void *pv, DWORD ctx, void *cd, DWORD fl, CLSID *clsid)
{
    IMarshal *m; HRESULT hr;
    if (SUCCEEDED(hr = get_inner(h_from_marshal(i), &m))) { hr = IMarshal_GetUnmarshalClass(m, riid, pv, ctx, cd, fl, clsid); IMarshal_Release(m); }
    LOG("GetUnmarshalClass ctx=%lu -> %#lx", ctx, hr);
    return hr;
}
static HRESULT WINAPI hm_GetMarshalSizeMax(IMarshal *i, REFIID riid, void *pv, DWORD ctx, void *cd, DWORD fl, DWORD *size)
{
    IMarshal *m; HRESULT hr;
    if (SUCCEEDED(hr = get_inner(h_from_marshal(i), &m))) { hr = IMarshal_GetMarshalSizeMax(m, riid, pv, ctx, cd, fl, size); IMarshal_Release(m); }
    LOG("GetMarshalSizeMax ctx=%lu -> %#lx", ctx, hr);
    return hr;
}
static HRESULT WINAPI hm_MarshalInterface(IMarshal *i, IStream *s, REFIID riid, void *pv, DWORD ctx, void *cd, DWORD fl)
{
    IMarshal *m; HRESULT hr;
    if (SUCCEEDED(hr = get_inner(h_from_marshal(i), &m))) { hr = IMarshal_MarshalInterface(m, s, riid, pv, ctx, cd, fl); IMarshal_Release(m); }
    LOG("MarshalInterface ctx=%lu -> %#lx", ctx, hr);
    return hr;
}
static HRESULT WINAPI hm_UnmarshalInterface(IMarshal *i, IStream *s, REFIID riid, void **out)
{
    struct handler *h = h_from_marshal(i);
    IMarshal *m; HRESULT hr; DWORD extra = 0; ULONG got = 0;
    if (SUCCEEDED(hr = get_inner(h, &m))) { hr = IMarshal_UnmarshalInterface(m, s, riid, out); IMarshal_Release(m); }
    LOG("inner UnmarshalInterface -> %#lx out=%p handler=%p", hr, out ? *out : NULL, &h->IUnknown_iface);
    if (SUCCEEDED(hr)) { IStream_Read(s, &extra, 4, &got); LOG("extra=%#lx (%lu bytes)", extra, got); }
    return hr;
}
static HRESULT WINAPI hm_ReleaseMarshalData(IMarshal *i, IStream *s) { LOG("ReleaseMarshalData"); return E_NOTIMPL; }
static HRESULT WINAPI hm_DisconnectObject(IMarshal *i, DWORD r) { LOG("DisconnectObject"); return S_OK; }
static const IMarshalVtbl hm_vtbl = { hm_QueryInterface, hm_AddRef, hm_Release, hm_GetUnmarshalClass, hm_GetMarshalSizeMax,
    hm_MarshalInterface, hm_UnmarshalInterface, hm_ReleaseMarshalData, hm_DisconnectObject };

static HRESULT WINAPI cf_QueryInterface(IClassFactory *i, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory)) { *out = i; return S_OK; }
    *out = NULL; return E_NOINTERFACE;
}
static ULONG WINAPI cf_AddRef(IClassFactory *i) { return 2; }
static ULONG WINAPI cf_Release(IClassFactory *i) { return 1; }
static HRESULT WINAPI cf_CreateInstance(IClassFactory *i, IUnknown *outer, REFIID riid, void **out)
{
    struct handler *h = calloc(1, sizeof(*h));
    APTTYPE at = -1; APTTYPEQUALIFIER aq = -1;
    HRESULT hr;
    CoGetApartmentType(&at, &aq);
    h->IUnknown_iface.lpVtbl = (IUnknownVtbl *)&h_vtbl;
    h->IMarshal_iface.lpVtbl = (IMarshalVtbl *)&hm_vtbl;
    h->ref = 1;
    h->outer = outer ? outer : &h->IUnknown_iface;
    if (outer && !IsEqualIID(riid, &IID_IUnknown)) hr = CLASS_E_NOAGGREGATION;
    else hr = h_QueryInterface(&h->IUnknown_iface, riid, out);
    h_Release(&h->IUnknown_iface);
    LOG("CreateInstance handler=%p outer=%p apttype=%d qualifier=%d -> %#lx", &h->IUnknown_iface, outer, at, aq, hr);
    return hr;
}
static HRESULT WINAPI cf_LockServer(IClassFactory *i, BOOL l) { return S_OK; }
static const IClassFactoryVtbl cf_vtbl = { cf_QueryInterface, cf_AddRef, cf_Release, cf_CreateInstance, cf_LockServer };
static IClassFactory cf = { (IClassFactoryVtbl *)&cf_vtbl };

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    LOG("DllGetClassObject");
    if (!IsEqualCLSID(clsid, &CLSID_TestHandler)) return CLASS_E_CLASSNOTAVAILABLE;
    return cf_QueryInterface(&cf, riid, out);
}
HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }

#else
#define HPROBE_SIDE "exe"
/* server object: IUnknown + IMarshal, custom unmarshal class = test handler */
struct server { IUnknown IUnknown_iface; IMarshal IMarshal_iface; IStdMarshalInfo IStdMarshalInfo_iface; LONG ref; IUnknown *std; };
static struct server srv;
/* 'info' server shape: IStdMarshalInfo + GetUnmarshalClass delegated to the std marshaler */
static int native_shape;
static HRESULT WINAPI s_QueryInterface(IUnknown *i, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown)) *out = &srv.IUnknown_iface;
    else if (IsEqualIID(riid, &IID_IMarshal)) *out = &srv.IMarshal_iface;
    else if (native_shape && IsEqualIID(riid, &IID_IStdMarshalInfo)) *out = &srv.IStdMarshalInfo_iface;
    else { *out = NULL; return E_NOINTERFACE; }
    return S_OK;
}
static HRESULT WINAPI si_QueryInterface(IStdMarshalInfo *i, REFIID riid, void **out) { return s_QueryInterface(NULL, riid, out); }
static ULONG WINAPI si_AddRef(IStdMarshalInfo *i) { return 2; }
static ULONG WINAPI si_Release(IStdMarshalInfo *i) { return 1; }
static HRESULT WINAPI si_GetClassForHandler(IStdMarshalInfo *i, DWORD ctx, void *cd, CLSID *clsid)
{
    LOG("GetClassForHandler ctx=%lu", ctx);
    *clsid = CLSID_TestHandler;
    return S_OK;
}
static const IStdMarshalInfoVtbl si_vtbl = { si_QueryInterface, si_AddRef, si_Release, si_GetClassForHandler };
static ULONG WINAPI s_AddRef(IUnknown *i) { return 2; }
static ULONG WINAPI s_Release(IUnknown *i) { return 1; }
static const IUnknownVtbl s_vtbl = { s_QueryInterface, s_AddRef, s_Release };
static IMarshal *s_std(void)
{
    IMarshal *m = NULL;
    if (!srv.std) LOG("CoGetStdMarshalEx(SMEXF_SERVER) -> %#lx", CoGetStdMarshalEx(&srv.IUnknown_iface, SMEXF_SERVER, &srv.std));
    if (srv.std) IUnknown_QueryInterface(srv.std, &IID_IMarshal, (void **)&m);
    return m;
}
static HRESULT WINAPI sm_QueryInterface(IMarshal *i, REFIID riid, void **out) { return s_QueryInterface(NULL, riid, out); }
static ULONG WINAPI sm_AddRef(IMarshal *i) { return 2; }
static ULONG WINAPI sm_Release(IMarshal *i) { return 1; }
static HRESULT WINAPI sm_GetUnmarshalClass(IMarshal *i, REFIID riid, void *pv, DWORD ctx, void *cd, DWORD fl, CLSID *clsid)
{
    IMarshal *m; HRESULT hr;
    if (!native_shape) { *clsid = CLSID_TestHandler; return S_OK; }
    m = s_std(); hr = IMarshal_GetUnmarshalClass(m, riid, pv, ctx, cd, fl, clsid); IMarshal_Release(m);
    LOG("std GetUnmarshalClass -> %#lx clsid=%08lx", hr, clsid->Data1);
    return hr;
}
static HRESULT WINAPI sm_GetMarshalSizeMax(IMarshal *i, REFIID riid, void *pv, DWORD ctx, void *cd, DWORD fl, DWORD *size)
{
    IMarshal *m = s_std(); HRESULT hr = IMarshal_GetMarshalSizeMax(m, riid, pv, ctx, cd, fl, size); IMarshal_Release(m);
    if (SUCCEEDED(hr)) *size += 4;
    return hr;
}
static HRESULT WINAPI sm_MarshalInterface(IMarshal *i, IStream *s, REFIID riid, void *pv, DWORD ctx, void *cd, DWORD fl)
{
    IMarshal *m = s_std(); DWORD extra = 0x1234abcd;
    HRESULT hr = IMarshal_MarshalInterface(m, s, riid, pv, ctx, cd, fl); IMarshal_Release(m);
    LOG("std MarshalInterface -> %#lx", hr);
    if (SUCCEEDED(hr)) hr = IStream_Write(s, &extra, 4, NULL);
    return hr;
}
static HRESULT WINAPI sm_UnmarshalInterface(IMarshal *i, IStream *s, REFIID riid, void **out) { return E_NOTIMPL; }
static HRESULT WINAPI sm_ReleaseMarshalData(IMarshal *i, IStream *s) { return E_NOTIMPL; }
static HRESULT WINAPI sm_DisconnectObject(IMarshal *i, DWORD r) { return E_NOTIMPL; }
static const IMarshalVtbl sm_vtbl = { sm_QueryInterface, sm_AddRef, sm_Release, sm_GetUnmarshalClass, sm_GetMarshalSizeMax,
    sm_MarshalInterface, sm_UnmarshalInterface, sm_ReleaseMarshalData, sm_DisconnectObject };

static const char *file, *mode;
static HANDLE done;

static DWORD WINAPI client_thread(void *arg)
{
    HGLOBAL hg; IStream *s; IUnknown *unk = NULL, *ident = NULL; HRESULT hr; FILE *f; char buf[4096]; size_t n;
    APTTYPE at = -1; APTTYPEQUALIFIER aq = -1;
    LOG("CoInitializeEx(%s) -> %#lx", mode, CoInitializeEx(NULL, !strcmp(mode, "mta") ? COINIT_MULTITHREADED : COINIT_APARTMENTTHREADED));
    CoGetApartmentType(&at, &aq);
    LOG("caller apttype=%d qualifier=%d", at, aq);
    f = fopen(file, "rb"); n = fread(buf, 1, sizeof(buf), f); fclose(f);
    hg = GlobalAlloc(GMEM_MOVEABLE, n); memcpy(GlobalLock(hg), buf, n); GlobalUnlock(hg);
    CreateStreamOnHGlobal(hg, TRUE, &s);
    hr = CoUnmarshalInterface(s, &IID_IUnknown, (void **)&unk);
    LOG("CoUnmarshalInterface(%lu bytes) -> %#lx unk=%p", (unsigned long)n, hr, unk);
    if (SUCCEEDED(hr))
    {
        IMarshal *m = NULL;
        hr = IUnknown_QueryInterface(unk, &IID_IMarshal, (void **)&m);
        LOG("QI IMarshal -> %#lx", hr);
        if (m) IMarshal_Release(m);
        IUnknown_QueryInterface(unk, &IID_IUnknown, (void **)&ident);
        LOG("identity unk=%p", ident);
        if (ident) IUnknown_Release(ident);
        IUnknown_Release(unk);
    }
    IStream_Release(s);
    CoUninitialize();
    SetEvent(done);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    file = argv[2];
    if (!strcmp(argv[1], "server"))
    {
        IStream *s; HGLOBAL hg; HRESULT hr; FILE *f; STATSTG st;
        CoInitializeEx(NULL, COINIT_MULTITHREADED);
        srv.IUnknown_iface.lpVtbl = (IUnknownVtbl *)&s_vtbl;
        srv.IMarshal_iface.lpVtbl = (IMarshalVtbl *)&sm_vtbl;
        srv.IStdMarshalInfo_iface.lpVtbl = (IStdMarshalInfoVtbl *)&si_vtbl;
        native_shape = argc > 3 && !strcmp(argv[3], "info");
        CreateStreamOnHGlobal(NULL, FALSE, &s);
        hr = CoMarshalInterface(s, &IID_IUnknown, &srv.IUnknown_iface, MSHCTX_LOCAL, NULL, MSHLFLAGS_NORMAL);
        IStream_Stat(s, &st, STATFLAG_NONAME);
        LOG("CoMarshalInterface -> %#lx size=%lu", hr, (unsigned long)st.cbSize.QuadPart);
        GetHGlobalFromStream(s, &hg);
        { BYTE *b = GlobalLock(hg); ULONG k; printf("objref:"); for (k = 0; k < st.cbSize.QuadPart && k < 48; k++) printf(" %02x", b[k]); printf("\n"); fflush(stdout); }
        f = fopen(file, "wb"); fwrite(GlobalLock(hg), 1, (size_t)st.cbSize.QuadPart, f); fclose(f);
        Sleep(30000);
        return 0;
    }
    else
    {
        MSG msg; HANDLE th;
        mode = argc > 3 ? argv[3] : "mta";
        LOG("main CoInitializeEx(STA) -> %#lx", CoInitializeEx(NULL, COINIT_APARTMENTTHREADED));
        done = CreateEventW(NULL, TRUE, FALSE, NULL);
        th = CreateThread(NULL, 0, client_thread, NULL, 0, NULL);
        while (MsgWaitForMultipleObjects(1, &done, FALSE, 20000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        LOG("done wait=%lu", WaitForSingleObject(done, 0));
        CloseHandle(th);
        CoUninitialize();
        return 0;
    }
}
#endif
