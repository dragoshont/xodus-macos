#define COBJMACROS
#include <stdio.h>
#include <wchar.h>
#include "initguid.h"
#include "windows.h"
#include "roapi.h"
#include "winstring.h"
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#include "windows.foundation.collections.h"

static unsigned checks, failures;
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; printf("FAIL line %u: %s\n", __LINE__, #expr); } } while (0)

int main(void)
{
    HMODULE module = LoadLibraryW(L"wintypes.dll");
    HRESULT (WINAPI *create)(IPropertySet **);
    IPropertySet *set = NULL, *second = NULL;
    IMap_HSTRING_IInspectable *map = NULL;
    IObservableMap_HSTRING_IInspectable *observable = NULL;
    IPropertyValueStatics *statics = NULL;
    IPropertyValue *number = NULL;
    IInspectable *value = NULL, *looked_up = NULL;
    IUnknown *agile = (void *)1;
    IUnknown *identity = NULL;
    IActivationFactory *regular_factory = NULL;
    IInspectable *regular = NULL;
    WCHAR path[MAX_PATH];
    HSTRING key = NULL, class_name = NULL, regular_name = NULL;
    BOOLEAN replaced;
    UINT32 size, result;
    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);

    CHECK(SUCCEEDED(hr));
    if (FAILED(hr)) goto done;
    CHECK(module != NULL);
    if (!module) goto cleanup;
    CHECK(GetModuleFileNameW(module, path, MAX_PATH) != 0);
    printf("PROPERTYSET_PROVIDER=%ls\n", path);
    create = (void *)GetProcAddress(module, "RoCreateNonAgilePropertySet");
    CHECK(create != NULL);
    if (!create) goto cleanup;
    CHECK(create(NULL) == E_POINTER);
    CHECK(create(&set) == S_OK && set);
    if (!set) goto cleanup;
    CHECK(IPropertySet_QueryInterface(set, &IID_IAgileObject, (void **)&agile) == E_NOINTERFACE && !agile);
    CHECK(IPropertySet_QueryInterface(set, &IID_IMap_HSTRING_IInspectable, (void **)&map) == S_OK);
    CHECK(IPropertySet_QueryInterface(set, &IID_IObservableMap_HSTRING_IInspectable, (void **)&observable) == S_OK);
    if (!map || !observable) goto cleanup;
    CHECK(IMap_HSTRING_IInspectable_QueryInterface(map, &IID_IAgileObject, (void **)&agile) == E_NOINTERFACE && !agile);
    CHECK(IObservableMap_HSTRING_IInspectable_QueryInterface(observable, &IID_IAgileObject, (void **)&agile) == E_NOINTERFACE && !agile);
    CHECK(IMap_HSTRING_IInspectable_QueryInterface(map, &IID_IUnknown, (void **)&identity) == S_OK
          && identity == (IUnknown *)set);
    CHECK(IMap_HSTRING_IInspectable_get_Size(map, &size) == S_OK && !size);
    CHECK(WindowsCreateString(L"Windows.Foundation.PropertyValue", 32, &class_name) == S_OK);
    CHECK(RoGetActivationFactory(class_name, &IID_IPropertyValueStatics, (void **)&statics) == S_OK);
    if (!statics) goto cleanup;
    CHECK(IPropertyValueStatics_CreateUInt32(statics, 0x12345678, &value) == S_OK);
    CHECK(WindowsCreateString(L"key", 3, &key) == S_OK);
    if (!value || !key) goto cleanup;
    CHECK(IMap_HSTRING_IInspectable_Insert(map, key, value, &replaced) == S_OK && !replaced);
    CHECK(IMap_HSTRING_IInspectable_get_Size(map, &size) == S_OK && size == 1);
    CHECK(IMap_HSTRING_IInspectable_Lookup(map, key, &looked_up) == S_OK && looked_up);
    if (!looked_up) goto cleanup;
    CHECK(IInspectable_QueryInterface(looked_up, &IID_IPropertyValue, (void **)&number) == S_OK);
    if (!number) goto cleanup;
    CHECK(IPropertyValue_GetUInt32(number, &result) == S_OK && result == 0x12345678);
    CHECK(create(&second) == S_OK && second && second != set);
    CHECK(IMap_HSTRING_IInspectable_Remove(map, key) == S_OK);
    CHECK(IMap_HSTRING_IInspectable_get_Size(map, &size) == S_OK && !size);
    CHECK(WindowsCreateString(L"Windows.Foundation.Collections.PropertySet",
                              wcslen(L"Windows.Foundation.Collections.PropertySet"), &regular_name) == S_OK);
    CHECK(RoGetActivationFactory(regular_name, &IID_IActivationFactory, (void **)&regular_factory) == S_OK);
    if (!regular_factory) goto cleanup;
    CHECK(IActivationFactory_ActivateInstance(regular_factory, &regular) == S_OK && regular);
    if (!regular) goto cleanup;
    CHECK(IInspectable_QueryInterface(regular, &IID_IAgileObject, (void **)&agile) == S_OK && agile);
cleanup:
    if (agile && agile != (void *)1) IUnknown_Release(agile);
    if (second) IPropertySet_Release(second);
    if (identity) IUnknown_Release(identity);
    if (regular) IInspectable_Release(regular);
    if (regular_factory) IActivationFactory_Release(regular_factory);
    if (number) IPropertyValue_Release(number);
    if (looked_up) IInspectable_Release(looked_up);
    if (value) IInspectable_Release(value);
    if (statics) IPropertyValueStatics_Release(statics);
    if (observable) IObservableMap_HSTRING_IInspectable_Release(observable);
    if (map) IMap_HSTRING_IInspectable_Release(map);
    if (set) IPropertySet_Release(set);
    WindowsDeleteString(key);
    WindowsDeleteString(class_name);
    WindowsDeleteString(regular_name);
    if (module) FreeLibrary(module);
    RoUninitialize();
done:
    printf("nonagile_propertyset_checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
