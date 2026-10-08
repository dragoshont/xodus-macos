#!/usr/bin/env python3
# Standard handler marshaling for Wine combase, needed by the original Xbox app's window broker path.
#
# Native evidence (paired probe handler-unmarshal-probe.c on Windows 10.0.26300, outputs in
# probe-outputs/activation-20261007/):
#  - An object that aggregates CoGetStdMarshalEx(SMEXF_SERVER) and exposes IStdMarshalInfo is marshaled
#    as OBJREF_CUSTOM{CLSID_AggStdMarshal {00000027-0000-0008-C000-000000000046}} wrapping
#    OBJREF_HANDLER{STDOBJREF, handler clsid from GetClassForHandler, empty DUALSTRINGARRAY}; the
#    object's own trailing data follows.
#  - Unmarshaling creates the InprocHandler32 class (registered without ThreadingModel) on the calling
#    thread, in MTA and in a non-main STA alike, aggregated by a standard identity; the handler's
#    CoGetStdMarshalEx(outer, SMEXF_HANDLER) succeeds for that identity and returns E_INVALIDARG for a
#    plain outer; the inner unmarshal returns the identity and the stream is left after OBJREF_HANDLER.
#  - twinapi.appcore 26100.9278 CWrlLightweightHandlerClientImpl::v_GetHandlerType (0x93a80) returns 2
#    (SMEXF_HANDLER); UnmarshalInterface (0x31be0) aggregates CoGetStdMarshalEx(outer, that value).
# Upstream Wine 59416cf5 (2026-10-06): CoGetStdMarshalEx is "@ stub", CLSID_AggStdMarshal is not
# handled and get_unmarshaler_from_stream has "FIXME: handler marshaling". Apply after patch-stdmex.py.
# Limitation: the identity forwards unknown interfaces to the handler; standard proxies keep their own
# IUnknown identity (Wine's proxy manager cannot be the aggregating outer).
import os, shutil, sys
W = sys.argv[1]
def rd(p): return open(os.path.join(W, p)).read()
def wr(p, s):
    f = os.path.join(W, p)
    if not os.path.exists(f + ".pre-stdmexh"): shutil.copy2(f, f + ".pre-stdmexh")
    open(f, "w").write(s)
def sub(p, old, new, marker):
    s = rd(p)
    if marker in s: print("already", p, marker); return
    assert s.count(old) == 1, (p, old)
    wr(p, s.replace(old, new)); print("patched", p, marker)

M = "dlls/combase/marshal.c"

sub(M, "static HRESULT get_unmarshaler_from_stream(IStream *stream, IMarshal **marshal, IID *iid)\n{",
"""static const CLSID CLSID_AggStdMarshal_ = {0x00000027,0x0000,0x0008,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static HRESULT agg_std_unmarshaler_create(IMarshal **marshal);

static HRESULT get_unmarshaler_from_stream(IStream *stream, IMarshal **marshal, IID *iid)
{""", "static HRESULT agg_std_unmarshaler_create(IMarshal **marshal);")

sub(M, """        hr = CoCreateInstance(&objref.u_objref.u_custom.clsid, NULL,
                              CLSCTX_INPROC_SERVER, &IID_IMarshal,
                              (LPVOID*)marshal);""",
"""        if (IsEqualCLSID(&objref.u_objref.u_custom.clsid, &CLSID_AggStdMarshal_))
            hr = agg_std_unmarshaler_create(marshal);
        else
            hr = CoCreateInstance(&objref.u_objref.u_custom.clsid, NULL,
                                  CLSCTX_INPROC_SERVER, &IID_IMarshal,
                                  (LPVOID*)marshal);""", "hr = agg_std_unmarshaler_create(marshal);")

