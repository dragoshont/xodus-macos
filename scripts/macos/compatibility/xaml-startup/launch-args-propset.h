/* Shared by args-propset-probe.c and activate-app.c: a logging
 * IMap<HSTRING, IInspectable> handed to twinapi.appcore's cloaked
 * IValueUnmarshalByPropertySet::UnmarshalObjectFromPropertySet, the measured
 * native route for materialising activated event arguments. Values are
 * genuine Windows.Foundation.PropertyValue objects. */
#ifndef XODUS_LAUNCH_ARGS_PROPSET_H
#define XODUS_LAUNCH_ARGS_PROPSET_H
#include <windows.h>
#include <objbase.h>
#include <roapi.h>
#include <winstring.h>
#include <stdio.h>
#include <stdlib.h>
static const GUID IID_IValueUnmarshalByPropertySet_ =
    {0xecabe149, 0x4833, 0x452b, {0x81, 0x12, 0x3e, 0xee, 0xc6, 0xd2, 0x0c, 0xb0}};
static const GUID IID_IMap_HSTRING_IInspectable =
    {0x1b0d3570, 0x0877, 0x5ec2, {0x8a, 0x2c, 0x3b, 0x95, 0x39, 0x50, 0x6a, 0xca}};
static const GUID IID_IPropertySet_ =
    {0x8a43ed9f, 0xf4e6, 0x4421, {0xac, 0xf9, 0x1d, 0xab, 0x29, 0x86, 0x82, 0x0c}};
static const GUID IID_IInspectable_ =
    {0xaf86e2e0, 0xb12d, 0x4c6a, {0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90}};
static const GUID IID_IPropertyValueStatics_ =
    {0x629bdbc8, 0xd932, 0x4ff4, {0x96, 0xb9, 0x8d, 0x96, 0xc5, 0xc1, 0xe8, 0x58}};
static const GUID IID_IActivatedEventArgs_ =
    {0xcf651713, 0xcd08, 0x4fd8, {0xb6, 0x97, 0xa2, 0x81, 0xb6, 0x54, 0x4e, 0x2e}};
static const GUID IID_ILaunchActivatedEventArgs_ =
    {0xfbc93e26, 0xa14a, 0x4b4f, {0x82, 0xb0, 0x33, 0xbe, 0xd9, 0x20, 0xaf, 0x52}};

typedef HRESULT (WINAPI *get_factory_fn)(HSTRING, IUnknown **);
typedef HRESULT (WINAPI *fn_create_bool)(void *, BOOLEAN, IUnknown **);
typedef HRESULT (WINAPI *fn_create_i32)(void *, INT32, IUnknown **);
typedef HRESULT (WINAPI *fn_create_u32)(void *, UINT32, IUnknown **);
typedef HRESULT (WINAPI *fn_create_i64)(void *, INT64, IUnknown **);
typedef HRESULT (WINAPI *fn_create_u64)(void *, UINT64, IUnknown **);
typedef HRESULT (WINAPI *fn_create_str)(void *, HSTRING, IUnknown **);
typedef HRESULT (WINAPI *fn_create_guid)(void *, GUID, IUnknown **);

#define MAX_ENTRIES 64
struct entry { WCHAR key[128]; IUnknown *value; };

struct map
{
    const void *propset_vtbl; /* IPropertySet : IInspectable */
    const void *map_vtbl;     /* IMap<HSTRING, IInspectable *> */
    LONG ref;
    struct entry entries[MAX_ENTRIES];
    UINT32 count;
};

static void print_guid(const GUID *g)
{
    printf("{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", g->Data1, g->Data2, g->Data3,
           g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

static struct map *impl_from_propset(void *iface) { return CONTAINING_RECORD(iface, struct map, propset_vtbl); }
static struct map *impl_from_map(void *iface) { return CONTAINING_RECORD(iface, struct map, map_vtbl); }

static HRESULT map_qi(struct map *m, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable_) || IsEqualGUID(iid, &IID_IPropertySet_))
        *out = &m->propset_vtbl;
    else if (IsEqualGUID(iid, &IID_IMap_HSTRING_IInspectable))
        *out = &m->map_vtbl;
    else
    {
        printf("  map QI "); print_guid(iid); printf(" -> E_NOINTERFACE\n");
        *out = NULL;
        return E_NOINTERFACE;
    }
    printf("  map QI "); print_guid(iid); printf(" -> S_OK\n");
    InterlockedIncrement(&m->ref);
    return S_OK;
}

