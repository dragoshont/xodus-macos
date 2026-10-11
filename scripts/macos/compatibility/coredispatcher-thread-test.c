#define COBJMACROS
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_UI_Core
#include <stdio.h>
#include <stddef.h>
#include "initguid.h"
#include "windows.h"
#include "roapi.h"
#include "winstring.h"
#include "windows.ui.core.h"

/* Verified-static Windows.UI private vtables; descriptive method names. */
typedef struct thread_factory thread_factory;
struct thread_factory_vtbl
{
    IInspectableVtbl base;
    HRESULT (WINAPI *GetForCurrentThread)(thread_factory *, ICoreDispatcher **);
    HRESULT (WINAPI *GetOrCreateForCurrentThread)(thread_factory *, ICoreDispatcher **);
};
struct thread_factory { const struct thread_factory_vtbl *lpVtbl; };
typedef struct pump pump;
struct pump_vtbl
{
    IUnknownVtbl base;
    HRESULT (WINAPI *PumpUntilSignaled)(pump *, HANDLE);
    HRESULT (WINAPI *BeginShutdown)(pump *);
};
struct pump { const struct pump_vtbl *lpVtbl; };
static const GUID iid_factory = {0x4b4d0861,0xd718,0x4f7c,{0xbe,0xc7,0x73,0x5c,0x06,0x5f,0x7c,0x73}};
static const GUID iid_pump = {0x34fff979,0xbd36,0x4bb1,{0x82,0xd2,0x78,0x5a,0x60,0x5b,0x81,0xfb}};
static const GUID iid_interop = {0x45d64a29,0xa63e,0x4cb6,{0xb4,0x98,0x57,0x81,0xd2,0x98,0xcb,0x4f}};
static unsigned checks, failures;
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; printf("FAIL line %u: %s\n", __LINE__, #expr); } } while (0)

struct work
{
    thread_factory *factory;
    pump *main_pump;
    ICoreDispatcher *main_dispatcher, *retained;
    HWND hwnd;
    HANDLE finish, dispatched;
    HRESULT wrong_thread, lookup, create, repeat;
    BOOL absent, independent, same, posted, signaled;
    DWORD message_wait;
};

static LRESULT CALLBACK wndproc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_APP + 1)
    {
        SetEvent((HANDLE)lp);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}

static DWORD WINAPI worker(void *param)
{
    struct work *work = param;
    ICoreDispatcher *existing = (void *)1, *second = NULL;
    work->wrong_thread = work->main_pump->lpVtbl->PumpUntilSignaled(work->main_pump, work->finish);
    work->lookup = work->factory->lpVtbl->GetForCurrentThread(work->factory, &existing);
    work->absent = !existing;
    work->create = work->factory->lpVtbl->GetOrCreateForCurrentThread(work->factory, &work->retained);
    work->independent = work->retained && work->retained != work->main_dispatcher;
    work->repeat = work->factory->lpVtbl->GetForCurrentThread(work->factory, &second);
    work->same = second == work->retained;
    if (second) ICoreDispatcher_Release(second);
    if (existing && existing != (void *)1) ICoreDispatcher_Release(existing);
    work->posted = PostMessageW(work->hwnd, WM_APP + 1, 0, (LPARAM)work->dispatched);
    work->message_wait = WaitForSingleObject(work->dispatched, 3000);
    work->signaled = SetEvent(work->finish);
    return 0;
}

