#define COBJMACROS
#include <stdio.h>
#include <wchar.h>
#include "initguid.h"
#include "windows.h"
#include "roapi.h"
#include "winstring.h"

HRESULT WINAPI RoRegisterActivationFactories(HSTRING *, PFNGETACTIVATIONFACTORY *, UINT32, RO_REGISTRATION_COOKIE *);
void WINAPI RoRevokeActivationFactories(RO_REGISTRATION_COOKIE);

static unsigned checks, failures, calls;
static LONG factory_refs = 1;
static RO_REGISTRATION_COOKIE recursive_cookie;
static HANDLE entered, proceed;
static BOOL self_revoke, block_callback;
static HRESULT callback_result = S_OK;
static BOOL empty_factory;
static const WCHAR class_name[] = L"Xodus.Tests.RegisteredActivation";
static const WCHAR other_name[] = L"Xodus.Tests.OtherActivation";
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; printf("FAIL line %u: %s\n", __LINE__, #expr); } } while (0)

static HRESULT WINAPI factory_query( IActivationFactory *iface, REFIID iid, void **out )
{
    *out = NULL;
    if (!IsEqualGUID(iid, &IID_IUnknown) && !IsEqualGUID(iid, &IID_IInspectable) &&
        !IsEqualGUID(iid, &IID_IActivationFactory)) return E_NOINTERFACE;
    *out = iface;
    IActivationFactory_AddRef( iface );
    return S_OK;
}
static ULONG WINAPI factory_addref( IActivationFactory *iface )
{
    (void)iface;
    return InterlockedIncrement( &factory_refs );
}
static ULONG WINAPI factory_release( IActivationFactory *iface )
{
    (void)iface;
    return InterlockedDecrement( &factory_refs );
}
static HRESULT WINAPI factory_iids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    (void)iface; (void)count; (void)iids;
    return E_NOTIMPL;
}
static HRESULT WINAPI factory_name( IActivationFactory *iface, HSTRING *name )
{
    (void)iface; (void)name;
    return E_NOTIMPL;
}
static HRESULT WINAPI factory_trust( IActivationFactory *iface, TrustLevel *trust )
{
    (void)iface; (void)trust;
    return E_NOTIMPL;
}
static HRESULT WINAPI factory_activate( IActivationFactory *iface, IInspectable **out )
{
    (void)iface;
    *out = NULL;
    return E_NOTIMPL;
}
static const IActivationFactoryVtbl factory_vtbl =
{
    factory_query, factory_addref, factory_release, factory_iids,
    factory_name, factory_trust, factory_activate
};
static IActivationFactory factory = { &factory_vtbl };

