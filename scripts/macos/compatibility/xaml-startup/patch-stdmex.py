#!/usr/bin/env python3
# CoGetStdMarshalEx (SMEXF_SERVER) for Wine combase, semantics measured on native Windows 11 ARM64
# (audit-stdmex.c / audit-stdmex2.c). Upstream Wine 59416cf5, Proton and ReactOS only stub it.
import os, shutil, sys
W = sys.argv[1]
def rd(p): return open(os.path.join(W, p)).read()
def wr(p, s):
    f = os.path.join(W, p)
    if not os.path.exists(f + ".pre-stdmex"): shutil.copy2(f, f + ".pre-stdmex")
    open(f, "w").write(s)
def sub(p, old, new, marker):
    s = rd(p)
    if marker in s: print("already", p); return
    assert s.count(old) == 1, (p, old)
    wr(p, s.replace(old, new)); print("patched", p)

sub("dlls/combase/combase.spec", "@ stub CoGetStdMarshalEx\n",
    "@ stdcall CoGetStdMarshalEx(ptr long ptr)\n", "stdcall CoGetStdMarshalEx")
sub("dlls/ole32/ole32.spec",
    "@ stdcall CoGetStandardMarshal(ptr ptr long ptr long ptr) combase.CoGetStandardMarshal\n",
    "@ stdcall CoGetStandardMarshal(ptr ptr long ptr long ptr) combase.CoGetStandardMarshal\n"
    "@ stdcall CoGetStdMarshalEx(ptr long ptr) combase.CoGetStdMarshalEx\n", "CoGetStdMarshalEx")
OB = "WINOLE32API HRESULT WINAPI CoGetStandardMarshal(REFIID riid, LPUNKNOWN pUnk, DWORD dwDestContext, LPVOID pvDestContext, DWORD mshlflags, LPMARSHAL* ppMarshal);\n"
sub("include/objbase.h", OB, OB +
    "typedef enum tagSTDMSHLFLAGS\n{\n    SMEXF_SERVER  = 0x01,\n    SMEXF_HANDLER = 0x02\n} STDMSHLFLAGS;\n"
    "WINOLE32API HRESULT WINAPI CoGetStdMarshalEx(LPUNKNOWN pUnkOuter, DWORD smexflags, LPUNKNOWN *ppUnkInner);\n",
    "CoGetStdMarshalEx")