int main(int argc, char **argv)
{
    HRESULT (WINAPI *create_window)(DWORD, PCWSTR, INT, INT, UINT, UINT, DWORD, HWND, REFIID, void **);
    HRESULT (WINAPI *get_dll_factory)(HSTRING, IActivationFactory **);
    struct work work = {0};
    HMODULE module = argc > 1 ? LoadLibraryA(argv[1]) : LoadLibraryW(L"windows.ui.dll");
    WCHAR path[MAX_PATH];
    HSTRING name = NULL, runtime_name = (void *)1;
    IActivationFactory *activation = NULL, *dll_factory = NULL;
    thread_factory *factory = NULL;
    ICoreDispatcher *dispatcher = (void *)1, *second = NULL, *window_dispatcher = NULL;
    ICoreWindow *window = NULL;
    IUnknown *private_identity = NULL, *public_identity = NULL;
    IUnknown *interop = NULL;
    struct interop_vtbl { IUnknownVtbl base; HRESULT (WINAPI *get_WindowHandle)(IUnknown *, HWND *); };
    pump *private = NULL;
    TrustLevel trust = BaseTrust;
    IID *iids = NULL;
    ULONG count = 0;
    WNDCLASSW cls = {0};
    HANDLE thread = NULL;
    HWND test_hwnd = NULL, core_hwnd = NULL;
    MSG msg;
    boolean access = FALSE;
    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);

    CHECK(SUCCEEDED(hr));
    if (FAILED(hr)) return 1;
    CHECK(sizeof(struct thread_factory_vtbl) == 8 * sizeof(void *));
    CHECK(sizeof(struct pump_vtbl) == 5 * sizeof(void *));
    CHECK(module != NULL);
    if (!module) goto done;
    CHECK(GetModuleFileNameW(module, path, MAX_PATH) != 0);
    printf("DISPATCHER_PROVIDER=%ls\n", path);
    CHECK(WindowsCreateString(L"Windows.UI.Core.CoreDispatcher", 30, &name) == S_OK);
    CHECK(RoGetActivationFactory(name, &IID_IActivationFactory, (void **)&activation) == S_OK);
    if (!activation) goto done;
    get_dll_factory = (void *)GetProcAddress(module, "DllGetActivationFactory");
    CHECK(get_dll_factory != NULL);
    if (!get_dll_factory) goto done;
    CHECK(get_dll_factory(name, &dll_factory) == S_OK && dll_factory);
    CHECK(dll_factory == activation);
    CHECK(IActivationFactory_QueryInterface(activation, &iid_factory, (void **)&factory) == S_OK);
    if (!factory) goto done;
    CHECK(IInspectable_GetRuntimeClassName((IInspectable *)factory, &runtime_name) == E_ILLEGAL_METHOD_CALL && !runtime_name);
    CHECK(IInspectable_GetTrustLevel((IInspectable *)factory, &trust) == S_OK && trust == FullTrust);
    CHECK(IInspectable_GetIids((IInspectable *)factory, &count, &iids) == S_OK && count == 2 && iids);
    CoTaskMemFree(iids);
    CHECK(factory->lpVtbl->GetForCurrentThread(factory, NULL) == E_POINTER);
    CHECK(factory->lpVtbl->GetOrCreateForCurrentThread(factory, NULL) == E_POINTER);
    CHECK(factory->lpVtbl->GetForCurrentThread(factory, &dispatcher) == S_OK && !dispatcher);
    CHECK(factory->lpVtbl->GetOrCreateForCurrentThread(factory, &dispatcher) == S_OK && dispatcher);
    if (!dispatcher) goto done;
    CHECK(factory->lpVtbl->GetForCurrentThread(factory, &second) == S_OK && second == dispatcher);
    ICoreDispatcher_Release(second);
    second = NULL;
    CHECK(factory->lpVtbl->GetOrCreateForCurrentThread(factory, &second) == S_OK && second == dispatcher);
    CHECK(ICoreDispatcher_get_HasThreadAccess(dispatcher, &access) == S_OK && access);
    CHECK(ICoreDispatcher_QueryInterface(dispatcher, &iid_pump, (void **)&private) == S_OK);
    if (!private) goto done;
    CHECK(IUnknown_QueryInterface((IUnknown *)private, &IID_IUnknown, (void **)&private_identity) == S_OK);
    CHECK(ICoreDispatcher_QueryInterface(dispatcher, &IID_IUnknown, (void **)&public_identity) == S_OK);
    CHECK(private_identity == public_identity);
    cls.lpfnWndProc = wndproc;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = L"Xodus.Dispatcher.Test";
    CHECK(RegisterClassW(&cls) != 0);
    test_hwnd = CreateWindowExW(0, cls.lpszClassName, L"", 0, 0, 0, 10, 10, HWND_MESSAGE, NULL, cls.hInstance, NULL);
    CHECK(test_hwnd != NULL);
    work.factory = factory; work.main_pump = private; work.main_dispatcher = dispatcher; work.hwnd = test_hwnd;
    work.finish = CreateEventW(NULL, TRUE, FALSE, NULL);
    work.dispatched = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(work.finish && work.dispatched);
    if (!test_hwnd || !work.finish || !work.dispatched) goto done;
    thread = CreateThread(NULL, 0, worker, &work, 0, NULL);
    CHECK(thread != NULL);
    if (!thread) goto done;
    CHECK(private->lpVtbl->PumpUntilSignaled(private, work.finish) == S_OK);
    CHECK(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    CHECK(work.wrong_thread == RPC_E_WRONG_THREAD);
    CHECK(work.lookup == S_OK && work.absent);
    CHECK(work.create == S_OK && work.independent);
    CHECK(work.repeat == S_OK && work.same);
    CHECK(work.posted && work.message_wait == WAIT_OBJECT_0 && work.signaled);
    /* The thread's TLS reference has gone; only the returned reference remains. */
    CHECK(work.retained && ICoreDispatcher_Release(work.retained) == 0);
    work.retained = NULL;
    create_window = (void *)GetProcAddress(module, (LPCSTR)(ULONG_PTR)1500);
    CHECK(create_window != NULL);
    if (!create_window) goto done;
    CHECK(create_window(0, L"Dispatcher identity", 0, 0, 100, 100, 0, NULL, &IID_ICoreWindow, (void **)&window) == S_OK && window);
    if (!window) goto done;
    CHECK(ICoreWindow_get_Dispatcher(window, &window_dispatcher) == S_OK && window_dispatcher == dispatcher);
    CHECK(ICoreWindow_QueryInterface(window, &iid_interop, (void **)&interop) == S_OK);
    if (!interop) goto done;
    CHECK(((const struct interop_vtbl *)interop->lpVtbl)->get_WindowHandle(interop, &core_hwnd) == S_OK && core_hwnd);
    CHECK(DestroyWindow(core_hwnd));
    core_hwnd = NULL;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {}
    PostQuitMessage(0);
    CHECK(private->lpVtbl->PumpUntilSignaled(private, NULL) == E_APPLICATION_EXITING);
    CHECK(private->lpVtbl->BeginShutdown(private) == S_OK);
    CHECK(private->lpVtbl->BeginShutdown(private) == S_OK);
    CHECK(private->lpVtbl->PumpUntilSignaled(private, NULL) == E_APPLICATION_EXITING);
done:
    if (thread) { WaitForSingleObject(thread, INFINITE); CloseHandle(thread); }
    if (core_hwnd) DestroyWindow(core_hwnd);
    if (test_hwnd) DestroyWindow(test_hwnd);
    if (work.finish) CloseHandle(work.finish);
    if (work.dispatched) CloseHandle(work.dispatched);
    if (interop) IUnknown_Release(interop);
    if (window_dispatcher) ICoreDispatcher_Release(window_dispatcher);
    if (window) ICoreWindow_Release(window);
    if (private_identity) IUnknown_Release(private_identity);
    if (public_identity) IUnknown_Release(public_identity);
    if (private) IUnknown_Release((IUnknown *)private);
    if (second) ICoreDispatcher_Release(second);
    if (dispatcher && dispatcher != (void *)1) ICoreDispatcher_Release(dispatcher);
    if (factory) IInspectable_Release((IInspectable *)factory);
    if (dll_factory) IActivationFactory_Release(dll_factory);
    if (activation) IActivationFactory_Release(activation);
    WindowsDeleteString(name);
    RoUninitialize();
    if (module) FreeLibrary(module);
    printf("coredispatcher_thread_checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
