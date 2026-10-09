#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"

struct attributes_view
{
    IMapView_HSTRING_IInspectable view;
    IIterable_IKeyValuePair_HSTRING_IInspectable iterable;
    LONG refs;
    IMapView_HSTRING_IInspectable *inner;
};
static const IMapView_HSTRING_IInspectableVtbl attributes_view_vtbl;
static const IIterable_IKeyValuePair_HSTRING_IInspectableVtbl attributes_iterable_vtbl;

static struct attributes_view *attributes_from_view(IMapView_HSTRING_IInspectable *iface)
{
    return CONTAINING_RECORD(iface, struct attributes_view, view);
}
static HRESULT attributes_wrap(IMapView_HSTRING_IInspectable *inner, IMapView_HSTRING_IInspectable **out)
{
    struct attributes_view *object;
    if (!inner || !out) return E_INVALIDARG;
    object = calloc(1, sizeof(*object));
    if (!object) return E_OUTOFMEMORY;
    object->view.lpVtbl = &attributes_view_vtbl;
    object->iterable.lpVtbl = &attributes_iterable_vtbl;
    object->refs = 1; object->inner = inner;
    IMapView_HSTRING_IInspectable_AddRef(inner);
    *out = &object->view;
    return S_OK;
}
static ULONG WINAPI attributes_addref(IMapView_HSTRING_IInspectable *iface)
{
    return InterlockedIncrement(&attributes_from_view(iface)->refs);
}
static ULONG WINAPI attributes_release(IMapView_HSTRING_IInspectable *iface)
{
    struct attributes_view *object = attributes_from_view(iface);
    ULONG refs = InterlockedDecrement(&object->refs);
    if (!refs) { IMapView_HSTRING_IInspectable_Release(object->inner); free(object); }
    return refs;
}
static HRESULT WINAPI attributes_query(IMapView_HSTRING_IInspectable *iface, REFIID iid, void **out)
{
    struct attributes_view *object = attributes_from_view(iface);
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IInspectable) ||
        IsEqualIID(iid, &IID_IMapView_HSTRING_IInspectable)) *out = iface;
    else if (IsEqualIID(iid, &IID_IIterable_IKeyValuePair_HSTRING_IInspectable)) *out = &object->iterable;
    else return E_NOINTERFACE;
    attributes_addref(iface);
    return S_OK;
}
static HRESULT WINAPI attributes_iids(IMapView_HSTRING_IInspectable *iface, ULONG *count, IID **out)
{
    if (!out || !count) return E_POINTER;
    *out = NULL; *count = 0;
    if (!(*out = CoTaskMemAlloc(2 * sizeof(**out)))) return E_OUTOFMEMORY;
    (*out)[0] = IID_IMapView_HSTRING_IInspectable;
    (*out)[1] = IID_IIterable_IKeyValuePair_HSTRING_IInspectable;
    *count = 2;
    return S_OK;
}
static HRESULT WINAPI attributes_name(IMapView_HSTRING_IInspectable *iface, HSTRING *out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    return E_NOTIMPL;
}
static HRESULT WINAPI attributes_trust(IMapView_HSTRING_IInspectable *iface, TrustLevel *out)
{
    return out ? E_NOTIMPL : E_POINTER;
}
static HRESULT WINAPI attributes_lookup(IMapView_HSTRING_IInspectable *iface, HSTRING key, IInspectable **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    return IMapView_HSTRING_IInspectable_Lookup(attributes_from_view(iface)->inner, key, out);
}
static HRESULT WINAPI attributes_size(IMapView_HSTRING_IInspectable *iface, UINT32 *out)
{
    return out ? IMapView_HSTRING_IInspectable_get_Size(attributes_from_view(iface)->inner, out) : E_POINTER;
}
static HRESULT WINAPI attributes_has(IMapView_HSTRING_IInspectable *iface, HSTRING key, boolean *out)
{
    return out ? IMapView_HSTRING_IInspectable_HasKey(attributes_from_view(iface)->inner, key, out) : E_POINTER;
}
static HRESULT WINAPI attributes_split(IMapView_HSTRING_IInspectable *iface,
    IMapView_HSTRING_IInspectable **first, IMapView_HSTRING_IInspectable **second)
{
    IMapView_HSTRING_IInspectable *a = NULL, *b = NULL;
    UINT32 size;
    HRESULT hr;
    if (!first || !second) return E_POINTER;
    *first = NULL; *second = NULL;
    if (FAILED(hr = attributes_size(iface, &size))) return hr;
    if (size < 2) return S_OK;
    if (size != 2) return E_NOTIMPL;
    hr = IMapView_HSTRING_IInspectable_Split(attributes_from_view(iface)->inner, &a, &b);
    if (SUCCEEDED(hr) && !a && !b) return S_OK;
    if (SUCCEEDED(hr)) hr = attributes_wrap(a, first);
    if (SUCCEEDED(hr)) hr = attributes_wrap(b, second);
    if (FAILED(hr) && *first) { attributes_release(*first); *first = NULL; }
    if (a) IMapView_HSTRING_IInspectable_Release(a);
    if (b) IMapView_HSTRING_IInspectable_Release(b);
    return hr;
}
static IMapView_HSTRING_IInspectable *attributes_iterable_view(IIterable_IKeyValuePair_HSTRING_IInspectable *iface)
{
    return &CONTAINING_RECORD(iface, struct attributes_view, iterable)->view;
}
static HRESULT WINAPI attributes_iterable_query(IIterable_IKeyValuePair_HSTRING_IInspectable *iface, REFIID iid, void **out)
{ return attributes_query(attributes_iterable_view(iface), iid, out); }
static ULONG WINAPI attributes_iterable_addref(IIterable_IKeyValuePair_HSTRING_IInspectable *iface)
{ return attributes_addref(attributes_iterable_view(iface)); }
static ULONG WINAPI attributes_iterable_release(IIterable_IKeyValuePair_HSTRING_IInspectable *iface)
{ return attributes_release(attributes_iterable_view(iface)); }
static HRESULT WINAPI attributes_iterable_iids(IIterable_IKeyValuePair_HSTRING_IInspectable *iface, ULONG *count, IID **out)
{ return attributes_iids(attributes_iterable_view(iface), count, out); }
static HRESULT WINAPI attributes_iterable_name(IIterable_IKeyValuePair_HSTRING_IInspectable *iface, HSTRING *out)
{ return attributes_name(attributes_iterable_view(iface), out); }
static HRESULT WINAPI attributes_iterable_trust(IIterable_IKeyValuePair_HSTRING_IInspectable *iface, TrustLevel *out)
{ return attributes_trust(attributes_iterable_view(iface), out); }
static HRESULT WINAPI attributes_first(IIterable_IKeyValuePair_HSTRING_IInspectable *iface,
    IIterator_IKeyValuePair_HSTRING_IInspectable **out)
{
    IIterable_IKeyValuePair_HSTRING_IInspectable *inner;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    hr = IMapView_HSTRING_IInspectable_QueryInterface(attributes_from_view(attributes_iterable_view(iface))->inner,
        &IID_IIterable_IKeyValuePair_HSTRING_IInspectable, (void **)&inner);
    if (FAILED(hr)) return hr;
    hr = IIterable_IKeyValuePair_HSTRING_IInspectable_First(inner, out);
    IIterable_IKeyValuePair_HSTRING_IInspectable_Release(inner);
    return hr;
}
static const IMapView_HSTRING_IInspectableVtbl attributes_view_vtbl =
{
    attributes_query, attributes_addref, attributes_release, attributes_iids, attributes_name, attributes_trust,
    attributes_lookup, attributes_size, attributes_has, attributes_split
};
static const IIterable_IKeyValuePair_HSTRING_IInspectableVtbl attributes_iterable_vtbl =
{
    attributes_iterable_query, attributes_iterable_addref, attributes_iterable_release, attributes_iterable_iids,
    attributes_iterable_name, attributes_iterable_trust, attributes_first
};