# server side: SMEXF_SERVER marshaler of an object exposing IStdMarshalInfo writes handler marshal data
sub(M, """static HRESULT WINAPI std_marshal_ex_GetUnmarshalClass(IMarshal *iface, REFIID riid, void *pv,
        DWORD dest_context, void *dest_context_data, DWORD mshlflags, CLSID *clsid)
{
    struct std_marshal_ex *marshal = std_marshal_ex_from_IMarshal(iface);
    return IMarshal_GetUnmarshalClass(marshal->std, riid, pv, dest_context, dest_context_data, mshlflags, clsid);
}

static HRESULT WINAPI std_marshal_ex_GetMarshalSizeMax(IMarshal *iface, REFIID riid, void *pv,
        DWORD dest_context, void *dest_context_data, DWORD mshlflags, DWORD *size)
{
    struct std_marshal_ex *marshal = std_marshal_ex_from_IMarshal(iface);
    return IMarshal_GetMarshalSizeMax(marshal->std, riid, pv, dest_context, dest_context_data, mshlflags, size);
}

static HRESULT WINAPI std_marshal_ex_MarshalInterface(IMarshal *iface, IStream *stream, REFIID riid, void *pv,
        DWORD dest_context, void *dest_context_data, DWORD mshlflags)
{
    struct std_marshal_ex *marshal = std_marshal_ex_from_IMarshal(iface);
    return IMarshal_MarshalInterface(marshal->std, stream, riid, pv, dest_context, dest_context_data, mshlflags);
}""",
"""/* An object exposing IStdMarshalInfo is marshaled for an in-process handler: OBJREF_CUSTOM for
 * CLSID_AggStdMarshal wrapping an OBJREF_HANDLER that names the handler class. */
static BOOL std_marshal_ex_get_handler(struct std_marshal_ex *marshal, DWORD dest_context,
        void *dest_context_data, CLSID *clsid)
{
    IStdMarshalInfo *info;
    HRESULT hr;

    if (FAILED(IUnknown_QueryInterface(marshal->outer, &IID_IStdMarshalInfo, (void **)&info)))
        return FALSE;
    hr = IStdMarshalInfo_GetClassForHandler(info, dest_context, dest_context_data, clsid);
    IStdMarshalInfo_Release(info);
    return hr == S_OK;
}

static HRESULT WINAPI std_marshal_ex_GetUnmarshalClass(IMarshal *iface, REFIID riid, void *pv,
        DWORD dest_context, void *dest_context_data, DWORD mshlflags, CLSID *clsid)
{
    struct std_marshal_ex *marshal = std_marshal_ex_from_IMarshal(iface);
    CLSID handler;

    if (std_marshal_ex_get_handler(marshal, dest_context, dest_context_data, &handler))
    {
        *clsid = CLSID_AggStdMarshal_;
        return S_OK;
    }
    return IMarshal_GetUnmarshalClass(marshal->std, riid, pv, dest_context, dest_context_data, mshlflags, clsid);
}

static HRESULT WINAPI std_marshal_ex_GetMarshalSizeMax(IMarshal *iface, REFIID riid, void *pv,
        DWORD dest_context, void *dest_context_data, DWORD mshlflags, DWORD *size)
{
    struct std_marshal_ex *marshal = std_marshal_ex_from_IMarshal(iface);
    CLSID handler;
    HRESULT hr;

    hr = IMarshal_GetMarshalSizeMax(marshal->std, riid, pv, dest_context, dest_context_data, mshlflags, size);
    if (SUCCEEDED(hr) && std_marshal_ex_get_handler(marshal, dest_context, dest_context_data, &handler))
        *size += sizeof(CLSID);
    return hr;
}

static HRESULT WINAPI std_marshal_ex_MarshalInterface(IMarshal *iface, IStream *stream, REFIID riid, void *pv,
        DWORD dest_context, void *dest_context_data, DWORD mshlflags)
{
    struct std_marshal_ex *marshal = std_marshal_ex_from_IMarshal(iface);
    struct apartment *apt;
    OBJREF objref;
    CLSID handler;
    HRESULT hr;
    ULONG res;

    if (!std_marshal_ex_get_handler(marshal, dest_context, dest_context_data, &handler))
        return IMarshal_MarshalInterface(marshal->std, stream, riid, pv, dest_context, dest_context_data, mshlflags);

    TRACE("handler %s\\n", debugstr_guid(&handler));

    if (!(apt = apartment_get_current_or_mta()))
        return CO_E_NOTINITIALIZED;
    rpc_start_remoting(apt);

    objref.signature = OBJREF_SIGNATURE;
    objref.flags = OBJREF_HANDLER;
    objref.iid = *riid;
    hr = marshal_object(apt, &objref.u_objref.u_handler.std, riid, pv, dest_context, dest_context_data, mshlflags);
    apartment_release(apt);
    if (hr != S_OK)
        return hr;
    objref.u_objref.u_handler.clsid = handler;
    memset(&objref.u_objref.u_handler.saResAddr, 0, sizeof(objref.u_objref.u_handler.saResAddr));
    return IStream_Write(stream, &objref, FIELD_OFFSET(OBJREF, u_objref.u_handler.saResAddr.aStringArray), &res);
}""", "std_marshal_ex_get_handler(struct std_marshal_ex")

