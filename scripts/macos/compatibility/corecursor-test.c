#define COBJMACROS
#include <stdio.h>
#include <wchar.h>
#include "initguid.h"
#include "windows.h"
#include "roapi.h"
#include "winstring.h"
#define WIDL_using_Windows_UI_Core
#include "windows.ui.core.h"

static unsigned checks, failures;
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; printf("FAIL line %u: %s\n", __LINE__, #expr); } } while (0)

typedef HRESULT (WINAPI *activation_fn)(HSTRING, IActivationFactory **);

static void test_cursor( ICoreCursorFactory *factory, CoreCursorType kind, UINT32 requested_id )
{
    ICoreCursor *cursor = NULL;
    IUnknown *agile = NULL, *identity = NULL;
    IInspectable *inspectable = NULL;
    CoreCursorType type = -1;
    UINT32 id = ~0u;
    HSTRING name = NULL;
    TrustLevel trust = FullTrust;
    IID *iids = NULL;
    ULONG count = 0;
    HRESULT hr;

    hr = ICoreCursorFactory_CreateCursor( factory, kind, requested_id, &cursor );
    CHECK(hr == S_OK && cursor != NULL);
    if (FAILED(hr) || !cursor) return;
    CHECK(ICoreCursor_get_Id(cursor, &id) == S_OK && id == requested_id);
    CHECK(ICoreCursor_get_Type(cursor, &type) == S_OK && type == kind);
    CHECK(ICoreCursor_get_Id(cursor, NULL) == E_POINTER);
    CHECK(ICoreCursor_get_Type(cursor, NULL) == E_POINTER);
    CHECK(ICoreCursor_QueryInterface(cursor, &IID_IInspectable, (void **)&inspectable) == S_OK);
    if (inspectable) CHECK(IInspectable_Release(inspectable) == 1);
    CHECK(ICoreCursor_QueryInterface(cursor, &IID_IAgileObject, (void **)&agile) == S_OK);
    if (agile)
    {
        CHECK(IUnknown_QueryInterface(agile, &IID_IUnknown, (void **)&identity) == S_OK);
        CHECK(identity == (IUnknown *)cursor);
        if (identity) CHECK(IUnknown_Release(identity) == 2);
        CHECK(IUnknown_Release(agile) == 1);
    }
    identity = (IUnknown *)0xdeadbeef;
    CHECK(ICoreCursor_QueryInterface(cursor, &IID_IClassFactory, (void **)&identity) == E_NOINTERFACE);
    CHECK(identity == NULL);
    CHECK(ICoreCursor_QueryInterface(cursor, &IID_IUnknown, NULL) == E_POINTER);
    CHECK(ICoreCursor_GetRuntimeClassName(cursor, &name) == S_OK);
    CHECK(name && !wcscmp(WindowsGetStringRawBuffer(name, NULL), L"Windows.UI.Core.CoreCursor"));
    WindowsDeleteString( name );
    CHECK(ICoreCursor_GetTrustLevel(cursor, &trust) == S_OK && trust == BaseTrust);
    CHECK(ICoreCursor_GetIids(cursor, &count, &iids) == S_OK && count == 2);
    if (iids) CHECK(IsEqualGUID(&iids[0], &IID_ICoreCursor) && IsEqualGUID(&iids[1], &IID_IAgileObject));
    CoTaskMemFree( iids );
    CHECK(ICoreCursor_AddRef(cursor) == 2);
    CHECK(ICoreCursor_Release(cursor) == 1);
    CHECK(ICoreCursor_Release(cursor) == 0);
}

int main(void)
{
    HMODULE module;
    WCHAR path[MAX_PATH];
    activation_fn get_factory;
    IActivationFactory *activation = NULL;
    ICoreCursorFactory *factory = NULL;
    IInspectable *instance = (IInspectable *)0xdeadbeef;
    IUnknown *identity = NULL;
    ICoreCursor *cursor;
    HSTRING name = NULL;
    HRESULT hr = RoInitialize( RO_INIT_MULTITHREADED );
    ULONG refs;
    int kind;

    CHECK(SUCCEEDED(hr));
    if (FAILED(hr)) goto done;
    module = LoadLibraryW( L"windows.ui.dll" );
    CHECK(module != NULL);
    if (!module) goto uninitialize;
    CHECK(GetModuleFileNameW(module, path, MAX_PATH) != 0);
    printf("CORECURSOR_PROVIDER %ls\n", path);
    get_factory = (activation_fn)(void *)GetProcAddress( module, "DllGetActivationFactory" );
    CHECK(get_factory != NULL);
    if (!get_factory) goto unload;
    CHECK(WindowsCreateString(L"Windows.UI.Core.CoreCursor", 26, &name) == S_OK);
    hr = get_factory( name, &activation );
    CHECK(hr == S_OK && activation != NULL);
    WindowsDeleteString( name );
    if (FAILED(hr) || !activation) goto unload;
    CHECK(IActivationFactory_QueryInterface(activation, &IID_ICoreCursorFactory, (void **)&factory) == S_OK);
    CHECK(IActivationFactory_ActivateInstance(activation, &instance) == E_NOTIMPL && instance == NULL);
    CHECK(IActivationFactory_ActivateInstance(activation, NULL) == E_INVALIDARG);
    if (factory)
    {
        CHECK(ICoreCursorFactory_QueryInterface(factory, &IID_IActivationFactory, (void **)&identity) == S_OK);
        CHECK(identity == (IUnknown *)activation);
        if (identity) IUnknown_Release( identity );
        CHECK(ICoreCursorFactory_QueryInterface(factory, &IID_IAgileObject, (void **)&identity) == S_OK);
        if (identity) IUnknown_Release( identity );
        refs = ICoreCursorFactory_AddRef( factory );
        CHECK(ICoreCursorFactory_Release(factory) == refs - 1);
        CHECK(ICoreCursorFactory_CreateCursor(factory, CoreCursorType_Arrow, 0, NULL) == E_INVALIDARG);
        cursor = (ICoreCursor *)0xdeadbeef;
        CHECK(ICoreCursorFactory_CreateCursor(factory, (CoreCursorType)-1, 0, &cursor) == E_INVALIDARG && cursor == NULL);
        cursor = (ICoreCursor *)0xdeadbeef;
        CHECK(ICoreCursorFactory_CreateCursor(factory, (CoreCursorType)16, 0, &cursor) == E_INVALIDARG && cursor == NULL);
        CHECK(CoreCursorType_Custom == 2 && CoreCursorType_SizeWestEast == 10);
        test_cursor( factory, CoreCursorType_Arrow, 0 );
        test_cursor( factory, CoreCursorType_Custom, 0x12345678 );
        for (kind = 1; kind <= 15; ++kind)
        {
            cursor = NULL;
            hr = ICoreCursorFactory_CreateCursor( factory, (CoreCursorType)kind, 0, &cursor );
            CHECK(hr == S_OK && cursor != NULL);
            if (cursor) ICoreCursor_Release( cursor );
        }
        ICoreCursorFactory_Release( factory );
    }
    IActivationFactory_Release( activation );
unload:
    FreeLibrary( module );
uninitialize:
    RoUninitialize();
done:
    printf("corecursor: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
