#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>

static const IID iid_target = {0x638bb2db,0x451d,0x4661,{0xb0,0x99,0x41,0x4f,0x34,0xff,0xb9,0xf1}};
static const IID iid_inspectable = {0xaf86e2e0,0xb12d,0x4c6a,{0x9c,0x5a,0xd7,0xaa,0x65,0x10,0x1e,0x90}};

/* minimal outer object for the proxy */
static HRESULT WINAPI o_qi(IUnknown *i, REFIID r, void **p) { *p = NULL; return E_NOINTERFACE; }
static ULONG WINAPI o_ar(IUnknown *i) { return 2; }
static ULONG WINAPI o_rl(IUnknown *i) { return 1; }
static IUnknownVtbl ovt = { o_qi, o_ar, o_rl };
static IUnknown outer = { &ovt };

int main(void)
{
    CLSID ps;
    IPSFactoryBuffer *fac = NULL;
    IRpcStubBuffer *stub = NULL;
    IRpcProxyBuffer *proxy = NULL;
    void *obj = NULL;
    HRESULT hr;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    hr = CoGetPSClsid(&iid_target, &ps);
    printf("CoGetPSClsid hr=%08lx clsid=%08lx-%04x\n", hr, ps.Data1, ps.Data2);
    if (FAILED(hr)) return 1;
    hr = CoGetClassObject(&ps, CLSCTX_INPROC_SERVER, NULL, &IID_IPSFactoryBuffer, (void **)&fac);
    printf("CoGetClassObject hr=%08lx\n", hr);
    if (FAILED(hr)) return 1;

    hr = IPSFactoryBuffer_CreateStub(fac, &iid_target, NULL, &stub);
    printf("CreateStub(target,NULL) hr=%08lx stub=%d\n", hr, stub != NULL);
    if (stub)
    {
        printf("  IsIIDSupported(target)=%d (unconnected)\n", IRpcStubBuffer_IsIIDSupported(stub, &iid_target) != NULL);
        printf("  CountRefs=%lu\n", IRpcStubBuffer_CountRefs(stub));
        printf("  Release=%lu\n", IRpcStubBuffer_Release(stub));
    }
    hr = IPSFactoryBuffer_CreateStub(fac, &iid_inspectable, NULL, &stub);
    printf("CreateStub(IInspectable) hr=%08lx\n", hr);

    hr = IPSFactoryBuffer_CreateProxy(fac, &outer, &iid_target, &proxy, &obj);
    printf("CreateProxy(target) hr=%08lx proxy=%d obj=%d\n", hr, proxy != NULL, obj != NULL);
    if (obj)
    {
        IUnknown *u = obj;
        void **vt = *(void ***)obj;
        HMODULE m1 = 0, m6 = 0;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, vt[3], &m1);
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, vt[6], &m6);
        {
            char a[MAX_PATH] = "?", b[MAX_PATH] = "?";
            if (m1) GetModuleFileNameA(m1, a, MAX_PATH);
            if (m6) GetModuleFileNameA(m6, b, MAX_PATH);
            printf("  vtbl[3] in %s\n  vtbl[6] in %s\n", strrchr(a,'\\') ? strrchr(a,'\\')+1 : a, strrchr(b,'\\') ? strrchr(b,'\\')+1 : b);
        }
        IUnknown_Release(u);
    }
    if (proxy) printf("  proxy Release=%lu\n", IRpcProxyBuffer_Release(proxy));
    printf("factory Release=%lu\n", IPSFactoryBuffer_Release(fac));
    CoUninitialize();
    return 0;
}