static struct entry *find(struct map *m, HSTRING key)
{
    const WCHAR *k = WindowsGetStringRawBuffer(key, NULL);
    UINT32 i;
    for (i = 0; i < m->count; ++i) if (!lstrcmpW(m->entries[i].key, k)) return &m->entries[i];
    return NULL;
}

static HRESULT WINAPI ps_QueryInterface(void *iface, REFIID iid, void **out) { return map_qi(impl_from_propset(iface), iid, out); }
static ULONG WINAPI ps_AddRef(void *iface) { return InterlockedIncrement(&impl_from_propset(iface)->ref); }
static ULONG WINAPI ps_Release(void *iface) { return InterlockedDecrement(&impl_from_propset(iface)->ref); }
static HRESULT WINAPI ps_GetIids(void *iface, ULONG *n, IID **iids) { printf("  GetIids\n"); return E_NOTIMPL; }
static HRESULT WINAPI ps_GetRuntimeClassName(void *iface, HSTRING *name) { printf("  GetRuntimeClassName\n"); return E_NOTIMPL; }
static HRESULT WINAPI ps_GetTrustLevel(void *iface, int *level) { *level = 0; return S_OK; }
static const void *propset_vtbl[] = { ps_QueryInterface, ps_AddRef, ps_Release, ps_GetIids, ps_GetRuntimeClassName, ps_GetTrustLevel };

static HRESULT WINAPI m_QueryInterface(void *iface, REFIID iid, void **out) { return map_qi(impl_from_map(iface), iid, out); }
static ULONG WINAPI m_AddRef(void *iface) { return InterlockedIncrement(&impl_from_map(iface)->ref); }
static ULONG WINAPI m_Release(void *iface) { return InterlockedDecrement(&impl_from_map(iface)->ref); }
static HRESULT WINAPI m_GetIids(void *iface, ULONG *n, IID **iids) { return E_NOTIMPL; }
static HRESULT WINAPI m_GetRuntimeClassName(void *iface, HSTRING *name) { return E_NOTIMPL; }
static HRESULT WINAPI m_GetTrustLevel(void *iface, int *level) { *level = 0; return S_OK; }
static HRESULT WINAPI m_Lookup(void *iface, HSTRING key, IUnknown **value)
{
    struct entry *e = find(impl_from_map(iface), key);
    printf("  Lookup(%ls) -> %s\n", WindowsGetStringRawBuffer(key, NULL), e ? "S_OK" : "E_BOUNDS");
    *value = NULL;
    if (!e) return E_BOUNDS;
    if ((*value = e->value)) IUnknown_AddRef(*value);
    return S_OK;
}
static HRESULT WINAPI m_get_Size(void *iface, UINT32 *size)
{
    *size = impl_from_map(iface)->count;
    printf("  get_Size -> %u\n", *size);
    return S_OK;
}
static HRESULT WINAPI m_HasKey(void *iface, HSTRING key, BOOLEAN *found)
{
    *found = !!find(impl_from_map(iface), key);
    printf("  HasKey(%ls) -> %d\n", WindowsGetStringRawBuffer(key, NULL), *found);
    return S_OK;
}
static HRESULT WINAPI m_GetView(void *iface, void **view) { printf("  GetView\n"); *view = NULL; return E_NOTIMPL; }
static HRESULT WINAPI m_Insert(void *iface, HSTRING key, IUnknown *value, BOOLEAN *replaced)
{ printf("  Insert(%ls)\n", WindowsGetStringRawBuffer(key, NULL)); return E_NOTIMPL; }
static HRESULT WINAPI m_Remove(void *iface, HSTRING key) { printf("  Remove(%ls)\n", WindowsGetStringRawBuffer(key, NULL)); return E_NOTIMPL; }
static HRESULT WINAPI m_Clear(void *iface) { printf("  Clear\n"); return E_NOTIMPL; }
static const void *map_vtbl[] = { m_QueryInterface, m_AddRef, m_Release, m_GetIids, m_GetRuntimeClassName, m_GetTrustLevel,
                                  m_Lookup, m_get_Size, m_HasKey, m_GetView, m_Insert, m_Remove, m_Clear };

