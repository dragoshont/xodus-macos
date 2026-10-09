#define COBJMACROS
#include <initguid.h>
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <activation.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "rpcbindingcreate-attributes.c"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line=%u %s\n", __LINE__, #c); ++failures; } } while (0)

static void component(void *object, const char *label)
{
    HMODULE module;
    WCHAR path[1024];
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (const WCHAR *)(*(void ***)object)[0], &module) && GetModuleFileNameW(module, path, 1024))
        printf("ACTUAL_COMPONENT %s=%ls\n", label, path);
}

int main(int argc, char **argv)
{
    WCHAR entry[1024];
    HSTRING entry_string, aliased_key, entry_key, missing;
    IMapView_HSTRING_IInspectable *view, *a, *b;
    IInspectable *value;
    IPropertyValue *property;
    IIterable_IKeyValuePair_HSTRING_IInspectable *iterable;
    IIterator_IKeyValuePair_HSTRING_IInspectable *iterator;
    IKeyValuePair_HSTRING_IInspectable *pair;
    UINT32 size, number, pass, seen;
    PropertyType type;
    boolean exists;
    IID *iids;
    ULONG iid_count;
    setvbuf(stdout, NULL, _IONBF, 0);
    SetErrorMode(SEM_NOGPFAULTERRORBOX);
    if ((argc != 2 && argc != 3) || FAILED(RoInitialize(RO_INIT_MULTITHREADED))) return 2;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argv[1], -1, entry, 1024)) return 2;
    WindowsCreateString(entry, wcslen(entry), &entry_string);
    WindowsCreateString(L"AppObject.Aliased", 17, &aliased_key);
    WindowsCreateString(L"AppObject.EntryPoint", 20, &entry_key);
    WindowsCreateString(L"Missing", 7, &missing);
    {
        HSTRING factory_name;
        IPropertyValueStatics *statics;
        boolean flag = FALSE;
        WindowsCreateString(L"Windows.Foundation.PropertyValue", 32, &factory_name);
        CHECK(RoGetActivationFactory(factory_name, &IID_IPropertyValueStatics, (void **)&statics) == S_OK);
        CHECK(IPropertyValueStatics_CreateBoolean(statics, TRUE, &value) == S_OK);
        component(statics, "PropertyValueStatics");
        component(value, "BooleanValue");
        CHECK(IInspectable_QueryInterface(value, &IID_IPropertyValue, (void **)&property) == S_OK);
        CHECK(IPropertyValue_get_Type(property, &type) == S_OK && type == PropertyType_Boolean);
        CHECK(IPropertyValue_GetBoolean(property, &flag) == S_OK && flag);
        printf("BOOLEAN_BOXING_TYPE=%u VALUE=%u\n", type, flag);
        IPropertyValue_Release(property); IInspectable_Release(value);
        IPropertyValueStatics_Release(statics); WindowsDeleteString(factory_name);
    }
    if (argc == 3)
    {
        typedef HRESULT (WINAPI *GET_REGISTRATION)(HSTRING, void **);
        typedef HRESULT (WINAPI *GET_ATTRIBUTES)(void *, IMapView_HSTRING_IInspectable **);
        GET_REGISTRATION query = (GET_REGISTRATION)(ULONG_PTR)GetProcAddress(GetModuleHandleW(L"combase.dll"),
                                                                           "RoGetActivatableClassRegistration");
        HSTRING name;
        void *object = NULL;
        WindowsCreateString(L"Windows.Foundation.Collections.PropertySet", 42, &name);
        CHECK(query(name, &object) == S_OK && object);
        CHECK(((GET_ATTRIBUTES)(*(void ***)object)[10])(object, &view) == S_OK);
        CHECK(IMapView_HSTRING_IInspectable_get_Size(view, &size) == S_OK);
        printf("NATIVE_GLOBAL_METADATA_MAP_SIZE=%u\n", size);
        CHECK(IMapView_HSTRING_IInspectable_QueryInterface(view, &IID_IMapView_HSTRING_IInspectable, (void **)&a) == S_OK);
        IMapView_HSTRING_IInspectable_Release(a);
        CHECK(IMapView_HSTRING_IInspectable_QueryInterface(view,
              &IID_IIterable_IKeyValuePair_HSTRING_IInspectable, (void **)&iterable) == S_OK);
        IIterable_IKeyValuePair_HSTRING_IInspectable_Release(iterable);
        CHECK(IMapView_HSTRING_IInspectable_GetIids(view, &iid_count, &iids) == S_OK);
        for (pass = 0; pass < iid_count; ++pass)
        {
            WCHAR text[64];
            StringFromGUID2(&iids[pass], text, 64);
            printf("NATIVE_GLOBAL_MAP_IID=%ls\n", text);
        }
        CoTaskMemFree(iids);
        IMapView_HSTRING_IInspectable_Release(view);
        IUnknown_Release((IUnknown *)object); WindowsDeleteString(name);
    }
    for (pass = 0; pass < 500; ++pass)
    {
        {
            HRESULT hr = attributes_create(1, entry_string, &view);
            if (FAILED(hr)) { printf("CREATE_ERROR=%08lx\n", (unsigned long)hr); return 1; }
            if (!pass) component(attributes_from_view(view)->inner, "PropertySet_GetView_backing");
        }
        CHECK(IMapView_HSTRING_IInspectable_get_Size(view, &size) == S_OK && size == 2);
        {
            IMap_HSTRING_IInspectable *mutable = (void *)1;
            CHECK(IMapView_HSTRING_IInspectable_QueryInterface(view, &IID_IMap_HSTRING_IInspectable,
                (void **)&mutable) == E_NOINTERFACE && !mutable);
        }
        CHECK(IMapView_HSTRING_IInspectable_GetIids(view, &iid_count, &iids) == S_OK && iid_count == 2);
        CHECK(IsEqualIID(&iids[0], &IID_IMapView_HSTRING_IInspectable));
        CHECK(IsEqualIID(&iids[1], &IID_IIterable_IKeyValuePair_HSTRING_IInspectable));
        CoTaskMemFree(iids);
        CHECK(IMapView_HSTRING_IInspectable_HasKey(view, missing, &exists) == S_OK && !exists);
        value = (void *)(ULONG_PTR)1;
        CHECK(IMapView_HSTRING_IInspectable_Lookup(view, missing, &value) == E_BOUNDS && !value);
        CHECK(IMapView_HSTRING_IInspectable_Lookup(view, aliased_key, &value) == S_OK);
        CHECK(IInspectable_QueryInterface(value, &IID_IPropertyValue, (void **)&property) == S_OK);
        CHECK(IPropertyValue_get_Type(property, &type) == S_OK && type == PropertyType_UInt32);
        CHECK(IPropertyValue_GetUInt32(property, &number) == S_OK && number == 1);
        IPropertyValue_Release(property); IInspectable_Release(value);
        CHECK(IMapView_HSTRING_IInspectable_Lookup(view, entry_key, &value) == S_OK);
        CHECK(IInspectable_QueryInterface(value, &IID_IPropertyValue, (void **)&property) == S_OK);
        CHECK(IPropertyValue_get_Type(property, &type) == S_OK && type == PropertyType_String);
        {
            HSTRING text = NULL;
            CHECK(IPropertyValue_GetString(property, &text) == S_OK &&
                  !wcscmp(WindowsGetStringRawBuffer(text, NULL), entry));
            WindowsDeleteString(text);
        }
        CHECK(IMapView_HSTRING_IInspectable_Split(view, &a, &b) == S_OK && ((!a && !b) || (a && b)));
        if (a && b) {
        CHECK(IMapView_HSTRING_IInspectable_get_Size(a, &size) == S_OK && size == 1);
        CHECK(IMapView_HSTRING_IInspectable_get_Size(b, &size) == S_OK && size == 1);
        {
            IMapView_HSTRING_IInspectable *x = (void *)1, *y = (void *)1;
            CHECK(IMapView_HSTRING_IInspectable_Split(a, &x, &y) == S_OK && !x && !y);
        }
        IMapView_HSTRING_IInspectable_Release(a); IMapView_HSTRING_IInspectable_Release(b);
        }
        CHECK(IMapView_HSTRING_IInspectable_QueryInterface(view,
            &IID_IIterable_IKeyValuePair_HSTRING_IInspectable, (void **)&iterable) == S_OK);
        {
            IUnknown *from_view = NULL, *from_iterable = NULL;
            CHECK(IMapView_HSTRING_IInspectable_QueryInterface(view, &IID_IUnknown, (void **)&from_view) == S_OK);
            CHECK(IIterable_IKeyValuePair_HSTRING_IInspectable_QueryInterface(iterable, &IID_IUnknown,
                (void **)&from_iterable) == S_OK && from_view == from_iterable);
            IUnknown_Release(from_view); IUnknown_Release(from_iterable);
        }
        CHECK(IIterable_IKeyValuePair_HSTRING_IInspectable_First(iterable, &iterator) == S_OK);
        IIterable_IKeyValuePair_HSTRING_IInspectable_Release(iterable);
        IMapView_HSTRING_IInspectable_Release(view);
        seen = 0;
        while (SUCCEEDED(IIterator_IKeyValuePair_HSTRING_IInspectable_get_HasCurrent(iterator, &exists)) && exists)
        {
            HSTRING key;
            CHECK(IIterator_IKeyValuePair_HSTRING_IInspectable_get_Current(iterator, &pair) == S_OK);
            CHECK(IKeyValuePair_HSTRING_IInspectable_get_Key(pair, &key) == S_OK);
            WindowsDeleteString(key); IKeyValuePair_HSTRING_IInspectable_Release(pair);
            ++seen;
            IIterator_IKeyValuePair_HSTRING_IInspectable_MoveNext(iterator, &exists);
        }
        CHECK(seen == 2);
        IIterator_IKeyValuePair_HSTRING_IInspectable_Release(iterator);
        CHECK(IPropertyValue_get_Type(property, &type) == S_OK && type == PropertyType_String);
        IPropertyValue_Release(property); IInspectable_Release(value);
    }
    WindowsDeleteString(entry_string); WindowsDeleteString(aliased_key);
    WindowsDeleteString(entry_key); WindowsDeleteString(missing);
    RoUninitialize();
    printf("ATTRIBUTES_MAP_%s cycles=500 failures=%d entry=%ls\n", failures ? "FAIL" : "PASS", failures, entry);
    return failures ? 1 : 0;
}
