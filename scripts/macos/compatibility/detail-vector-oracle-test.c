#define main propertyset_null_main
#include "propertyset-null-test.c"
#undef main
#include <stddef.h>
#include "bcrypt.h"
#define WIDL_using_Windows_ApplicationModel_Core
#define WIDL_using_Windows_UI_Core
#include "windows.applicationmodel.core.h"

#ifndef _WIN64
#error This oracle describes only the verified x64 caller.
#endif

static const WCHAR alias_path[] =
    L"Y:\\xodus-app-tooling\\cuphead\\detail-vector-candidate\\xodus-vector-oracle.dll";
static const WCHAR wine_path[] = L"C:\\windows\\system32\\wintypes.dll";
static const WCHAR vector_class[] = L"Windows.Foundation.Collections.Detail.Vector";
static const GUID activation_iid = {0x00000035,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static const GUID agile_iid = {0x94ea2b94,0xe9cc,0x49e0,{0xc0,0xff,0xee,0x64,0xca,0x8f,0x5b,0x90}};
static const GUID factory_iid = {0x08c77958,0x89bf,0x5cf8,{0xa9,0xcd,0xc7,0x21,0x47,0xb9,0xb3,0xa9}};
static const GUID vector_iid = {0x8af29ecf,0x6cc4,0x5a34,{0x99,0xc6,0xcd,0x90,0x69,0x80,0x92,0x53}};
static const GUID view_iid = {0xde9e16c4,0x1b7c,0x5126,{0xb1,0xd8,0x7c,0xd0,0x4f,0x13,0xbd,0x08}};
static const GUID iterable_iid = {0x32bc12d1,0x2653,0x5a41,{0xa5,0x5e,0x88,0xa1,0x2a,0xf2,0x02,0x6a}};
static const GUID iterator_iid = {0x4f5f6944,0x264b,0x5868,{0x80,0x9e,0xc7,0xac,0x1a,0xc5,0xed,0xad}};
static const GUID unknown_iid = {0xfd529846,0x955c,0x4bbc,{0x95,0x35,0xb4,0x34,0x88,0x4b,0x65,0x12}};
static unsigned vector_checks, vector_failures;
#define VECTOR_CHECK(expr) do { ++vector_checks; if (!(expr)) { ++vector_failures; \
    printf("VECTOR_FAIL %u: %s\n", __LINE__, #expr); } } while (0)

struct vector_descriptor
{
    HSTRING vector_name, view_name, iterator_name;
    GUID element, vector, view, iterable, iterator;
};
struct vector_factory_vtbl
{
    IInspectableVtbl base;
    void *uncalled_slots_6_to_29[24];
    HRESULT (WINAPI *create_object_vector)(IInspectable *, const struct vector_descriptor *, IInspectable **);
};
C_ASSERT(sizeof(struct vector_descriptor) == 104);
C_ASSERT(offsetof(struct vector_descriptor, element) == 24);
C_ASSERT(offsetof(struct vector_descriptor, iterator) == 88);
C_ASSERT(offsetof(struct vector_factory_vtbl, create_object_vector) == 30 * sizeof(void *));

struct mock_view { ICoreApplicationView iface; LONG ref, destroyed; };
static struct mock_view *mock_from(ICoreApplicationView *iface)
{
    return CONTAINING_RECORD(iface, struct mock_view, iface);
}
static ULONG WINAPI mock_addref(ICoreApplicationView *iface)
{
    return InterlockedIncrement(&mock_from(iface)->ref);
}
static ULONG WINAPI mock_release(ICoreApplicationView *iface)
{
    struct mock_view *mock = mock_from(iface);
    LONG ref = InterlockedDecrement(&mock->ref);
    if (!ref) InterlockedIncrement(&mock->destroyed);
    return ref;
}
static HRESULT WINAPI mock_qi(ICoreApplicationView *iface, REFIID iid, void **out)
{
    *out = NULL;
    if (!IsEqualGUID(iid, &IID_IUnknown) && !IsEqualGUID(iid, &IID_IInspectable) &&
        !IsEqualGUID(iid, &IID_ICoreApplicationView) && !IsEqualGUID(iid, &agile_iid)) return E_NOINTERFACE;
    *out = iface;
    mock_addref(iface);
    return S_OK;
}
static HRESULT WINAPI mock_iids(ICoreApplicationView *iface, ULONG *count, IID **iids)
{
    *count = 0;
    if (!(*iids = CoTaskMemAlloc(sizeof(**iids)))) return E_OUTOFMEMORY;
    **iids = IID_ICoreApplicationView;
    *count = 1;
    return S_OK;
}
static HRESULT WINAPI mock_name(ICoreApplicationView *iface, HSTRING *name)
{
    const WCHAR text[] = L"Windows.ApplicationModel.Core.CoreApplicationView";
    return WindowsCreateString(text, ARRAY_SIZE(text) - 1, name);
}
static HRESULT WINAPI mock_trust(ICoreApplicationView *iface, TrustLevel *level)
{
    *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI mock_window(ICoreApplicationView *iface, ICoreWindow **window)
{
    puts("MOCK_UNSUPPORTED get_CoreWindow");
    *window = NULL;
    return E_NOTIMPL;
}
static HRESULT WINAPI mock_add(ICoreApplicationView *iface,
    ITypedEventHandler_CoreApplicationView_IActivatedEventArgs *handler, EventRegistrationToken *token)
{
    puts("MOCK_UNSUPPORTED add_Activated");
    token->value = 0;
    return E_NOTIMPL;
}
static HRESULT WINAPI mock_remove(ICoreApplicationView *iface, EventRegistrationToken token)
{
    puts("MOCK_UNSUPPORTED remove_Activated");
    return E_NOTIMPL;
}
static HRESULT WINAPI mock_bool(ICoreApplicationView *iface, boolean *out)
{
    *out = FALSE;
    return S_OK;
}
static const ICoreApplicationViewVtbl mock_vtbl = {
    mock_qi, mock_addref, mock_release, mock_iids, mock_name, mock_trust,
    mock_window, mock_add, mock_remove, mock_bool, mock_bool
};

static BOOL module_pin(HMODULE module, const WCHAR *expected_path, const char *expected_hash)
{
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    WCHAR path[1024];
    BYTE block[16384], digest[32];
    DWORD bytes, length;
    HANDLE file = INVALID_HANDLE_VALUE;
    char hex[65];
    unsigned i;
    BOOL ok = FALSE;
    NTSTATUS status;

    length = GetModuleFileNameW(module, path, ARRAY_SIZE(path));
    if (!length || length >= ARRAY_SIZE(path) || _wcsicmp(path, expected_path)) goto done;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                      NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) goto done;
    status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    if (status) { printf("SHA256_OPEN status=%08lx\n", (ULONG)status); goto done; }
    status = BCryptCreateHash(algorithm, &hash, NULL, 0, NULL, 0, 0);
    if (status) { printf("SHA256_CREATE status=%08lx\n", (ULONG)status); goto done; }
    for (;;)
    {
        if (!ReadFile(file, block, sizeof(block), &bytes, NULL)) goto done;
        if (!bytes) break;
        status = BCryptHashData(hash, block, bytes, 0);
        if (status) { printf("SHA256_DATA status=%08lx\n", (ULONG)status); goto done; }
    }
    status = BCryptFinishHash(hash, digest, sizeof(digest), 0);
    if (status) { printf("SHA256_FINISH status=%08lx\n", (ULONG)status); goto done; }
    for (i = 0; i < sizeof(digest); ++i) sprintf(hex + 2 * i, "%02x", digest[i]);
    printf("MODULE_PIN path=%ls sha256=%s\n", path, hex);
    ok = !strcmp(hex, expected_hash);
done:
    if (!ok) printf("MODULE_PIN_FAILED expected=%ls error=%lu\n", expected_path, GetLastError());
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    return ok;
}

static void vector_controls(IInspectable *factory)
{
    const WCHAR *names[] = {
        L"Windows.Foundation.Collections.IVector`1<Windows.ApplicationModel.Core.CoreApplicationView>",
        L"Windows.Foundation.Collections.IVectorView`1<Windows.ApplicationModel.Core.CoreApplicationView>",
        L"Windows.Foundation.Collections.IIterator`1<Windows.ApplicationModel.Core.CoreApplicationView>"
    };
    struct vector_descriptor descriptor = {0};
    const struct vector_factory_vtbl *vtbl = (const void *)factory->lpVtbl;
    struct mock_view a = {{&mock_vtbl}, 1, 0}, b = {{&mock_vtbl}, 1, 0};
    IInspectable *raw = NULL, *out = NULL;
    IVector_IInspectable *vector = NULL;
    IVectorView_IInspectable *view = NULL;
    IIterable_IInspectable *iterable = NULL;
    IIterator_IInspectable *iterator = NULL;
    HSTRING name = NULL;
    HRESULT hr;
    UINT32 size = 0, index = 0;
    boolean found = FALSE, current = FALSE;
    unsigned i;

    descriptor.element = IID_ICoreApplicationView;
    descriptor.vector = vector_iid;
    descriptor.view = view_iid;
    descriptor.iterable = iterable_iid;
    descriptor.iterator = iterator_iid;
    for (i = 0; i < ARRAY_SIZE(names); ++i)
    {
        HSTRING *field = i == 0 ? &descriptor.vector_name : i == 1 ? &descriptor.view_name : &descriptor.iterator_name;
        hr = WindowsCreateString(names[i], wcslen(names[i]), field);
        VECTOR_CHECK(hr == S_OK);
        if (FAILED(hr)) goto done;
    }
    hr = vtbl->create_object_vector(factory, &descriptor, &raw);
    printf("SLOT30 hr=%08lx object=%d\n", (ULONG)hr, raw != NULL);
    VECTOR_CHECK(hr == S_OK && raw);
    if (FAILED(hr) || !raw) goto done;
    hr = IInspectable_QueryInterface(raw, &vector_iid, (void **)&vector);
    VECTOR_CHECK(hr == S_OK && vector);
    if (FAILED(hr) || !vector) goto done;
    WindowsDeleteString(descriptor.vector_name); descriptor.vector_name = NULL;
    WindowsDeleteString(descriptor.view_name); descriptor.view_name = NULL;
    WindowsDeleteString(descriptor.iterator_name); descriptor.iterator_name = NULL;
    hr = IInspectable_GetRuntimeClassName(raw, &name);
    printf("VECTOR_NAME hr=%08lx name=%ls\n", (ULONG)hr,
           name ? WindowsGetStringRawBuffer(name, NULL) : L"(null)");
    VECTOR_CHECK(hr == S_OK && name);
    WindowsDeleteString(name); name = NULL;
    IInspectable_Release(raw); raw = NULL;
    VECTOR_CHECK(IVector_IInspectable_get_Size(vector, &size) == S_OK && !size);
    hr = IVector_IInspectable_GetAt(vector, 0, &out);
    VECTOR_CHECK(hr == E_BOUNDS);
    printf("EMPTY_GETAT hr=%08lx object=%d\n", (ULONG)hr, out != NULL);
    if (SUCCEEDED(hr) && out) IInspectable_Release(out);
    out = NULL;
    VECTOR_CHECK(IVector_IInspectable_Append(vector, (IInspectable *)&a.iface) == S_OK && a.ref > 1);
    VECTOR_CHECK(IVector_IInspectable_Append(vector, (IInspectable *)&b.iface) == S_OK && b.ref > 1);
    VECTOR_CHECK(IVector_IInspectable_GetAt(vector, 0, &out) == S_OK && out == (IInspectable *)&a.iface);
    if (out) IInspectable_Release(out);
    out = NULL;
    VECTOR_CHECK(IVector_IInspectable_IndexOf(vector, (IInspectable *)&b.iface, &index, &found) == S_OK &&
                 found && index == 1);
    VECTOR_CHECK(IVector_IInspectable_SetAt(vector, 0, (IInspectable *)&b.iface) == S_OK && a.ref == 1);
    VECTOR_CHECK(IVector_IInspectable_InsertAt(vector, 0, (IInspectable *)&a.iface) == S_OK);
    VECTOR_CHECK(IVector_IInspectable_RemoveAt(vector, 2) == S_OK);
    VECTOR_CHECK(IVector_IInspectable_Clear(vector) == S_OK && a.ref == 1 && b.ref == 1);
    VECTOR_CHECK(IVector_IInspectable_Append(vector, (IInspectable *)&a.iface) == S_OK);
    hr = IVector_IInspectable_GetView(vector, &view);
    VECTOR_CHECK(hr == S_OK && view);
    if (FAILED(hr) || !view) goto done;
    VECTOR_CHECK(IInspectable_QueryInterface((IInspectable *)view, &view_iid, (void **)&out) == S_OK && out);
    if (out) IInspectable_Release(out);
    out = NULL;
    hr = IVectorView_IInspectable_QueryInterface(view, &iterable_iid, (void **)&iterable);
    VECTOR_CHECK(hr == S_OK && iterable);
    if (FAILED(hr) || !iterable) goto done;
    hr = IIterable_IInspectable_First(iterable, &iterator);
    VECTOR_CHECK(hr == S_OK && iterator);
    if (FAILED(hr) || !iterator) goto done;
    VECTOR_CHECK(IIterator_IInspectable_QueryInterface(iterator, &iterator_iid, (void **)&out) == S_OK && out);
    if (out) IInspectable_Release(out);
    out = NULL;
    IIterable_IInspectable_Release(iterable); iterable = NULL;
    IVector_IInspectable_Release(vector); vector = NULL;
    VECTOR_CHECK(IVectorView_IInspectable_get_Size(view, &size) == S_OK && size == 1);
    VECTOR_CHECK(IVectorView_IInspectable_GetAt(view, 0, &out) == S_OK && out == (IInspectable *)&a.iface);
    if (out) IInspectable_Release(out);
    out = NULL;
    IVectorView_IInspectable_Release(view); view = NULL;
    VECTOR_CHECK(IIterator_IInspectable_get_HasCurrent(iterator, &current) == S_OK && current);
    VECTOR_CHECK(IIterator_IInspectable_get_Current(iterator, &out) == S_OK && out == (IInspectable *)&a.iface);
    if (out) IInspectable_Release(out);
    out = NULL;
    VECTOR_CHECK(IIterator_IInspectable_MoveNext(iterator, &current) == S_OK && !current);
done:
    if (out) IInspectable_Release(out);
    if (iterator) IIterator_IInspectable_Release(iterator);
    if (iterable) IIterable_IInspectable_Release(iterable);
    if (view) IVectorView_IInspectable_Release(view);
    if (vector) IVector_IInspectable_Release(vector);
    if (raw) IInspectable_Release(raw);
    WindowsDeleteString(descriptor.vector_name);
    WindowsDeleteString(descriptor.view_name);
    WindowsDeleteString(descriptor.iterator_name);
    printf("VECTOR_REFS a=%ld b=%ld\n", a.ref, b.ref);
    VECTOR_CHECK(a.ref == 1 && b.ref == 1);
    VECTOR_CHECK(mock_release(&a.iface) == 0 && a.destroyed == 1);
    VECTOR_CHECK(mock_release(&b.iface) == 0 && b.destroyed == 1);
}

int main(int argc, char **argv)
{
    typedef HRESULT (WINAPI *factory_fn)(HSTRING, IInspectable **);
    HMODULE wine = NULL, native = NULL, method_module = NULL;
    IInspectable *activation = NULL, *factory = NULL;
    IUnknown *identity = NULL, *other = NULL;
    GUID *iids = NULL;
    ULONG count = 0;
    HSTRING name = NULL;
    HRESULT hr;
    factory_fn get_factory;
    unsigned i;
    BOOL initialized = FALSE, catalog, absent;
    int null_result = 1;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc != 2 || (strcmp(argv[1], "direct") && strcmp(argv[1], "catalog") && strcmp(argv[1], "absent")))
    {
        puts("Usage: detail-vector-oracle-test.exe direct|catalog|absent");
        return 2;
    }
    catalog = !strcmp(argv[1], "catalog");
    absent = !strcmp(argv[1], "absent");
    wine = LoadLibraryW(L"wintypes.dll");
    VECTOR_CHECK(wine && module_pin(wine, wine_path,
        "90900b71e275676d61cff3b82c3b0c1968b9de453368b9836c9e71f6862d2438"));
    if (vector_failures) goto done;
    hr = RoInitialize(RO_INIT_MULTITHREADED);
    VECTOR_CHECK(hr == S_OK);
    if (FAILED(hr)) goto done;
    initialized = TRUE;
    VECTOR_CHECK(WindowsCreateString(vector_class, ARRAY_SIZE(vector_class) - 1, &name) == S_OK);
    if (!name) goto done;
    if (absent)
    {
        hr = RoGetActivationFactory(name, &factory_iid, (void **)&factory);
        printf("ABSENT_FACTORY hr=%08lx object=%d\n", (ULONG)hr, factory != NULL);
        VECTOR_CHECK(hr == REGDB_E_CLASSNOTREG && !factory);
        goto done;
    }
    native = LoadLibraryW(alias_path);
    VECTOR_CHECK(native && native != wine && module_pin(native, alias_path,
        "5a94b019c7190d9ec47f98e0bc28dced70036f1f97e9a84ef9d75340e364158a"));
    if (vector_failures) goto done;
    get_factory = (factory_fn)(void *)GetProcAddress(native, "DllGetActivationFactory");
    VECTOR_CHECK(get_factory != NULL);
    if (!get_factory) goto done;
    hr = catalog ? RoGetActivationFactory(name, &activation_iid, (void **)&activation)
                 : get_factory(name, &activation);
    printf("NATIVE_FACTORY mode=%s hr=%08lx object=%d\n", argv[1], (ULONG)hr, activation != NULL);
    VECTOR_CHECK(hr == S_OK && activation);
    if (FAILED(hr) || !activation) goto done;
    hr = IInspectable_QueryInterface(activation, &factory_iid, (void **)&factory);
    VECTOR_CHECK(hr == S_OK && factory);
    if (FAILED(hr) || !factory) goto done;
    VECTOR_CHECK(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (const WCHAR *)(ULONG_PTR)((const struct vector_factory_vtbl *)factory->lpVtbl)->create_object_vector,
        &method_module) && method_module == native);
    if (vector_failures) goto done;
    hr = IInspectable_GetIids(activation, &count, &iids);
    printf("FACTORY_GETIIDS hr=%08lx count=%lu\n", (ULONG)hr, count);
    VECTOR_CHECK(hr == S_OK && count == 2 && iids);
    if (vector_failures) goto done;
    VECTOR_CHECK((IsEqualGUID(&iids[0], &factory_iid) && IsEqualGUID(&iids[1], &activation_iid)) ||
                 (IsEqualGUID(&iids[1], &factory_iid) && IsEqualGUID(&iids[0], &activation_iid)));
    if (vector_failures) goto done;
    for (i = 0; i < count && iids; ++i)
        printf("FACTORY_IID %08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x\n",
               iids[i].Data1, iids[i].Data2, iids[i].Data3, iids[i].Data4[0], iids[i].Data4[1],
               iids[i].Data4[2], iids[i].Data4[3], iids[i].Data4[4], iids[i].Data4[5],
               iids[i].Data4[6], iids[i].Data4[7]);
    VECTOR_CHECK(IInspectable_QueryInterface(activation, &IID_IUnknown, (void **)&identity) == S_OK && identity);
    VECTOR_CHECK(IInspectable_QueryInterface(factory, &IID_IUnknown, (void **)&other) == S_OK && other == identity);
    if (other) IUnknown_Release(other);
    other = NULL;
    hr = IInspectable_QueryInterface(factory, &unknown_iid, (void **)&other);
    VECTOR_CHECK(hr == E_NOINTERFACE && !other);
    if (vector_failures) goto done;
    printf("FACTORY_REF_ADD=%lu\n", IInspectable_AddRef(factory));
    printf("FACTORY_REF_RELEASE=%lu\n", IInspectable_Release(factory));
    vector_controls(factory);
done:
    CoTaskMemFree(iids);
    if (other) IUnknown_Release(other);
    if (identity) IUnknown_Release(identity);
    if (factory) IInspectable_Release(factory);
    if (activation) IInspectable_Release(activation);
    WindowsDeleteString(name);
    if (initialized) RoUninitialize();
    VECTOR_CHECK(wine && GetModuleHandleW(L"wintypes.dll") == wine && module_pin(wine, wine_path,
        "90900b71e275676d61cff3b82c3b0c1968b9de453368b9836c9e71f6862d2438"));
    if (!vector_failures) null_result = propertyset_null_main();
    if (native) FreeLibrary(native);
    if (wine) FreeLibrary(wine);
    printf("Detail.Vector native oracle: %u checks, %u failures, nullable_result=%d\n",
           vector_checks, vector_failures, null_result);
    return vector_failures || null_result ? 1 : 0;
}
