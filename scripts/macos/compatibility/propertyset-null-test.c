#define COBJMACROS
#include <stdio.h>
#include "windows.h"
#include "roapi.h"
#include "winstring.h"
#include "initguid.h"
DEFINE_GUID(IID_IInspectable, 0xaf86e2e0, 0xb12d, 0x4c6a, 0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90);
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"

static unsigned checks, failures;
static LONG refs = 1, destroyed;
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; printf("FAIL %u: %s\n", __LINE__, #expr); } } while (0)

static HRESULT WINAPI value_QI(IInspectable *iface, REFIID iid, void **out)
{
    *out = NULL;
    if (!IsEqualGUID(iid, &IID_IUnknown) && !IsEqualGUID(iid, &IID_IInspectable)) return E_NOINTERFACE;
    IInspectable_AddRef(iface);
    *out = iface;
    return S_OK;
}
static ULONG WINAPI value_AddRef(IInspectable *iface) { return InterlockedIncrement(&refs); }
static ULONG WINAPI value_Release(IInspectable *iface)
{
    LONG ref = InterlockedDecrement(&refs);
    if (!ref) InterlockedIncrement(&destroyed);
    return ref;
}
static HRESULT WINAPI value_GetIids(IInspectable *iface, ULONG *count, IID **iids) { return E_NOTIMPL; }
static HRESULT WINAPI value_GetName(IInspectable *iface, HSTRING *name) { return E_NOTIMPL; }
static HRESULT WINAPI value_GetTrust(IInspectable *iface, TrustLevel *trust) { return E_NOTIMPL; }
static const IInspectableVtbl value_vtbl =
{
    value_QI, value_AddRef, value_Release, value_GetIids, value_GetName, value_GetTrust,
};
static IInspectable value = {&value_vtbl};

static void check_null(IMapView_HSTRING_IInspectable *view, HSTRING key)
{
    IInspectable *out = (void *)0xdeadbeef;
    boolean exists = FALSE;
    CHECK(IMapView_HSTRING_IInspectable_HasKey(view, key, &exists) == S_OK && exists);
    CHECK(IMapView_HSTRING_IInspectable_Lookup(view, key, &out) == S_OK && !out);
}

int main(void)
{
    const WCHAR *class_name = RuntimeClass_Windows_Foundation_Collections_PropertySet;
    IPropertySet *set = NULL;
    IMap_HSTRING_IInspectable *map = NULL;
    IMapView_HSTRING_IInspectable *view = NULL, *first = NULL, *second = NULL;
    IIterable_IKeyValuePair_HSTRING_IInspectable *iterable = NULL;
    IIterator_IKeyValuePair_HSTRING_IInspectable *iterator = NULL;
    IKeyValuePair_HSTRING_IInspectable *pair = NULL, *null_pair = NULL;
    IInspectable *out;
    HSTRING name, alpha, beta, absent;
    boolean replaced, current;
    WCHAR path[MAX_PATH];
    UINT32 size;
    HRESULT hr;

    CHECK(RoInitialize(RO_INIT_MULTITHREADED) == S_OK);
    WindowsCreateString(class_name, wcslen(class_name), &name);
    WindowsCreateString(L"alpha", 5, &alpha);
    WindowsCreateString(L"beta", 4, &beta);
    WindowsCreateString(L"absent", 6, &absent);
    hr = RoActivateInstance(name, (IInspectable **)&set);
    CHECK(hr == S_OK && set);
    if (FAILED(hr)) goto done;
    CHECK(IPropertySet_QueryInterface(set, &IID_IMap_HSTRING_IInspectable, (void **)&map) == S_OK);
    IPropertySet_Release(set);
    if (!map) goto done;
    GetModuleFileNameW(GetModuleHandleW(L"wintypes.dll"), path, ARRAY_SIZE(path));
    printf("NULLMAP_PROVIDER=%ls\n", path);
    CHECK(IMap_HSTRING_IInspectable_Insert(map, alpha, NULL, &replaced) == S_OK && !replaced);
    CHECK(IMap_HSTRING_IInspectable_Insert(map, beta, &value, &replaced) == S_OK && !replaced && refs == 2);
    out = (void *)0xdeadbeef;
    CHECK(IMap_HSTRING_IInspectable_Lookup(map, alpha, &out) == S_OK && !out);
    CHECK(IMap_HSTRING_IInspectable_Lookup(map, absent, &out) == E_BOUNDS);
    CHECK(IMap_HSTRING_IInspectable_Remove(map, absent) == E_BOUNDS);
    CHECK(IMap_HSTRING_IInspectable_GetView(map, &view) == S_OK && refs == 3);
    if (!view) goto release_map;
    check_null(view, alpha);
    CHECK(IMapView_HSTRING_IInspectable_Lookup(view, absent, &out) == E_BOUNDS);
    CHECK(IMapView_HSTRING_IInspectable_Split(view, &first, &second) == S_OK && refs == 4);
    if (first) check_null(first, alpha);
    if (second)
    {
        CHECK(IMapView_HSTRING_IInspectable_Lookup(second, beta, &out) == S_OK && out == &value && refs == 5);
        if (out) IInspectable_Release(out);
    }
    CHECK(IMapView_HSTRING_IInspectable_QueryInterface(view, &IID_IIterable_IKeyValuePair_HSTRING_IInspectable,
        (void **)&iterable) == S_OK);
    if (iterable)
    {
        CHECK(IIterable_IKeyValuePair_HSTRING_IInspectable_First(iterable, &iterator) == S_OK);
        IIterable_IKeyValuePair_HSTRING_IInspectable_Release(iterable);
    }
    if (iterator)
    {
        CHECK(IIterator_IKeyValuePair_HSTRING_IInspectable_get_Current(iterator, &pair) == S_OK);
        if (pair)
        {
            out = (void *)0xdeadbeef;
            CHECK(IKeyValuePair_HSTRING_IInspectable_get_Value(pair, &out) == S_OK && !out);
            CHECK(IKeyValuePair_HSTRING_IInspectable_Release(pair) == 0);
        }
        CHECK(IIterator_IKeyValuePair_HSTRING_IInspectable_MoveNext(iterator, &current) == S_OK && current);
        CHECK(IIterator_IKeyValuePair_HSTRING_IInspectable_get_Current(iterator, &pair) == S_OK && refs == 5);
        if (pair)
        {
            CHECK(IKeyValuePair_HSTRING_IInspectable_get_Value(pair, &out) == S_OK && out == &value && refs == 6);
            if (out) IInspectable_Release(out);
            CHECK(IKeyValuePair_HSTRING_IInspectable_Release(pair) == 0 && refs == 4);
        }
        CHECK(IIterator_IKeyValuePair_HSTRING_IInspectable_Release(iterator) == 0);
    }
    if (first) CHECK(IMapView_HSTRING_IInspectable_Release(first) == 0);
    if (second) CHECK(IMapView_HSTRING_IInspectable_Release(second) == 0 && refs == 3);
    CHECK(IMapView_HSTRING_IInspectable_Release(view) == 0 && refs == 2);

    CHECK(IMap_HSTRING_IInspectable_Insert(map, beta, NULL, &replaced) == S_OK && replaced && refs == 1);
    CHECK(IMap_HSTRING_IInspectable_Insert(map, beta, &value, &replaced) == S_OK && replaced && refs == 2);
    CHECK(IMap_HSTRING_IInspectable_Remove(map, beta) == S_OK && refs == 1);
    CHECK(IMap_HSTRING_IInspectable_Remove(map, alpha) == S_OK);
    CHECK(IMap_HSTRING_IInspectable_Clear(map) == S_OK && refs == 1);
    CHECK(IMap_HSTRING_IInspectable_Insert(map, beta, &value, &replaced) == S_OK && refs == 2);
    CHECK(IMap_HSTRING_IInspectable_Insert(map, alpha, NULL, &replaced) == S_OK);
    CHECK(IMap_HSTRING_IInspectable_GetView(map, &view) == S_OK && refs == 3);
    if (view)
    {
        CHECK(IMapView_HSTRING_IInspectable_QueryInterface(view, &IID_IIterable_IKeyValuePair_HSTRING_IInspectable,
            (void **)&iterable) == S_OK);
        if (iterable)
        {
            CHECK(IIterable_IKeyValuePair_HSTRING_IInspectable_First(iterable, &iterator) == S_OK);
            IIterable_IKeyValuePair_HSTRING_IInspectable_Release(iterable);
            if (iterator)
            {
                CHECK(IIterator_IKeyValuePair_HSTRING_IInspectable_get_Current(iterator, &null_pair) == S_OK);
                CHECK(IIterator_IKeyValuePair_HSTRING_IInspectable_MoveNext(iterator, &current) == S_OK && current);
                CHECK(IIterator_IKeyValuePair_HSTRING_IInspectable_get_Current(iterator, &pair) == S_OK && refs == 4);
                IIterator_IKeyValuePair_HSTRING_IInspectable_Release(iterator);
            }
        }
        CHECK(IMap_HSTRING_IInspectable_Clear(map) == S_OK && refs == 3);
        CHECK(IMapView_HSTRING_IInspectable_get_Size(view, &size) == E_CHANGED_STATE);
        IMap_HSTRING_IInspectable_Release(map);
        map = NULL;
        CHECK(IMapView_HSTRING_IInspectable_Release(view) == 0 && refs == 2);
        if (null_pair)
        {
            out = (void *)0xdeadbeef;
            CHECK(IKeyValuePair_HSTRING_IInspectable_get_Value(null_pair, &out) == S_OK && !out);
            CHECK(IKeyValuePair_HSTRING_IInspectable_Release(null_pair) == 0);
        }
        if (pair)
        {
            CHECK(IKeyValuePair_HSTRING_IInspectable_get_Value(pair, &out) == S_OK && out == &value);
            if (out) IInspectable_Release(out);
            CHECK(IKeyValuePair_HSTRING_IInspectable_Release(pair) == 0 && refs == 1);
        }
    }
release_map:
    if (map) IMap_HSTRING_IInspectable_Release(map);
    CHECK(refs == 1 && IInspectable_Release(&value) == 0 && destroyed == 1);
done:
    WindowsDeleteString(name);
    WindowsDeleteString(alpha);
    WindowsDeleteString(beta);
    WindowsDeleteString(absent);
    RoUninitialize();
    printf("PropertySet nullable-value test: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