static HRESULT WINAPI callback( HSTRING name, IActivationFactory **out )
{
    ++calls;
    if (wcscmp(WindowsGetStringRawBuffer(name, NULL), class_name) &&
        wcscmp(WindowsGetStringRawBuffer(name, NULL), other_name) &&
        wcscmp(WindowsGetStringRawBuffer(name, NULL), L"Windows.UI.Core.CoreCursor")) return E_INVALIDARG;
    *out = NULL;
    if (self_revoke) RoRevokeActivationFactories( recursive_cookie );
    if (block_callback)
    {
        SetEvent( entered );
        if (WaitForSingleObject(proceed, 5000) != WAIT_OBJECT_0) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    if (FAILED(callback_result) || empty_factory) return callback_result;
    IActivationFactory_AddRef( &factory );
    *out = &factory;
    return S_OK;
}

static DWORD WINAPI concurrent_lookup( void *context )
{
    HSTRING name = NULL;
    IActivationFactory *out = NULL;
    HRESULT hr = RoInitialize( RO_INIT_MULTITHREADED );
    (void)context;
    if (FAILED(hr)) return hr;
    WindowsCreateString( class_name, wcslen(class_name), &name );
    hr = RoGetActivationFactory( name, &IID_IActivationFactory, (void **)&out );
    if (out) IActivationFactory_Release( out );
    WindowsDeleteString( name );
    RoUninitialize();
    return hr;
}

int main(void)
{
    HSTRING names[2] = { NULL, NULL }, empty = NULL, embedded = NULL, builtin = NULL;
    PFNGETACTIVATIONFACTORY callbacks[2] = { callback, callback };
    RO_REGISTRATION_COOKIE cookie = NULL, second = NULL;
    IActivationFactory *out = NULL;
    HSTRING_HEADER header;
    WCHAR borrowed[64], path[MAX_PATH];
    HRESULT hr;
    HMODULE module = LoadLibraryW( L"combase.dll" );
    HRESULT (WINAPI *shutdown_hint)(void *, DWORD);
    HANDLE thread;
    DWORD exit_code = ~0u;

    CHECK(module != NULL);
    if (!module) goto done;
    CHECK(GetModuleFileNameW(module, path, MAX_PATH) != 0);
    printf("ACTIVATION_PROVIDER=%ls\n", path);
    shutdown_hint = (void *)GetProcAddress( module, (LPCSTR)69 );
    CHECK(shutdown_hint != NULL);
    if (shutdown_hint)
    {
        CHECK(shutdown_hint((void *)1, 2000) == S_OK);
        CHECK(shutdown_hint(NULL, 0) == S_OK);
    }
    CHECK(WindowsCreateString(class_name, wcslen(class_name), &names[0]) == S_OK);
    CHECK(WindowsCreateString(other_name, wcslen(other_name), &names[1]) == S_OK);
    CHECK(RoRegisterActivationFactories(names, callbacks, 1, &cookie) == CO_E_NOTINITIALIZED && !cookie);
    hr = RoInitialize( RO_INIT_MULTITHREADED );
    CHECK(SUCCEEDED(hr));
    if (FAILED(hr)) goto cleanup;
    CHECK(RoRegisterActivationFactories(names, callbacks, 1, NULL) == E_POINTER);
    CHECK(RoRegisterActivationFactories(NULL, callbacks, 1, &cookie) == E_POINTER && !cookie);
    CHECK(RoRegisterActivationFactories(names, NULL, 1, &cookie) == E_POINTER && !cookie);
    CHECK(RoRegisterActivationFactories(names, callbacks, 0, &cookie) == E_INVALIDARG && !cookie);
    CHECK(RoRegisterActivationFactories(&empty, callbacks, 1, &cookie) == E_INVALIDARG && !cookie);
    CHECK(WindowsCreateString(L"a\0b", 3, &embedded) == S_OK);
    CHECK(RoRegisterActivationFactories(&embedded, callbacks, 1, &cookie) == E_INVALIDARG && !cookie);
    WindowsDeleteString( embedded );
    callbacks[0] = NULL;
    CHECK(RoRegisterActivationFactories(names, callbacks, 1, &cookie) == E_INVALIDARG && !cookie);
    callbacks[0] = callback;
    WindowsDeleteString( names[1] );
    names[1] = names[0];
    CHECK(RoRegisterActivationFactories(names, callbacks, 2, &cookie) == CO_E_ALREADYINITIALIZED && !cookie);
    CHECK(RoGetActivationFactory(names[0], &IID_IActivationFactory, (void **)&out) == REGDB_E_CLASSNOTREG && !out);
    WindowsCreateString( other_name, wcslen(other_name), &names[1] );
    CHECK(RoRegisterActivationFactories(names, callbacks, 2, &cookie) == S_OK && cookie);
    CHECK(RoRegisterActivationFactories(names, callbacks, 1, &second) == CO_E_ALREADYINITIALIZED && !second);
    CHECK(RoGetActivationFactory(names[0], &IID_IActivationFactory, (void **)&out) == S_OK && out == &factory);
    if (out) IActivationFactory_Release( out );
    CHECK(RoGetActivationFactory(names[1], &IID_IClassFactory, (void **)&out) == E_NOINTERFACE && !out);
    callback_result = E_ACCESSDENIED;
    CHECK(RoGetActivationFactory(names[0], &IID_IActivationFactory, (void **)&out) == E_ACCESSDENIED && !out);
    callback_result = S_OK;
    empty_factory = TRUE;
    CHECK(RoGetActivationFactory(names[0], &IID_IActivationFactory, (void **)&out) == E_UNEXPECTED && !out);
    empty_factory = FALSE;
    RoRevokeActivationFactories( cookie );
    RoRevokeActivationFactories( cookie );
    CHECK(RoGetActivationFactory(names[0], &IID_IActivationFactory, (void **)&out) == REGDB_E_CLASSNOTREG && !out);
    CHECK(RoGetActivationFactory(names[1], &IID_IActivationFactory, (void **)&out) == REGDB_E_CLASSNOTREG && !out);
    wcscpy( borrowed, class_name );
    CHECK(WindowsCreateStringReference(borrowed, wcslen(borrowed), &header, &empty) == S_OK);
    CHECK(RoRegisterActivationFactories(&empty, callbacks, 1, &second) == S_OK && second != cookie);
    borrowed[0] = L'!';
    CHECK(RoGetActivationFactory(names[0], &IID_IActivationFactory, (void **)&out) == S_OK && out == &factory);
    if (out) IActivationFactory_Release( out );
    RoRevokeActivationFactories( second );
    CHECK(WindowsCreateString(L"Windows.UI.Core.CoreCursor", 26, &builtin) == S_OK);
    CHECK(RoRegisterActivationFactories(&builtin, callbacks, 1, &cookie) == S_OK);
    CHECK(RoGetActivationFactory(builtin, &IID_IActivationFactory, (void **)&out) == S_OK && out == &factory);
    if (out) IActivationFactory_Release( out );
    RoRevokeActivationFactories( cookie );
    WindowsDeleteString( builtin );
    CHECK(RoRegisterActivationFactories(names, callbacks, 1, &recursive_cookie) == S_OK);
    self_revoke = TRUE;
    CHECK(RoGetActivationFactory(names[0], &IID_IActivationFactory, (void **)&out) == S_OK && out == &factory);
    self_revoke = FALSE;
    if (out) IActivationFactory_Release( out );
    CHECK(RoGetActivationFactory(names[0], &IID_IActivationFactory, (void **)&out) == REGDB_E_CLASSNOTREG && !out);
    entered = CreateEventW( NULL, FALSE, FALSE, NULL );
    proceed = CreateEventW( NULL, FALSE, FALSE, NULL );
    CHECK(entered && proceed);
    if (entered && proceed)
    {
        CHECK(RoRegisterActivationFactories(names, callbacks, 1, &cookie) == S_OK);
        block_callback = TRUE;
        thread = CreateThread( NULL, 0, concurrent_lookup, NULL, 0, NULL );
        CHECK(thread != NULL);
        if (thread)
        {
            CHECK(WaitForSingleObject(entered, 5000) == WAIT_OBJECT_0);
            RoRevokeActivationFactories( cookie );
            CHECK(RoGetActivationFactory(names[0], &IID_IActivationFactory, (void **)&out) == REGDB_E_CLASSNOTREG && !out);
            SetEvent( proceed );
            CHECK(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
            CHECK(GetExitCodeThread(thread, &exit_code) && exit_code == S_OK);
            CloseHandle( thread );
        }
        else RoRevokeActivationFactories( cookie );
        block_callback = FALSE;
    }
    if (entered) CloseHandle( entered );
    if (proceed) CloseHandle( proceed );
    CHECK(factory_refs == 1 && calls >= 8);
    RoRevokeActivationFactories( NULL );
    RoRevokeActivationFactories( (RO_REGISTRATION_COOKIE)0xdeadbeef );
    RoUninitialize();
cleanup:
    WindowsDeleteString( names[0] );
    WindowsDeleteString( names[1] );
    FreeLibrary( module );
done:
    printf("RESULT activation_registration_checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
