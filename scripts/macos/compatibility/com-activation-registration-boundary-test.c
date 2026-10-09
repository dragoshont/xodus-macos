#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <stdio.h>

typedef HRESULT (WINAPI *REGISTER)(HSTRING *, PFNGETACTIVATIONFACTORY *, UINT32, RO_REGISTRATION_COOKIE *);
typedef void (WINAPI *REVOKE)(RO_REGISTRATION_COOKIE);

static HRESULT WINAPI factory(HSTRING name, IActivationFactory **out)
{
    (void)name;
    *out = NULL;
    return E_FAIL;
}

int main(void)
{
    HMODULE module = LoadLibraryW(L"combase.dll");
    REGISTER registration = module ?
        (REGISTER)(ULONG_PTR)GetProcAddress(module, "RoRegisterActivationFactories") : NULL;
    REVOKE revoke = module ?
        (REVOKE)(ULONG_PTR)GetProcAddress(module, "RoRevokeActivationFactories") : NULL;
    HSTRING name = NULL;
    PFNGETACTIVATIONFACTORY callback = factory;
    RO_REGISTRATION_COOKIE cookie = (void *)(ULONG_PTR)0x1234;
    HRESULT hr;
    if (!registration || !revoke || FAILED(RoInitialize(RO_INIT_MULTITHREADED))) return 2;
    if (FAILED(WindowsCreateString(L"Xodus.Unregistered.Factory", 26, &name))) return 3;
    if (registration(&name, &callback, 1, NULL) != E_POINTER) return 4;
    if (registration(NULL, NULL, 0, &cookie) != E_INVALIDARG) return 5;
    hr = registration(&name, &callback, 1, &cookie);
    revoke(NULL);
    revoke((void *)(ULONG_PTR)0x1234);
    printf("REAL_FACTORY_REGISTRATION_BOUNDARY hr=%08lx output_untouched=%d cleanup_no_fault=1\n",
           (unsigned long)hr, cookie == (void *)(ULONG_PTR)0x1234);
    WindowsDeleteString(name);
    RoUninitialize();
    FreeLibrary(module);
    return hr == E_NOTIMPL && cookie == (void *)(ULONG_PTR)0x1234 ? 0 : 6;
}
