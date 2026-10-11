#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>

#ifndef RO_E_MUST_BE_AGILE
#define RO_E_MUST_BE_AGILE ((HRESULT)0x8000001c)
#endif

typedef HRESULT (WINAPI *REGISTER)(IApartmentShutdown *, UINT64 *, void **);
typedef HRESULT (WINAPI *UNREGISTER)(void *);
typedef HRESULT (WINAPI *QUERY_ID)(UINT64 *);
static REGISTER register_shutdown;
static UNREGISTER unregister_shutdown;
static QUERY_ID query_id;
static unsigned checks, failures;
static LONG marker_refs = 1;
static const GUID agile_iid = {0x94ea2b94,0xe9cc,0x49e0,{0xc0,0xff,0xee,0x64,0xca,0x8f,0x5b,0x90}};

struct listener
{
    IApartmentShutdown iface;
    LONG refs, calls, queries;
    UINT64 notified_id;
    BOOL agile;
    HRESULT reentrant, reentrant_null, reentrant_unknown;
    void *cookie;
};

static void check(BOOL valid, const char *name)
{
    checks++;
    if (!valid) { failures++; printf("FAIL %s\n", name); }
}

static struct listener *from_iface(IApartmentShutdown *iface)
{
    return CONTAINING_RECORD(iface, struct listener, iface);
}

/* A distinct IUnknown-only marker catches accidental slot-3 callback dispatch. */
static HRESULT WINAPI marker_qi(IUnknown *iface, REFIID iid, void **out)
{
    (void)iid;
    *out = iface;
    InterlockedIncrement(&marker_refs);
    return S_OK;
}
static ULONG WINAPI marker_addref(IUnknown *iface) { (void)iface; return InterlockedIncrement(&marker_refs); }
static ULONG WINAPI marker_release(IUnknown *iface) { (void)iface; return InterlockedDecrement(&marker_refs); }
static IUnknownVtbl marker_vtbl = {marker_qi, marker_addref, marker_release};
static IUnknown marker = {&marker_vtbl};