static HRESULT add_value(struct map *m, void **statics, const char *spec)
{
    char key[128], type;
    const char *eq = strchr(spec, '='), *val;
    void **vt = *(void ***)statics;
    IUnknown *value = NULL;
    HRESULT hr;
    if (!eq || eq - spec >= (int)sizeof(key) || !eq[1] || eq[2] != ':' || m->count >= MAX_ENTRIES) return E_INVALIDARG;
    memcpy(key, spec, eq - spec); key[eq - spec] = 0;
    type = eq[1]; val = eq + 3;
    switch (type)
    {
    case 'b': hr = ((fn_create_bool)vt[17])(statics, (BOOLEAN)atoi(val), &value); break;
    case 'i': hr = ((fn_create_i32)vt[10])(statics, strtol(val, NULL, 0), &value); break;
    case 'u': hr = ((fn_create_u32)vt[11])(statics, strtoul(val, NULL, 0), &value); break;
    case 'l': hr = ((fn_create_i64)vt[12])(statics, _strtoi64(val, NULL, 0), &value); break;
    case 'q': hr = ((fn_create_u64)vt[13])(statics, _strtoui64(val, NULL, 0), &value); break;
    case 's':
    {
        WCHAR buffer[1024];
        HSTRING s = NULL;
        MultiByteToWideChar(CP_UTF8, 0, val, -1, buffer, ARRAYSIZE(buffer));
        hr = WindowsCreateString(buffer, lstrlenW(buffer), &s);
        if (SUCCEEDED(hr)) hr = ((fn_create_str)vt[18])(statics, s, &value);
        WindowsDeleteString(s);
        break;
    }
    case 'g':
    {
        WCHAR buffer[64];
        GUID g;
        MultiByteToWideChar(CP_UTF8, 0, val, -1, buffer, ARRAYSIZE(buffer));
        hr = CLSIDFromString(buffer, &g);
        if (SUCCEEDED(hr)) hr = ((fn_create_guid)vt[20])(statics, g, &value);
        break;
    }
    case 'n': hr = S_OK; break;
    default: return E_INVALIDARG;
    }
    if (FAILED(hr)) return hr;
    MultiByteToWideChar(CP_UTF8, 0, key, -1, m->entries[m->count].key, ARRAYSIZE(m->entries[m->count].key));
    m->entries[m->count++].value = value;
    return S_OK;
}

typedef HRESULT (WINAPI *fn_unmarshal)(void *, void *, IUnknown **);

static HRESULT get_property_value_statics(void **statics)
{
    static const WCHAR pv_class[] = L"Windows.Foundation.PropertyValue";
    HSTRING name = NULL;
    HRESULT hr = WindowsCreateString(pv_class, lstrlenW(pv_class), &name);
    if (SUCCEEDED(hr)) hr = RoGetActivationFactory(name, &IID_IPropertyValueStatics_, statics);
    WindowsDeleteString(name);
    return hr;
}

/* Materialise a genuine twinapi.appcore activated-event-args object of
 * runtime class `args_class` from the property map. */
static HRESULT unmarshal_args_from_map(const WCHAR *args_class, struct map *m, IUnknown **result)
{
    HMODULE module = LoadLibraryW(L"twinapi.appcore.dll");
    get_factory_fn get_factory = module ? (get_factory_fn)GetProcAddress(module, "DllGetActivationFactory") : NULL;
    IUnknown *factory = NULL;
    void **unmarshal = NULL;
    HSTRING name = NULL;
    HRESULT hr;

    *result = NULL;
    if (!get_factory) return HRESULT_FROM_WIN32(GetLastError());
    hr = WindowsCreateString(args_class, lstrlenW(args_class), &name);
    if (SUCCEEDED(hr)) hr = get_factory(name, &factory);
    WindowsDeleteString(name);
    printf("%ls factory hr=0x%08lx\n", args_class, hr);
    if (FAILED(hr)) return hr;
    hr = IUnknown_QueryInterface(factory, &IID_IValueUnmarshalByPropertySet_, (void **)&unmarshal);
    printf("QI IValueUnmarshalByPropertySet hr=0x%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        printf("UnmarshalObjectFromPropertySet with %u entries ...\n", m->count);
        hr = ((fn_unmarshal)(*(void ***)unmarshal)[6])(unmarshal, &m->propset_vtbl, result);
        printf("UnmarshalObjectFromPropertySet hr=0x%08lx result=%p\n", hr, *result);
        IUnknown_Release((IUnknown *)unmarshal);
    }
    IUnknown_Release(factory);
    return hr;
}
#endif