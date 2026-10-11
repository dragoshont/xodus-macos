#define COBJMACROS
#include <stdio.h>
#include <wchar.h>
#include "windows.h"
#include "roapi.h"
#include "winstring.h"
#include "initguid.h"
#define WIDL_using_Windows_Foundation
#include "windows.foundation.h"
#define WIDL_using_Windows_UI_ViewManagement
#include "windows.ui.viewmanagement.h"

static unsigned checks, failures;
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; printf("FAIL line %u: %s\n", __LINE__, #expr); } } while (0)

struct handler
{
    ITypedEventHandler_AccessibilitySettings_IInspectable iface;
    LONG refs;
    LONG calls;
};

static HRESULT WINAPI handler_QueryInterface(ITypedEventHandler_AccessibilitySettings_IInspectable *iface,
                                             REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) &&
        !IsEqualIID(iid, &IID_ITypedEventHandler_AccessibilitySettings_IInspectable))
        return E_NOINTERFACE;
    *out = iface;
    ITypedEventHandler_AccessibilitySettings_IInspectable_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI handler_AddRef(ITypedEventHandler_AccessibilitySettings_IInspectable *iface)
{
    struct handler *handler = CONTAINING_RECORD(iface, struct handler, iface);
    return InterlockedIncrement(&handler->refs);
}

static ULONG WINAPI handler_Release(ITypedEventHandler_AccessibilitySettings_IInspectable *iface)
{
    struct handler *handler = CONTAINING_RECORD(iface, struct handler, iface);
    return InterlockedDecrement(&handler->refs);
}

static HRESULT WINAPI handler_Invoke(ITypedEventHandler_AccessibilitySettings_IInspectable *iface,
                                     IAccessibilitySettings *sender, IInspectable *args)
{
    struct handler *handler = CONTAINING_RECORD(iface, struct handler, iface);
    (void)sender;
    (void)args;
    InterlockedIncrement(&handler->calls);
    return S_OK;
}

static const ITypedEventHandler_AccessibilitySettings_IInspectableVtbl handler_vtbl =
{
    handler_QueryInterface, handler_AddRef, handler_Release, handler_Invoke
};

typedef HRESULT (WINAPI *activation_fn)(HSTRING, IActivationFactory **);

int main(void)
{
    struct handler handler = {{&handler_vtbl}, 1, 0};
    EventRegistrationToken first = {0}, second = {0}, third = {0}, unknown = {0};
    IActivationFactory *factory = NULL;
    IInspectable *instance = NULL;
    IAccessibilitySettings *settings = NULL;
    HIGHCONTRASTW contrast = {.cbSize = sizeof(contrast)};
    HSTRING name = NULL;
    HMODULE module = NULL;
    activation_fn get_factory;
    boolean before = 0, after = 0;
    BOOL spi;
    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);

    CHECK(SUCCEEDED(hr));
    if (FAILED(hr)) goto done;
    module = LoadLibraryW(L"windows.ui.dll");
    CHECK(module != NULL);
    if (!module) goto cleanup;
    get_factory = (activation_fn)(void *)GetProcAddress(module, "DllGetActivationFactory");
    CHECK(get_factory != NULL);
    if (!get_factory) goto cleanup;
    hr = WindowsCreateString(L"Windows.UI.ViewManagement.AccessibilitySettings",
                            wcslen(L"Windows.UI.ViewManagement.AccessibilitySettings"), &name);
    CHECK(hr == S_OK);
    if (FAILED(hr)) goto cleanup;
    hr = get_factory(name, &factory);
    CHECK(hr == S_OK && factory != NULL);
    if (FAILED(hr) || !factory) goto cleanup;
    hr = IActivationFactory_ActivateInstance(factory, &instance);
    CHECK(hr == S_OK && instance != NULL);
    if (FAILED(hr) || !instance) goto cleanup;
    hr = IInspectable_QueryInterface(instance, &IID_IAccessibilitySettings, (void **)&settings);
    CHECK(hr == S_OK && settings != NULL);
    if (FAILED(hr) || !settings) goto cleanup;
    IInspectable_Release(instance);
    instance = NULL;

    spi = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
    hr = IAccessibilitySettings_get_HighContrast(settings, &before);
    CHECK(hr == (spi ? S_OK : E_FAIL));
    if (spi) CHECK(before == !!(contrast.dwFlags & HCF_HIGHCONTRASTON));
    CHECK(IAccessibilitySettings_add_HighContrastChanged(settings, NULL, &first) == E_INVALIDARG);
    CHECK(IAccessibilitySettings_add_HighContrastChanged(settings, &handler.iface, NULL) == E_INVALIDARG);
    CHECK(IAccessibilitySettings_add_HighContrastChanged(settings, NULL, NULL) == E_INVALIDARG);
    CHECK(handler.refs == 1);
    CHECK(IAccessibilitySettings_remove_HighContrastChanged(settings, unknown) == S_OK);
    CHECK(IAccessibilitySettings_add_HighContrastChanged(settings, &handler.iface, &first) == S_OK);
    CHECK(first.value > 0 && handler.refs == 2);
    CHECK(IAccessibilitySettings_add_HighContrastChanged(settings, &handler.iface, &second) == S_OK);
    CHECK(second.value > first.value && handler.refs == 3);
    CHECK(IAccessibilitySettings_remove_HighContrastChanged(settings, first) == S_OK);
    CHECK(handler.refs == 2);
    CHECK(IAccessibilitySettings_remove_HighContrastChanged(settings, first) == S_OK);
    unknown.value = second.value + 100;
    CHECK(IAccessibilitySettings_remove_HighContrastChanged(settings, unknown) == S_OK);
    CHECK(handler.refs == 2);
    CHECK(IAccessibilitySettings_remove_HighContrastChanged(settings, second) == S_OK);
    CHECK(handler.refs == 1);
    CHECK(IAccessibilitySettings_add_HighContrastChanged(settings, &handler.iface, &third) == S_OK);
    CHECK(third.value > second.value && handler.refs == 2);
    CHECK(IAccessibilitySettings_add_HighContrastChanged(settings, &handler.iface, &second) == S_OK);
    CHECK(second.value > third.value && handler.refs == 3);
    CHECK(IAccessibilitySettings_get_HighContrast(settings, &after) == hr);
    if (spi) CHECK(before == after);
    CHECK(IAccessibilitySettings_Release(settings) == 0);
    settings = NULL;
    CHECK(handler.refs == 1 && handler.calls == 0);

cleanup:
    if (settings) IAccessibilitySettings_Release(settings);
    if (instance) IInspectable_Release(instance);
    if (factory) IActivationFactory_Release(factory);
    WindowsDeleteString(name);
    if (module) FreeLibrary(module);
    RoUninitialize();
done:
    printf("accessibilitysettings-event: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
