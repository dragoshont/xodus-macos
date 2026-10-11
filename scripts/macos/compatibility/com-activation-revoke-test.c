#include <windows.h>
#include <roapi.h>
#include <stdio.h>

typedef HRESULT (WINAPI *REGISTER)(HSTRING *, PFNGETACTIVATIONFACTORY *, UINT32, RO_REGISTRATION_COOKIE *);
typedef void (WINAPI *REVOKE)(RO_REGISTRATION_COOKIE);
static unsigned checks, failures;

static void check(BOOL valid, const char *name)
{
    checks++;
    if (!valid) { failures++; printf("FAIL %s\n", name); }
}

int main(void)
{
    HMODULE module = LoadLibraryW(L"combase.dll");
    REGISTER register_factories = module ? (REGISTER)(ULONG_PTR)GetProcAddress(module, "RoRegisterActivationFactories") : NULL;
    REVOKE revoke = module ? (REVOKE)(ULONG_PTR)GetProcAddress(module, "RoRevokeActivationFactories") : NULL;
    RO_REGISTRATION_COOKIE cookie = (RO_REGISTRATION_COOKIE)(ULONG_PTR)0xdeadbeef;
    unsigned state;

    check(register_factories && revoke, "paired named exports present");
    if (!register_factories || !revoke) goto done;
    check(register_factories(NULL, NULL, 0, &cookie) == E_NOTIMPL && !cookie,
          "unsupported registration cannot invent success or cookie");
    check(register_factories(NULL, NULL, 0, NULL) == E_POINTER, "invalid registration output rejected");
    for (state = 0; state < 3; state++)
    {
        HRESULT hr = S_OK;
        if (state)
        {
            hr = CoInitializeEx(NULL, state == 1 ? COINIT_APARTMENTTHREADED : COINIT_MULTITHREADED);
            check(SUCCEEDED(hr), "real apartment initialized");
        }
        if (FAILED(hr)) continue;
        revoke(NULL);
        check(TRUE, "void NULL revoke returns without abort");
        revoke((RO_REGISTRATION_COOKIE)(ULONG_PTR)0xdeadbeef);
        check(TRUE, "void unknown-cookie revoke returns without dereference");
        if (state) CoUninitialize();
    }
done:
    printf("RESULT activation_revoke_checks=%u failures=%u\n", checks, failures);
    if (module) FreeLibrary(module);
    return failures ? 1 : 0;
}
