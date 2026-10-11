#define COBJMACROS
#include <windows.h>
#include <inspectable.h>
#include <winstring.h>
#include <stdio.h>
#include <stddef.h>

#ifndef E_ILLEGAL_METHOD_CALL
#define E_ILLEGAL_METHOD_CALL ((HRESULT)0x8000000e)
#endif

/* Failure-counted public SDK ICoreWindowStatic/WithContext and CoreWindow.idl ABI checks. */
typedef struct with_context with_context;
struct context_vtbl
{
    IInspectableVtbl base;
    HRESULT (WINAPI *get_UIContext)(with_context *, IInspectable **);
};
struct with_context { const struct context_vtbl *lpVtbl; };
typedef struct adapter adapter;
struct adapter_vtbl
{
    IInspectableVtbl base;
    HRESULT (WINAPI *getters[7])(adapter *, IUnknown **);
    HRESULT (WINAPI *SetWindowClientAdapter)(adapter *, IUnknown *);
};
struct adapter { const struct adapter_vtbl *lpVtbl; };
struct statics_vtbl
{
    IInspectableVtbl base;
    HRESULT (WINAPI *GetForCurrentThread)(IInspectable *, IInspectable **);
};
struct interop_vtbl
{
    IUnknownVtbl base;
    HRESULT (WINAPI *get_WindowHandle)(IUnknown *, HWND *);
    HRESULT (WINAPI *put_MessageHandled)(IUnknown *, BYTE);
};
static const GUID iid_static = {0x4d239005,0x3c2a,0x41b1,{0x90,0x22,0x53,0x6b,0xb9,0xcf,0x93,0xb1}};
static const GUID iid_context = {0x9ac40241,0x3575,0x4c3b,{0xaf,0x66,0xe8,0xc5,0x29,0xd4,0xd0,0x6c}};
static const GUID iid_window = {0x79b9d5f2,0x879e,0x4b89,{0xb7,0x98,0x79,0xe4,0x75,0x98,0x03,0x0c}};
static const GUID iid_ui = {0xbb5cfacd,0x5bd8,0x59d0,{0xa5,0x9e,0x1c,0x17,0xa4,0xd6,0xd2,0x43}};
static const GUID iid_adapter = {0x7a5b6fd1,0xcd73,0x4b6c,{0x9c,0xf4,0x2e,0x86,0x9e,0xaf,0x47,0x0a}};
static const GUID iid_interop = {0x45d64a29,0xa63e,0x4cb6,{0xb4,0x98,0x57,0x81,0xd2,0x98,0xcb,0x4f}};
static const GUID iid_agile = {0x94ea2b94,0xe9cc,0x49e0,{0xc0,0xff,0xee,0x64,0xca,0x8f,0x5b,0x90}};
static const GUID iid_inspectable = {0xaf86e2e0,0xb12d,0x4c6a,{0x9c,0x5a,0xd7,0xaa,0x65,0x10,0x1e,0x90}};
static unsigned failures, checks;
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; printf("FAIL line %u: %s\n", __LINE__, #expr); } } while (0)
static LONG fake_refs = 1;
static HRESULT WINAPI fake_qi(IUnknown *iface, REFIID iid, void **out) { *out = NULL; return E_NOINTERFACE; }
static ULONG WINAPI fake_addref(IUnknown *iface) { return InterlockedIncrement(&fake_refs); }
static ULONG WINAPI fake_release(IUnknown *iface) { return InterlockedDecrement(&fake_refs); }
static IUnknownVtbl fake_vtbl = {fake_qi, fake_addref, fake_release};

int main(void)
{
    HRESULT (WINAPI *create_string)(LPCWSTR, UINT32, HSTRING *);
    HRESULT (WINAPI *delete_string)(HSTRING);
    PCWSTR (WINAPI *raw_string)(HSTRING, UINT32 *);
    HRESULT (WINAPI *get_factory)(HSTRING, IInspectable **);
    HRESULT (WINAPI *create_window)(DWORD, PCWSTR, INT, INT, UINT, UINT, DWORD, HWND, REFIID, void **);
    HMODULE combase = LoadLibraryW(L"combase.dll"), ui = LoadLibraryW(L"windows.ui.dll");
    IInspectable *factory = NULL, *statics = NULL, *window = NULL, *first = NULL, *second = NULL;
    IUnknown *interop = NULL, *identity = NULL, fake = {&fake_vtbl};
    with_context *context = NULL;
    adapter *adapters = NULL;
    HSTRING name = NULL, class_name = NULL;
    IID *iids = NULL;
    ULONG count = 0;
    UINT32 length;
    HWND hwnd = NULL;
    unsigned i;
    HRESULT hr;
    const GUID *context_iids[] = {&IID_IUnknown, &iid_inspectable, &iid_agile, &iid_ui};

    CHECK(offsetof(struct context_vtbl, get_UIContext) == 6 * sizeof(void *));
    CHECK(offsetof(struct adapter_vtbl, SetWindowClientAdapter) == 13 * sizeof(void *));
    CHECK(combase && ui);
    if (!combase || !ui) return 1;
    create_string = (void *)GetProcAddress(combase, "WindowsCreateString");
    delete_string = (void *)GetProcAddress(combase, "WindowsDeleteString");
    raw_string = (void *)GetProcAddress(combase, "WindowsGetStringRawBuffer");
    get_factory = (void *)GetProcAddress(ui, "DllGetActivationFactory");
    CHECK(create_string && delete_string && raw_string && get_factory);
    if (!create_string || !delete_string || !raw_string || !get_factory) return 1;
    CHECK(create_string(L"Windows.UI.Core.CoreWindow", 26, &name) == S_OK);
    hr = get_factory(name, &factory);
    printf("COREWINDOW_FACTORY_HR=%08lx CLASS_LENGTH=%u\n", (unsigned long)hr, (unsigned)wcslen(raw_string(name, NULL)));
    CHECK(hr == S_OK && factory);
    if (!factory) return 1;
    CHECK(IInspectable_QueryInterface(factory, &iid_static, (void **)&statics) == S_OK);
    if (!statics) return 1;
    CHECK(SetEnvironmentVariableW(L"WINE_COREWINDOW_AUTOCREATE", NULL));
    CHECK(SetEnvironmentVariableW(L"WINE_COREWINDOW_NO_AUTOCREATE", NULL));
    CHECK(((const struct statics_vtbl *)statics->lpVtbl)->GetForCurrentThread(statics, NULL) == E_POINTER);
    CHECK(((const struct statics_vtbl *)statics->lpVtbl)->GetForCurrentThread(statics, &window) == S_OK && !window);
    CHECK(SetEnvironmentVariableW(L"WINE_COREWINDOW_AUTOCREATE", L"0"));
    CHECK(((const struct statics_vtbl *)statics->lpVtbl)->GetForCurrentThread(statics, &window) == S_OK && !window);
    CHECK(SetEnvironmentVariableW(L"WINE_COREWINDOW_AUTOCREATE", L"yes"));
    CHECK(((const struct statics_vtbl *)statics->lpVtbl)->GetForCurrentThread(statics, &window) == S_OK && !window);
    CHECK(SetEnvironmentVariableW(L"WINE_COREWINDOW_AUTOCREATE", L"1"));
    CHECK(SetEnvironmentVariableW(L"WINE_COREWINDOW_NO_AUTOCREATE", L"1"));
    CHECK(((const struct statics_vtbl *)statics->lpVtbl)->GetForCurrentThread(statics, &window) == S_OK && !window);
    CHECK(SetEnvironmentVariableW(L"WINE_COREWINDOW_NO_AUTOCREATE", NULL));
    CHECK(((const struct statics_vtbl *)statics->lpVtbl)->GetForCurrentThread(statics, &window) == S_OK && window);
    if (!window) return 1;
    CHECK(IInspectable_QueryInterface(window, &iid_context, (void **)&context) == S_OK);
    CHECK(IInspectable_QueryInterface(window, &iid_adapter, (void **)&adapters) == S_OK);
    CHECK(IInspectable_QueryInterface(window, &iid_interop, (void **)&interop) == S_OK);
    if (!context || !adapters || !interop) return 1;
    CHECK(SetEnvironmentVariableW(L"WINE_COREWINDOW_AUTOCREATE", NULL));
    {
        IInspectable *existing = NULL;
        CHECK(((const struct statics_vtbl *)statics->lpVtbl)->GetForCurrentThread(statics, &existing) == S_OK &&
              existing == window);
        if (existing) IInspectable_Release(existing);
    }
    CHECK(context->lpVtbl->get_UIContext(context, NULL) == E_POINTER);
    CHECK(context->lpVtbl->get_UIContext(context, &first) == S_OK);
    CHECK(context->lpVtbl->get_UIContext(context, &second) == S_OK && first == second);
    if (!first || !second) return 1;
    CHECK(IInspectable_GetIids(first, &count, &iids) == S_OK && count == 1 && IsEqualGUID(iids, &iid_ui));
    CoTaskMemFree(iids);
    CHECK(IInspectable_GetRuntimeClassName(first, &class_name) == S_OK);
    CHECK(!wcscmp(raw_string(class_name, &length), L"Windows.UI.UIContext") && length == 20);
    delete_string(class_name);
    for (i = 0; i < sizeof(context_iids) / sizeof(context_iids[0]); ++i)
    {
        CHECK(IInspectable_QueryInterface(first, context_iids[i], (void **)&identity) == S_OK);
        CHECK((void *)identity == (void *)first);
        if (identity) IUnknown_Release(identity);
    }
    for (i = 0; i < 7; ++i)
    {
        IUnknown *value = &fake;
        CHECK(adapters->lpVtbl->getters[i](adapters, &value) == E_NOTIMPL && !value);
        CHECK(adapters->lpVtbl->getters[i](adapters, NULL) == E_POINTER);
    }
    CHECK(adapters->lpVtbl->SetWindowClientAdapter(adapters, &fake) == S_OK && fake_refs == 2);
    CHECK(adapters->lpVtbl->SetWindowClientAdapter(adapters, &fake) == S_OK && fake_refs == 2);
    CHECK(adapters->lpVtbl->SetWindowClientAdapter(adapters, NULL) == S_OK && fake_refs == 1);
    CHECK(adapters->lpVtbl->SetWindowClientAdapter(adapters, &fake) == S_OK && fake_refs == 2);
    CHECK(((const struct interop_vtbl *)interop->lpVtbl)->get_WindowHandle(interop, &hwnd) == S_OK && hwnd);
    CHECK(DestroyWindow(hwnd));
    IUnknown_Release(interop);
    IInspectable_Release((IInspectable *)adapters);
    IInspectable_Release((IInspectable *)context);
    IInspectable_Release(window);
    window = NULL;
    CHECK(((const struct statics_vtbl *)statics->lpVtbl)->GetForCurrentThread(statics, &window) == S_OK && !window);
    CHECK(fake_refs == 1);
    CHECK(IInspectable_GetRuntimeClassName(first, &class_name) == S_OK);
    delete_string(class_name);
    IInspectable_Release(second);
    CHECK(IInspectable_Release(first) == 0);
    create_window = (void *)GetProcAddress(ui, (LPCSTR)1500);
    CHECK(create_window != NULL);
    if (create_window)
    {
        RECT rect;
        MSG message;
        WCHAR title[64];
        IInspectable *existing = NULL;
        CHECK(create_window(0, NULL, 0, 0, 0, 0, 0, NULL, &iid_window, NULL) == E_POINTER);
        CHECK(create_window(0, NULL, 0, 0, 0, 0, 0, NULL, NULL, (void **)&window) == E_INVALIDARG && !window);
        while (PeekMessageW(&message, NULL, WM_QUIT, WM_QUIT, PM_REMOVE)) {}
        CHECK(create_window(0, NULL, 0, 0, 0, 0, 0, NULL, &iid_static, (void **)&window) == E_NOINTERFACE && !window);
        CHECK(!PeekMessageW(&message, NULL, WM_QUIT, WM_QUIT, PM_REMOVE));
        CHECK(((const struct statics_vtbl *)statics->lpVtbl)->GetForCurrentThread(statics, &window) == S_OK && !window);
        CHECK(create_window(0, L"Ordinal1500 test", 32, 48, 640, 480, 0, NULL,
                            &iid_window, (void **)&window) == S_OK && window);
        if (!window) return 1;
        CHECK(((const struct statics_vtbl *)statics->lpVtbl)->GetForCurrentThread(statics, &existing) == S_OK &&
              existing == window);
        if (existing) IInspectable_Release(existing);
        CHECK(IInspectable_QueryInterface(window, &iid_interop, (void **)&interop) == S_OK);
        if (!interop) return 1;
        CHECK(((const struct interop_vtbl *)interop->lpVtbl)->get_WindowHandle(interop, &hwnd) == S_OK && hwnd);
        CHECK(GetWindowRect(hwnd, &rect) && rect.left == 32 && rect.top == 48 &&
              rect.right - rect.left == 640 && rect.bottom - rect.top == 480);
        CHECK(GetWindowTextW(hwnd, title, sizeof(title) / sizeof(title[0])) &&
              !wcscmp(title, L"Ordinal1500 test"));
        existing = NULL;
        CHECK(create_window(0, NULL, 0, 0, 0, 0, 0, NULL, &iid_window,
                            (void **)&existing) == E_ILLEGAL_METHOD_CALL && !existing);
        CHECK(DestroyWindow(hwnd));
        IUnknown_Release(interop);
        IInspectable_Release(window);
        window = NULL;
        CHECK(((const struct statics_vtbl *)statics->lpVtbl)->GetForCurrentThread(statics, &window) == S_OK && !window);
    }
    IInspectable_Release(statics);
    IInspectable_Release(factory);
    delete_string(name);
    printf("COREWINDOW_CONTEXT_CHECKS=%u FAILURES=%u\n", checks, failures);
    return !!failures;
}