HANDLER = r'''/* Reads the rest of an OBJREF_HANDLER and returns its standard part as a stream holding an OR_STANDARD. */
static HRESULT handler_objref_to_std_stream(IStream *stream, CLSID *clsid, IStream **std_stream)
{
    LARGE_INTEGER zero = {{ 0 }};
    struct OR_STANDARD std;
    struct OR_HANDLER obj;
    HRESULT hr;
    ULONG res;

    *std_stream = NULL;
    hr = IStream_Read(stream, &obj, FIELD_OFFSET(struct OR_HANDLER, saResAddr.aStringArray), &res);
    if (hr != S_OK || res != FIELD_OFFSET(struct OR_HANDLER, saResAddr.aStringArray))
        return STG_E_READFAULT;
    if (obj.saResAddr.wNumEntries)
    {
        ERR("unsupported size of DUALSTRINGARRAY\n");
        return E_NOTIMPL;
    }
    if (clsid) *clsid = obj.clsid;

    memset(&std, 0, sizeof(std));
    std.std = obj.std;
    if (FAILED(hr = CreateStreamOnHGlobal(NULL, TRUE, std_stream)))
        return hr;
    hr = IStream_Write(*std_stream, &std, FIELD_OFFSET(struct OR_STANDARD, saResAddr.aStringArray), NULL);
    if (SUCCEEDED(hr))
        hr = IStream_Seek(*std_stream, zero, STREAM_SEEK_SET, NULL);
    if (FAILED(hr))
    {
        IStream_Release(*std_stream);
        *std_stream = NULL;
    }
    return hr;
}

static HRESULT std_or_handler_unmarshal(IStream *stream, const OBJREF *objref, IUnknown **proxy)
{
    IStream *std_stream;
    HRESULT hr;

    if (objref->flags & OBJREF_HANDLER)
    {
        if (FAILED(hr = handler_objref_to_std_stream(stream, NULL, &std_stream)))
            return hr;
        hr = std_unmarshal_interface(0, NULL, std_stream, &objref->iid, (void **)proxy, FALSE);
        IStream_Release(std_stream);
        return hr;
    }
    if (objref->flags & OBJREF_STANDARD)
        return std_unmarshal_interface(0, NULL, stream, &objref->iid, (void **)proxy, FALSE);

    FIXME("unsupported objref.flags = %lx\n", objref->flags);
    return E_NOTIMPL;
}

static HRESULT std_or_handler_release_marshal_data(IStream *stream, const OBJREF *objref)
{
    IStream *std_stream;
    HRESULT hr;

    if (objref->flags & OBJREF_HANDLER)
    {
        if (FAILED(hr = handler_objref_to_std_stream(stream, NULL, &std_stream)))
            return hr;
        hr = std_release_marshal_data(std_stream);
        IStream_Release(std_stream);
        return hr;
    }
    if (objref->flags & OBJREF_STANDARD)
        return std_release_marshal_data(stream);
    return E_NOTIMPL;
}

static HRESULT read_objref_header(IStream *stream, OBJREF *objref)
{
    HRESULT hr;
    ULONG res;

    hr = IStream_Read(stream, objref, FIELD_OFFSET(OBJREF, u_objref), &res);
    if (hr != S_OK || res != FIELD_OFFSET(OBJREF, u_objref))
        return STG_E_READFAULT;
    if (objref->signature != OBJREF_SIGNATURE)
        return RPC_E_INVALID_OBJREF;
    return S_OK;
}

/* Client half returned by CoGetStdMarshalEx(SMEXF_HANDLER). An in-process handler aggregates it and
 * passes it the OBJREF_HANDLER written by the server's standard marshaler. The handler identity is
 * what the caller receives; interfaces the handler does not implement itself are reached through
 * the inner unknown, which forwards them to the standard proxy. */
struct std_marshal_handler
{
    IUnknown IUnknown_inner;
    IMarshal IMarshal_iface;
    LONG refcount;
    IUnknown *outer;
    IUnknown *proxy;
};

static inline struct std_marshal_handler *std_marshal_handler_from_IUnknown(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, struct std_marshal_handler, IUnknown_inner);
}

static inline struct std_marshal_handler *std_marshal_handler_from_IMarshal(IMarshal *iface)
{
    return CONTAINING_RECORD(iface, struct std_marshal_handler, IMarshal_iface);
}

static HRESULT WINAPI std_marshal_handler_inner_QueryInterface(IUnknown *iface, REFIID riid, void **obj)
{
    struct std_marshal_handler *handler = std_marshal_handler_from_IUnknown(iface);

    TRACE("%p, %s, %p.\n", iface, debugstr_guid(riid), obj);

    *obj = NULL;
    if (IsEqualIID(riid, &IID_IUnknown))
        *obj = &handler->IUnknown_inner;
    else if (IsEqualIID(riid, &IID_IMarshal))
        *obj = &handler->IMarshal_iface;
    else if (handler->proxy)
        return IUnknown_QueryInterface(handler->proxy, riid, obj);
    else
        return E_NOINTERFACE;

    IUnknown_AddRef((IUnknown *)*obj);
    return S_OK;
}

static ULONG WINAPI std_marshal_handler_inner_AddRef(IUnknown *iface)
{
    struct std_marshal_handler *handler = std_marshal_handler_from_IUnknown(iface);
    return InterlockedIncrement(&handler->refcount);
}

static ULONG WINAPI std_marshal_handler_inner_Release(IUnknown *iface)
{
    struct std_marshal_handler *handler = std_marshal_handler_from_IUnknown(iface);
    ULONG refcount = InterlockedDecrement(&handler->refcount);

    if (!refcount)
    {
        if (handler->proxy) IUnknown_Release(handler->proxy);
        free(handler);
    }

    return refcount;
}

static const IUnknownVtbl std_marshal_handler_inner_vtbl =
{
    std_marshal_handler_inner_QueryInterface,
    std_marshal_handler_inner_AddRef,
    std_marshal_handler_inner_Release,
};

static HRESULT WINAPI std_marshal_handler_QueryInterface(IMarshal *iface, REFIID riid, void **obj)
{
    struct std_marshal_handler *handler = std_marshal_handler_from_IMarshal(iface);
    return IUnknown_QueryInterface(handler->outer, riid, obj);
}

static ULONG WINAPI std_marshal_handler_AddRef(IMarshal *iface)
{
    struct std_marshal_handler *handler = std_marshal_handler_from_IMarshal(iface);
    return IUnknown_AddRef(handler->outer);
}

static ULONG WINAPI std_marshal_handler_Release(IMarshal *iface)
{
    struct std_marshal_handler *handler = std_marshal_handler_from_IMarshal(iface);
    return IUnknown_Release(handler->outer);
}

/* Marshaling the handler again marshals its proxy. */
static HRESULT std_marshal_handler_get_proxy_marshal(struct std_marshal_handler *handler, IMarshal **marshal)
{
    if (!handler->proxy)
        return E_UNEXPECTED;
    return IUnknown_QueryInterface(handler->proxy, &IID_IMarshal, (void **)marshal);
}

static HRESULT WINAPI std_marshal_handler_GetUnmarshalClass(IMarshal *iface, REFIID riid, void *pv,
        DWORD dest_context, void *dest_context_data, DWORD mshlflags, CLSID *clsid)
{
    struct std_marshal_handler *handler = std_marshal_handler_from_IMarshal(iface);
    IMarshal *marshal;
    HRESULT hr;

    if (FAILED(hr = std_marshal_handler_get_proxy_marshal(handler, &marshal)))
        return hr;
    hr = IMarshal_GetUnmarshalClass(marshal, riid, pv, dest_context, dest_context_data, mshlflags, clsid);
    IMarshal_Release(marshal);
    return hr;
}

static HRESULT WINAPI std_marshal_handler_GetMarshalSizeMax(IMarshal *iface, REFIID riid, void *pv,
        DWORD dest_context, void *dest_context_data, DWORD mshlflags, DWORD *size)
{
    struct std_marshal_handler *handler = std_marshal_handler_from_IMarshal(iface);
    IMarshal *marshal;
    HRESULT hr;

    if (FAILED(hr = std_marshal_handler_get_proxy_marshal(handler, &marshal)))
        return hr;
    hr = IMarshal_GetMarshalSizeMax(marshal, riid, pv, dest_context, dest_context_data, mshlflags, size);
    IMarshal_Release(marshal);
    return hr;
}

static HRESULT WINAPI std_marshal_handler_MarshalInterface(IMarshal *iface, IStream *stream, REFIID riid,
        void *pv, DWORD dest_context, void *dest_context_data, DWORD mshlflags)
{
    struct std_marshal_handler *handler = std_marshal_handler_from_IMarshal(iface);
    IMarshal *marshal;
    HRESULT hr;

    if (FAILED(hr = std_marshal_handler_get_proxy_marshal(handler, &marshal)))
        return hr;
    hr = IMarshal_MarshalInterface(marshal, stream, riid, handler->proxy, dest_context, dest_context_data, mshlflags);
    IMarshal_Release(marshal);
    return hr;
}

static HRESULT WINAPI std_marshal_handler_UnmarshalInterface(IMarshal *iface, IStream *stream, REFIID riid, void **obj)
{
    struct std_marshal_handler *handler = std_marshal_handler_from_IMarshal(iface);
    IUnknown *proxy;
    OBJREF objref;
    HRESULT hr;

    TRACE("%p, %p, %s, %p.\n", iface, stream, debugstr_guid(riid), obj);

    *obj = NULL;
    if (FAILED(hr = read_objref_header(stream, &objref)))
        return hr;
    if (FAILED(hr = std_or_handler_unmarshal(stream, &objref, &proxy)))
        return hr;

    if (handler->proxy) IUnknown_Release(handler->proxy);
    handler->proxy = proxy;

    return IUnknown_QueryInterface(handler->outer, riid, obj);
}

static HRESULT WINAPI std_marshal_handler_ReleaseMarshalData(IMarshal *iface, IStream *stream)
{
    OBJREF objref;
    HRESULT hr;

    TRACE("%p, %p.\n", iface, stream);

    if (FAILED(hr = read_objref_header(stream, &objref)))
        return hr;
    return std_or_handler_release_marshal_data(stream, &objref);
}

static HRESULT WINAPI std_marshal_handler_DisconnectObject(IMarshal *iface, DWORD reserved)
{
    struct std_marshal_handler *handler = std_marshal_handler_from_IMarshal(iface);
    IUnknown *proxy;

    TRACE("%p, %#lx.\n", iface, reserved);

    if ((proxy = InterlockedExchangePointer((void **)&handler->proxy, NULL)))
        IUnknown_Release(proxy);
    return S_OK;
}

static const IMarshalVtbl std_marshal_handler_vtbl =
{
    std_marshal_handler_QueryInterface,
    std_marshal_handler_AddRef,
    std_marshal_handler_Release,
    std_marshal_handler_GetUnmarshalClass,
    std_marshal_handler_GetMarshalSizeMax,
    std_marshal_handler_MarshalInterface,
    std_marshal_handler_UnmarshalInterface,
    std_marshal_handler_ReleaseMarshalData,
    std_marshal_handler_DisconnectObject,
};

/* Identity that aggregates an in-process handler unmarshaled from OBJREF_HANDLER data. */
struct handler_identity
{
    IUnknown IUnknown_iface;
    LONG refcount;
    IUnknown *handler;
};

static inline struct handler_identity *handler_identity_from_IUnknown(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, struct handler_identity, IUnknown_iface);
}

static HRESULT WINAPI handler_identity_QueryInterface(IUnknown *iface, REFIID riid, void **obj)
{
    struct handler_identity *identity = handler_identity_from_IUnknown(iface);

    TRACE("%p, %s, %p.\n", iface, debugstr_guid(riid), obj);

    *obj = NULL;
    if (IsEqualIID(riid, &IID_IUnknown))
    {
        *obj = &identity->IUnknown_iface;
        IUnknown_AddRef(&identity->IUnknown_iface);
        return S_OK;
    }
    if (identity->handler)
        return IUnknown_QueryInterface(identity->handler, riid, obj);
    return E_NOINTERFACE;
}

static ULONG WINAPI handler_identity_AddRef(IUnknown *iface)
{
    struct handler_identity *identity = handler_identity_from_IUnknown(iface);
    return InterlockedIncrement(&identity->refcount);
}

static ULONG WINAPI handler_identity_Release(IUnknown *iface)
{
    struct handler_identity *identity = handler_identity_from_IUnknown(iface);
    ULONG refcount = InterlockedDecrement(&identity->refcount);

    if (!refcount)
    {
        identity->refcount = 1;
        if (identity->handler) IUnknown_Release(identity->handler);
        free(identity);
    }
    return refcount;
}

static const IUnknownVtbl handler_identity_vtbl =
{
    handler_identity_QueryInterface,
    handler_identity_AddRef,
    handler_identity_Release,
};

static HRESULT handler_identity_create(const CLSID *clsid, struct handler_identity **out)
{
    struct class_reg_data regdata = { 0 };
    struct handler_identity *identity;
    struct apartment *apt;
    IClassFactory *factory;
    HKEY hkey;
    HRESULT hr;

    if (FAILED(hr = open_key_for_clsid(clsid, L"InprocHandler32", KEY_READ, &hkey)))
    {
        WARN("handler %s is not registered, hr %#lx.\n", debugstr_guid(clsid), hr);
        return hr;
    }
    if (!(apt = apartment_get_current_or_mta()))
    {
        RegCloseKey(hkey);
        return CO_E_NOTINITIALIZED;
    }
    regdata.u.hkey = hkey;
    regdata.origin = CLASS_REG_REGISTRY;
    /* the handler is created in the unmarshaling apartment whatever its threading model */
    hr = apartment_get_inproc_class_object(apt, &regdata, clsid, &IID_IClassFactory,
            CLSCTX_INPROC_HANDLER | CLSCTX_PS_DLL, (void **)&factory);
    apartment_release(apt);
    RegCloseKey(hkey);
    if (FAILED(hr))
        return hr;

    if (!(identity = calloc(1, sizeof(*identity))))
    {
        IClassFactory_Release(factory);
        return E_OUTOFMEMORY;
    }
    identity->IUnknown_iface.lpVtbl = &handler_identity_vtbl;
    identity->refcount = 1;
    hr = IClassFactory_CreateInstance(factory, &identity->IUnknown_iface, &IID_IUnknown, (void **)&identity->handler);
    IClassFactory_Release(factory);
    if (FAILED(hr))
    {
        WARN("handler %s CreateInstance failed, hr %#lx.\n", debugstr_guid(clsid), hr);
        free(identity);
        return hr;
    }
    *out = identity;
    return S_OK;
}

/* Unmarshaler for CLSID_AggStdMarshal data. */
struct agg_std_unmarshaler
{
    IMarshal IMarshal_iface;
    LONG refcount;
};

static inline struct agg_std_unmarshaler *agg_std_unmarshaler_from_IMarshal(IMarshal *iface)
{
    return CONTAINING_RECORD(iface, struct agg_std_unmarshaler, IMarshal_iface);
}

static HRESULT WINAPI agg_std_unmarshaler_QueryInterface(IMarshal *iface, REFIID riid, void **obj)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IMarshal))
    {
        *obj = iface;
        IMarshal_AddRef(iface);
        return S_OK;
    }
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI agg_std_unmarshaler_AddRef(IMarshal *iface)
{
    return InterlockedIncrement(&agg_std_unmarshaler_from_IMarshal(iface)->refcount);
}

static ULONG WINAPI agg_std_unmarshaler_Release(IMarshal *iface)
{
    struct agg_std_unmarshaler *unmarshaler = agg_std_unmarshaler_from_IMarshal(iface);
    ULONG refcount = InterlockedDecrement(&unmarshaler->refcount);

    if (!refcount) free(unmarshaler);
    return refcount;
}

static HRESULT WINAPI agg_std_unmarshaler_GetUnmarshalClass(IMarshal *iface, REFIID riid, void *pv,
        DWORD dest_context, void *dest_context_data, DWORD mshlflags, CLSID *clsid)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI agg_std_unmarshaler_GetMarshalSizeMax(IMarshal *iface, REFIID riid, void *pv,
        DWORD dest_context, void *dest_context_data, DWORD mshlflags, DWORD *size)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI agg_std_unmarshaler_MarshalInterface(IMarshal *iface, IStream *stream, REFIID riid,
        void *pv, DWORD dest_context, void *dest_context_data, DWORD mshlflags)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI agg_std_unmarshaler_UnmarshalInterface(IMarshal *iface, IStream *stream, REFIID riid, void **obj)
{
    LARGE_INTEGER zero = {{ 0 }}, pos;
    struct handler_identity *identity;
    ULARGE_INTEGER start;
    IStream *std_stream;
    IMarshal *marshal;
    IUnknown *unk;
    OBJREF objref;
    CLSID clsid;
    HRESULT hr;

    TRACE("%p, %p, %s, %p.\n", iface, stream, debugstr_guid(riid), obj);

    *obj = NULL;
    if (FAILED(hr = IStream_Seek(stream, zero, STREAM_SEEK_CUR, &start)))
        return hr;
    if (FAILED(hr = read_objref_header(stream, &objref)))
        return hr;

    if (!(objref.flags & OBJREF_HANDLER))
    {
        if (FAILED(hr = std_or_handler_unmarshal(stream, &objref, &unk)))
            return hr;
        hr = IUnknown_QueryInterface(unk, riid, obj);
        IUnknown_Release(unk);
        return hr;
    }

    if (FAILED(hr = handler_objref_to_std_stream(stream, &clsid, &std_stream)))
        return hr;
    IStream_Release(std_stream);
    pos.QuadPart = start.QuadPart;
    if (FAILED(hr = IStream_Seek(stream, pos, STREAM_SEEK_SET, NULL)))
        return hr;

    TRACE("handler %s\n", debugstr_guid(&clsid));
    if (FAILED(hr = handler_identity_create(&clsid, &identity)))
        return hr;
    if (SUCCEEDED(hr = IUnknown_QueryInterface(identity->handler, &IID_IMarshal, (void **)&marshal)))
    {
        hr = IMarshal_UnmarshalInterface(marshal, stream, riid, obj);
        IMarshal_Release(marshal);
    }
    IUnknown_Release(&identity->IUnknown_iface);
    return hr;
}

static HRESULT WINAPI agg_std_unmarshaler_ReleaseMarshalData(IMarshal *iface, IStream *stream)
{
    OBJREF objref;
    HRESULT hr;

    if (FAILED(hr = read_objref_header(stream, &objref)))
        return hr;
    return std_or_handler_release_marshal_data(stream, &objref);
}

static HRESULT WINAPI agg_std_unmarshaler_DisconnectObject(IMarshal *iface, DWORD reserved)
{
    return S_OK;
}

static const IMarshalVtbl agg_std_unmarshaler_vtbl =
{
    agg_std_unmarshaler_QueryInterface,
    agg_std_unmarshaler_AddRef,
    agg_std_unmarshaler_Release,
    agg_std_unmarshaler_GetUnmarshalClass,
    agg_std_unmarshaler_GetMarshalSizeMax,
    agg_std_unmarshaler_MarshalInterface,
    agg_std_unmarshaler_UnmarshalInterface,
    agg_std_unmarshaler_ReleaseMarshalData,
    agg_std_unmarshaler_DisconnectObject,
};

static HRESULT agg_std_unmarshaler_create(IMarshal **marshal)
{
    struct agg_std_unmarshaler *unmarshaler;

    if (!(unmarshaler = calloc(1, sizeof(*unmarshaler))))
        return E_OUTOFMEMORY;
    unmarshaler->IMarshal_iface.lpVtbl = &agg_std_unmarshaler_vtbl;
    unmarshaler->refcount = 1;
    *marshal = &unmarshaler->IMarshal_iface;
    return S_OK;
}

'''

ANCHOR = "/***********************************************************************\n *            CoGetStdMarshalEx        (combase.@)\n */\n"
sub(M, ANCHOR, HANDLER + ANCHOR, "struct std_marshal_handler")

sub(M, """    if (smexflags == SMEXF_HANDLER)
    {
        FIXME("SMEXF_HANDLER is not supported.\\n");
        apartment_release(apt);
        return E_NOTIMPL;
    }""",
"""    if (smexflags == SMEXF_HANDLER)
    {
        struct std_marshal_handler *handler;

        apartment_release(apt);
        /* only the identity created while unmarshaling handler data can aggregate the client half */
        if (outer->lpVtbl != &handler_identity_vtbl)
            return E_INVALIDARG;
        if (!(handler = calloc(1, sizeof(*handler))))
            return E_OUTOFMEMORY;
        handler->IUnknown_inner.lpVtbl = &std_marshal_handler_inner_vtbl;
        handler->IMarshal_iface.lpVtbl = &std_marshal_handler_vtbl;
        handler->refcount = 1;
        handler->outer = outer;
        *inner = &handler->IUnknown_inner;
        return S_OK;
    }""", "handler->outer = outer;")
