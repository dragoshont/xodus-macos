#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <objidl.h>
#include <roapi.h>
#include <winstring.h>
#include <stdio.h>

static ULONG server_refs(IUnknown *server)
{
    IUnknown_AddRef(server);
    return IUnknown_Release(server);
}

int main(void)
{
    static const IID interfaces[] = {
        {0x00000035,0,0,{0xc0,0,0,0,0,0,0,0x46}},
        {0xaf86e2e0,0xb12d,0x4c6a,{0x9c,0x5a,0xd7,0xaa,0x65,0x10,0x1e,0x90}}
    };
    IUnknown *server = NULL;
    HSTRING name;
    ULONG before, after;
    unsigned int pass, kind;
    int failures = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (FAILED(RoInitialize(RO_INIT_MULTITHREADED))) return 2;
    WindowsCreateString(L"Windows.Foundation.Collections.PropertySet", 42, &name);
    if (FAILED(RoGetActivationFactory(name, &interfaces[0], (void **)&server))) return 2;
    WindowsDeleteString(name);
    before = server_refs(server);
    for (kind = 0; kind < 2; ++kind)
    {
        CLSID clsid;
        IPSFactoryBuffer *ps = NULL;
        HRESULT hr = CoGetPSClsid(&interfaces[kind], &clsid);
        if (FAILED(hr) || FAILED(CoGetClassObject(&clsid, CLSCTX_INPROC_SERVER, NULL,
                &IID_IPSFactoryBuffer, (void **)&ps))) return 2;
        for (pass = 0; pass < 500; ++pass)
        {
            IRpcStubBuffer *stub = NULL;
            hr = IPSFactoryBuffer_CreateStub(ps, &interfaces[kind], NULL, &stub);
            if (hr != S_OK || !stub) return 3;
            if (IRpcStubBuffer_IsIIDSupported(stub, &interfaces[kind])) ++failures;
            hr = IRpcStubBuffer_Connect(stub, server);
            if (hr != S_OK) { if (!pass) printf("FIRST_CONNECT_ERROR kind=%u hr=%08lx\n", kind, (unsigned long)hr); ++failures; }
            if (!IRpcStubBuffer_IsIIDSupported(stub, &interfaces[kind])) ++failures;
            IRpcStubBuffer_Disconnect(stub);
            if (IRpcStubBuffer_IsIIDSupported(stub, &interfaces[kind])) ++failures;
            IRpcStubBuffer_Disconnect(stub);
            if (IRpcStubBuffer_Connect(stub, server) != S_OK) ++failures;
            IRpcStubBuffer_Disconnect(stub);
            {
                ULONG refs = IRpcStubBuffer_Release(stub);
                if (!pass) printf("RELEASE_DIAGNOSTIC_REFS kind=%u refs=%lu\n", kind, refs);
            }
        }
        IPSFactoryBuffer_Release(ps);
        printf("NULL_CREATE_CONNECT_DISCONNECT kind=%u cycles=500 failures=%d\n", kind, failures);
    }
    after = server_refs(server);
    printf("REAL_NATIVE_FACTORY_REFS before=%lu after=%lu\n", before, after);
    if (before != after) ++failures;
    IUnknown_Release(server); RoUninitialize();
    printf("NULL_STUB_LIFETIME_%s failures=%d\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