static HRESULT WINAPI listener_qi(IApartmentShutdown *iface, REFIID iid, void **out)
{
    struct listener *listener = from_iface(iface);
    listener->queries++;
    *out = NULL;
    if (IsEqualGUID(iid, &agile_iid) && listener->agile)
    {
        *out = &marker;
        IUnknown_AddRef(&marker);
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG WINAPI listener_addref(IApartmentShutdown *iface)
{
    return InterlockedIncrement(&from_iface(iface)->refs);
}
static ULONG WINAPI listener_release(IApartmentShutdown *iface)
{
    return InterlockedDecrement(&from_iface(iface)->refs);
}
static void WINAPI listener_notify(IApartmentShutdown *iface, UINT64 identifier)
{
    struct listener *listener = from_iface(iface);
    listener->calls++;
    listener->notified_id = identifier;
    check(listener->refs == 3, "callback has stored and protective references");
    listener->reentrant = unregister_shutdown(listener->cookie);
    listener->reentrant_null = unregister_shutdown(NULL);
    listener->reentrant_unknown = unregister_shutdown((void *)0x123456);
}
static IApartmentShutdownVtbl listener_vtbl =
{
    listener_qi, listener_addref, listener_release, listener_notify
};

struct worker
{
    void *cookie;
    BOOL mta;
    HRESULT result;
};
static DWORD WINAPI foreign_unregister(void *argument)
{
    struct worker *worker = argument;
    HRESULT hr = CoInitializeEx(NULL, worker->mta ? COINIT_MULTITHREADED : COINIT_APARTMENTTHREADED);
    worker->result = FAILED(hr) ? hr : unregister_shutdown(worker->cookie);
    if (SUCCEEDED(hr)) CoUninitialize();
    return 0;
}

struct implicit_worker
{
    struct listener *listener;
    UINT64 identifier;
    HRESULT registered, unregistered;
};
static DWORD WINAPI implicit_register(void *argument)
{
    struct implicit_worker *worker = argument;
    void *cookie = NULL;
    worker->registered = register_shutdown(&worker->listener->iface, &worker->identifier, &cookie);
    worker->unregistered = SUCCEEDED(worker->registered) ? unregister_shutdown(cookie) : E_FAIL;
    return 0;
}

int main(void)
{
    HMODULE module = LoadLibraryW(L"combase.dll");
    struct listener listener = {{&listener_vtbl}, 1, 0, 0, 0, TRUE, 0, 0, 0, NULL};
    UINT64 identifier = 0xdead, expected = 0;
    void *cookie = (void *)0x123456, *second = NULL;
    HRESULT hr;
    unsigned i;

    register_shutdown = module ? (REGISTER)(ULONG_PTR)GetProcAddress(module, (LPCSTR)120) : NULL;
    unregister_shutdown = module ? (UNREGISTER)(ULONG_PTR)GetProcAddress(module, (LPCSTR)121) : NULL;
    query_id = module ? (QUERY_ID)(ULONG_PTR)GetProcAddress(module, (LPCSTR)122) : NULL;
    check(register_shutdown && unregister_shutdown && query_id, "all three private exports present");
    if (!register_shutdown || !unregister_shutdown || !query_id) goto done;
    check(register_shutdown(NULL, &identifier, &cookie) == CO_E_NOTINITIALIZED &&
          identifier == 0xdead && cookie == (void *)0x123456, "no apartment precedes listener validation and leaves outputs");
    check(unregister_shutdown(NULL) == CO_E_NOTINITIALIZED, "no apartment precedes NULL cookie validation");
    check(unregister_shutdown(cookie) == CO_E_NOTINITIALIZED, "no apartment precedes unknown cookie validation");
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    check(SUCCEEDED(hr), "STA initialized");
    if (FAILED(hr)) goto done;
    check(query_id(&expected) == S_OK && expected, "real apartment identifier");
    check(register_shutdown(NULL, &identifier, &cookie) == E_POINTER, "defensive NULL-listener divergence");
    listener.agile = FALSE;
    check(register_shutdown(&listener.iface, &identifier, &cookie) == RO_E_MUST_BE_AGILE &&
          identifier == expected && !cookie && listener.refs == 1, "non-agile listener rejected with grounded outputs");
    listener.agile = TRUE;
    check(register_shutdown(&listener.iface, &identifier, &cookie) == S_OK &&
          identifier == expected && cookie && listener.refs == 2, "original listener retained with real identity and cookie");
    check(register_shutdown(&listener.iface, &identifier, &second) == S_OK &&
          second && second != cookie && listener.refs == 3, "registrations have unique cookies");
    check(listener.queries == 3, "one agility QI per registration, no shutdown-interface QI");
    check(marker_refs == 1, "distinct agility marker released immediately");
    check(unregister_shutdown(NULL) == E_INVALIDARG, "NULL cookie rejected in initialized apartment");
    check(unregister_shutdown((void *)0x123456) == E_INVALIDARG, "unknown cookie rejected without dereference");
    for (i = 0; i < 2; i++)
    {
        struct worker worker = {cookie, i != 0, E_FAIL};
        HANDLE thread = CreateThread(NULL, 0, foreign_unregister, &worker, 0, NULL);
        check(thread != NULL, "foreign apartment worker created");
        if (thread)
        {
            DWORD wait = WaitForSingleObject(thread, 10000);
            check(wait == WAIT_OBJECT_0 && worker.result == E_INVALIDARG, "foreign apartment cannot consume registration");
            if (wait != WAIT_OBJECT_0) ExitProcess(2);
            CloseHandle(thread);
        }
    }
    check(unregister_shutdown(cookie) == S_OK && listener.refs == 2, "own apartment unregister releases listener");
    check(unregister_shutdown(cookie) == E_INVALIDARG, "stale cookie rejected");
    check(unregister_shutdown(second) == S_OK && listener.refs == 1, "second unregister releases listener");
    CoUninitialize();
    check(!listener.calls && listener.refs == 1, "unregistered listeners not notified");
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    check(SUCCEEDED(hr), "new STA initialized for teardown");
    if (FAILED(hr)) goto done;
    check(query_id(&expected) == S_OK && expected != identifier, "new apartment lifetime identity");
    check(register_shutdown(&listener.iface, &identifier, &listener.cookie) == S_OK &&
          listener.refs == 2, "teardown listener registered");
    CoUninitialize();
    check(listener.calls == 1 && listener.notified_id == expected && listener.refs == 1,
          "real teardown notifies original pointer exactly once and releases it");
    check(listener.reentrant == E_UNEXPECTED && listener.reentrant_null == E_UNEXPECTED &&
          listener.reentrant_unknown == E_UNEXPECTED, "teardown reentrancy precedes cookie validation");
    check(unregister_shutdown(listener.cookie) == CO_E_NOTINITIALIZED, "consumed cookie after teardown cannot be reused");
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    check(SUCCEEDED(hr), "MTA initialized");
    if (FAILED(hr)) goto done;
    check(register_shutdown(&listener.iface, &identifier, &listener.cookie) == S_OK &&
          query_id(&expected) == S_OK && identifier == expected, "MTA registration uses actual MTA identity");
    {
        struct implicit_worker worker = {&listener, 0, E_FAIL, E_FAIL};
        HANDLE thread = CreateThread(NULL, 0, implicit_register, &worker, 0, NULL);
        check(thread != NULL, "uninitialized MTA worker created");
        if (thread)
        {
            DWORD wait = WaitForSingleObject(thread, 10000);
            check(wait == WAIT_OBJECT_0 && worker.registered == S_OK &&
                  worker.unregistered == S_OK && worker.identifier == expected && listener.refs == 2,
                  "implicit MTA registration and unregister use owning MTA");
            if (wait != WAIT_OBJECT_0) ExitProcess(2);
            CloseHandle(thread);
        }
    }
    CoUninitialize();
    check(listener.calls == 2 && listener.notified_id == expected && listener.refs == 1,
          "MTA teardown notifies and releases listener");
done:
    printf("RESULT apartment_shutdown_checks=%u failures=%u\n", checks, failures);
    if (module) FreeLibrary(module);
    return failures ? 1 : 0;
}