CODE = r'''

/* Aggregatable standard marshaler returned by CoGetStdMarshalEx(SMEXF_SERVER). The outer object
 * is not referenced (aggregation); IMarshal delegates IUnknown to it and the marshaling work to the
 * regular standard marshaler. While it is alive it represents the object's standard identity, so a
 * second request for the same object fails with RPC_E_TOO_LATE as on Windows. */
struct std_marshal_ex
{
    IUnknown IUnknown_inner;
    IMarshal IMarshal_iface;
    LONG refcount;
    IUnknown *outer;
    IMarshal *std;
    struct list entry;
};

static struct list std_marshal_ex_list = LIST_INIT(std_marshal_ex_list);
static CRITICAL_SECTION std_marshal_ex_cs;
static CRITICAL_SECTION_DEBUG std_marshal_ex_cs_debug =
{
    0, 0, &std_marshal_ex_cs,
    { &std_marshal_ex_cs_debug.ProcessLocksList, &std_marshal_ex_cs_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": std_marshal_ex_cs") }
};
static CRITICAL_SECTION std_marshal_ex_cs = { &std_marshal_ex_cs_debug, -1, 0, 0, 0, 0 };

static inline struct std_marshal_ex *std_marshal_ex_from_IUnknown(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, struct std_marshal_ex, IUnknown_inner);
}

static inline struct std_marshal_ex *std_marshal_ex_from_IMarshal(IMarshal *iface)
{
    return CONTAINING_RECORD(iface, struct std_marshal_ex, IMarshal_iface);
}

static HRESULT WINAPI std_marshal_ex_inner_QueryInterface(IUnknown *iface, REFIID riid, void **obj)
{
    struct std_marshal_ex *marshal = std_marshal_ex_from_IUnknown(iface);

    TRACE("%p, %s, %p.\n", iface, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IUnknown))
        *obj = &marshal->IUnknown_inner;
    else if (IsEqualIID(riid, &IID_IMarshal) || IsEqualIID(riid, &IID_IMarshal2))
        *obj = &marshal->IMarshal_iface;
    else
    {
        *obj = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown *)*obj);
    return S_OK;
}

static ULONG WINAPI std_marshal_ex_inner_AddRef(IUnknown *iface)
{
    struct std_marshal_ex *marshal = std_marshal_ex_from_IUnknown(iface);
    return InterlockedIncrement(&marshal->refcount);
}

static ULONG WINAPI std_marshal_ex_inner_Release(IUnknown *iface)
{
    struct std_marshal_ex *marshal = std_marshal_ex_from_IUnknown(iface);
    ULONG refcount = InterlockedDecrement(&marshal->refcount);

    if (!refcount)
    {
        EnterCriticalSection(&std_marshal_ex_cs);
        list_remove(&marshal->entry);
        LeaveCriticalSection(&std_marshal_ex_cs);
        IMarshal_Release(marshal->std);
        free(marshal);
    }

    return refcount;
}

static const IUnknownVtbl std_marshal_ex_inner_vtbl =
{
    std_marshal_ex_inner_QueryInterface,
    std_marshal_ex_inner_AddRef,
    std_marshal_ex_inner_Release,
};

static HRESULT WINAPI std_marshal_ex_QueryInterface(IMarshal *iface, REFIID riid, void **obj)
{
    struct std_marshal_ex *marshal = std_marshal_ex_from_IMarshal(iface);
    return IUnknown_QueryInterface(marshal->outer, riid, obj);
}

static ULONG WINAPI std_marshal_ex_AddRef(IMarshal *iface)
{
    struct std_marshal_ex *marshal = std_marshal_ex_from_IMarshal(iface);
    return IUnknown_AddRef(marshal->outer);
}

static ULONG WINAPI std_marshal_ex_Release(IMarshal *iface)
{
    struct std_marshal_ex *marshal = std_marshal_ex_from_IMarshal(iface);
    return IUnknown_Release(marshal->outer);
}

static HRESULT WINAPI std_marshal_ex_GetUnmarshalClass(IMarshal *iface, REFIID riid, void *pv,
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
}

static HRESULT WINAPI std_marshal_ex_UnmarshalInterface(IMarshal *iface, IStream *stream, REFIID riid, void **obj)
{
    struct std_marshal_ex *marshal = std_marshal_ex_from_IMarshal(iface);
    return IMarshal_UnmarshalInterface(marshal->std, stream, riid, obj);
}

static HRESULT WINAPI std_marshal_ex_ReleaseMarshalData(IMarshal *iface, IStream *stream)
{
    struct std_marshal_ex *marshal = std_marshal_ex_from_IMarshal(iface);
    return IMarshal_ReleaseMarshalData(marshal->std, stream);
}

static HRESULT WINAPI std_marshal_ex_DisconnectObject(IMarshal *iface, DWORD reserved)
{
    struct std_marshal_ex *marshal = std_marshal_ex_from_IMarshal(iface);
    struct stub_manager *manager;
    struct apartment *apt;

    TRACE("%p, %#lx.\n", iface, reserved);

    if (!(apt = apartment_get_current_or_mta()))
        return CO_E_NOTINITIALIZED;

    if ((manager = get_stub_manager_from_object(apt, marshal->outer, FALSE)))
    {
        stub_manager_disconnect(manager);
        /* Release stub manager twice, to remove the apartment reference. */
        stub_manager_int_release(manager);
        stub_manager_int_release(manager);
    }

    apartment_release(apt);
    return S_OK;
}

static const IMarshalVtbl std_marshal_ex_vtbl =
{
    std_marshal_ex_QueryInterface,
    std_marshal_ex_AddRef,
    std_marshal_ex_Release,
    std_marshal_ex_GetUnmarshalClass,
    std_marshal_ex_GetMarshalSizeMax,
    std_marshal_ex_MarshalInterface,
    std_marshal_ex_UnmarshalInterface,
    std_marshal_ex_ReleaseMarshalData,
    std_marshal_ex_DisconnectObject,
};

/***********************************************************************
 *            CoGetStdMarshalEx        (combase.@)
 */
HRESULT WINAPI CoGetStdMarshalEx(IUnknown *outer, DWORD smexflags, IUnknown **inner)
{
    struct std_marshal_ex *object, *cur;
    struct stub_manager *manager;
    struct apartment *apt;
    BOOL identity = FALSE;
    HRESULT hr;

    TRACE("%p, %#lx, %p.\n", outer, smexflags, inner);

    if (!outer || !inner || (smexflags != SMEXF_SERVER && smexflags != SMEXF_HANDLER))
        return E_INVALIDARG;

    *inner = NULL;

    if (!(apt = apartment_get_current_or_mta()))
        return CO_E_NOTINITIALIZED;

    if (smexflags == SMEXF_HANDLER)
    {
        FIXME("SMEXF_HANDLER is not supported.\n");
        apartment_release(apt);
        return E_NOTIMPL;
    }

    if (!(object = malloc(sizeof(*object))))
    {
        apartment_release(apt);
        return E_OUTOFMEMORY;
    }

    if (FAILED(hr = StdMarshalImpl_Construct(&IID_IMarshal, MSHCTX_INPROC, NULL, (void **)&object->std)))
    {
        free(object);
        apartment_release(apt);
        return hr;
    }

    object->IUnknown_inner.lpVtbl = &std_marshal_ex_inner_vtbl;
    object->IMarshal_iface.lpVtbl = &std_marshal_ex_vtbl;
    object->refcount = 1;
    object->outer = outer;

    EnterCriticalSection(&std_marshal_ex_cs);
    LIST_FOR_EACH_ENTRY(cur, &std_marshal_ex_list, struct std_marshal_ex, entry)
    {
        if (cur->outer == outer)
        {
            identity = TRUE;
            break;
        }
    }
    if (!identity && (manager = get_stub_manager_from_object(apt, outer, FALSE)))
    {
        identity = TRUE;
        stub_manager_int_release(manager);
    }
    if (!identity)
        list_add_tail(&std_marshal_ex_list, &object->entry);
    LeaveCriticalSection(&std_marshal_ex_cs);
    apartment_release(apt);

    if (identity)
    {
        IMarshal_Release(object->std);
        free(object);
        return RPC_E_TOO_LATE;
    }

    *inner = &object->IUnknown_inner;
    return S_OK;
}
'''
s = rd("dlls/combase/marshal.c")
if "CoGetStdMarshalEx" in s: print("already marshal.c")
else:
    assert s.endswith("}\n"); wr("dlls/combase/marshal.c", s + CODE.rstrip("\n")[0:] + "\n"); print("patched marshal.c")
