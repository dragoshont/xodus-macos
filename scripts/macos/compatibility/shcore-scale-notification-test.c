#define COBJMACROS
#include <windows.h>
#include <unknwn.h>
#include <stdio.h>

typedef HRESULT (WINAPI *REGISTER_SINK)(void *, void *, DWORD *);
typedef HRESULT (WINAPI *REVOKE_SINK)(void *, DWORD);
static unsigned checks, failures;
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; printf("FAIL line %u: %s\n", __LINE__, #expr); } } while (0)

struct sink
{
    IUnknown iface;
    LONG refs;
};

static HRESULT WINAPI sink_query(IUnknown *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown)) return E_NOINTERFACE;
    *out = iface;
    IUnknown_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI sink_addref(IUnknown *iface)
{
    return InterlockedIncrement(&((struct sink *)iface)->refs);
}

static ULONG WINAPI sink_release(IUnknown *iface)
{
    return InterlockedDecrement(&((struct sink *)iface)->refs);
}

static IUnknownVtbl sink_vtbl = {sink_query, sink_addref, sink_release};

int main(void)
{
    HMODULE module = LoadLibraryW(L"shcore.dll");
    REGISTER_SINK register_sink;
    REVOKE_SINK revoke_sink;
    DWORD cookie = 123, second = 0, third = 0;
    struct sink sink = {{&sink_vtbl}, 1}, other = {{&sink_vtbl}, 1};

    CHECK(module != NULL);
    if (!module) return 1;
    register_sink = (REGISTER_SINK)(ULONG_PTR)GetProcAddress(module, (LPCSTR)240);
    revoke_sink = (REVOKE_SINK)(ULONG_PTR)GetProcAddress(module, (LPCSTR)246);
    CHECK(register_sink && revoke_sink);
    if (!register_sink || !revoke_sink) return 1;
    CHECK(register_sink(NULL, NULL, NULL) == E_INVALIDARG);
    CHECK(register_sink(NULL, NULL, &cookie) == E_INVALIDARG && !cookie);
    CHECK(register_sink(NULL, &sink.iface, &cookie) == S_OK && cookie);
    CHECK(sink.refs == 2);
    CHECK(register_sink((void *)(ULONG_PTR)1, &sink.iface, &second) == S_OK && second && second != cookie);
    CHECK(sink.refs == 3);
    CHECK(register_sink(NULL, &other.iface, &third) == S_OK && third && third != cookie && third != second);
    CHECK(other.refs == 2);
    CHECK(revoke_sink(NULL, 0) == E_INVALIDARG && sink.refs == 3 && other.refs == 2);
    CHECK(revoke_sink(NULL, cookie) == S_OK && sink.refs == 2);
    CHECK(revoke_sink(NULL, cookie) == E_INVALIDARG && sink.refs == 2);
    CHECK(revoke_sink((void *)(ULONG_PTR)1, second) == S_OK && sink.refs == 1);
    CHECK(revoke_sink(NULL, third) == S_OK && other.refs == 1);
    CHECK(revoke_sink(NULL, third) == E_INVALIDARG && other.refs == 1);
    FreeLibrary(module);
    printf("SHCORE_SCALE_CHECKS=%u FAILURES=%u\n", checks, failures);
    return !!failures;
}