static HRESULT attributes_create(DWORD aliased, HSTRING entry_point, IMapView_HSTRING_IInspectable **out)
{
    HSTRING name = NULL, key = NULL;
    IInspectable *set = NULL, *value = NULL;
    IMap_HSTRING_IInspectable *map = NULL;
    IMapView_HSTRING_IInspectable *view = NULL;
    IPropertyValueStatics *statics = NULL;
    boolean replaced;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    hr = WindowsCreateString(L"Windows.Foundation.PropertyValue", 32, &name);
    if (SUCCEEDED(hr)) hr = RoGetActivationFactory(name, &IID_IPropertyValueStatics, (void **)&statics);
    WindowsDeleteString(name); name = NULL;
    if (FAILED(hr)) return hr;
    hr = WindowsCreateString(L"Windows.Foundation.Collections.PropertySet", 42, &name);
    if (SUCCEEDED(hr)) hr = RoActivateInstance(name, &set);
    WindowsDeleteString(name);
    if (SUCCEEDED(hr)) hr = IInspectable_QueryInterface(set, &IID_IMap_HSTRING_IInspectable, (void **)&map);
    if (SUCCEEDED(hr)) hr = WindowsCreateString(L"AppObject.Aliased", 17, &key);
    if (SUCCEEDED(hr)) hr = IPropertyValueStatics_CreateUInt32(statics, aliased, &value);
    if (SUCCEEDED(hr)) hr = IMap_HSTRING_IInspectable_Insert(map, key, value, &replaced);
    WindowsDeleteString(key); key = NULL;
    if (value) IInspectable_Release(value);
    value = NULL;
    if (SUCCEEDED(hr)) hr = WindowsCreateString(L"AppObject.EntryPoint", 20, &key);
    if (SUCCEEDED(hr)) hr = IPropertyValueStatics_CreateString(statics, entry_point, &value);
    if (SUCCEEDED(hr)) hr = IMap_HSTRING_IInspectable_Insert(map, key, value, &replaced);
    if (SUCCEEDED(hr)) hr = IMap_HSTRING_IInspectable_GetView(map, &view);
    if (SUCCEEDED(hr)) hr = attributes_wrap(view, out);
    WindowsDeleteString(key);
    if (value) IInspectable_Release(value);
    if (view) IMapView_HSTRING_IInspectable_Release(view);
    if (map) IMap_HSTRING_IInspectable_Release(map);
    if (set) IInspectable_Release(set);
    IPropertyValueStatics_Release(statics);
    return hr;
}
