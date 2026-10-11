#define COBJMACROS
#include <stdio.h>
#include "windows.h"
#include "objbase.h"
#include "rpcproxy.h"

static const CLSID proxy_class =
    {0x95e15d0a,0x66e6,0x93d9,{0xc5,0x3c,0x76,0xe6,0x21,0x9d,0x33,0x41}};
static const IID private_view =
    {0x2e5500b6,0x66ad,0x467f,{0xab,0xb5,0x02,0x2a,0x64,0x28,0x3d,0x88}};

int main(void)
{
    HRESULT (WINAPI *get_class)(REFCLSID, REFIID, void **);
    IPSFactoryBuffer *factory = NULL;
    IRpcProxyBuffer *proxy = NULL;
    IUnknown *iface = NULL;
    HMODULE module;
    HRESULT hr;
    WCHAR path[MAX_PATH];
    unsigned failures = 0;

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
    {
        printf("PRIVATE_VIEW_STAGE=initialize HRESULT=%08lx\n", hr);
        return 1;
    }
    module = LoadLibraryW(L"C:\\windows\\system32\\OneCoreUAPCommonProxyStub.dll");
    if (!module)
    {
        printf("PRIVATE_VIEW_STAGE=load WIN32_ERROR=%lu\n", GetLastError());
        CoUninitialize();
        return 1;
    }
    if (!GetModuleFileNameW(module, path, MAX_PATH))
    {
        printf("PRIVATE_VIEW_STAGE=provider WIN32_ERROR=%lu\n", GetLastError());
        FreeLibrary(module);
        CoUninitialize();
        return 1;
    }
    printf("PRIVATE_VIEW_PROXY_PROVIDER=%ls\n", path);
    get_class = (void *)GetProcAddress(module, "DllGetClassObject");
    if (!get_class)
    {
        printf("PRIVATE_VIEW_STAGE=export WIN32_ERROR=%lu\n", GetLastError());
        FreeLibrary(module);
        CoUninitialize();
        return 1;
    }
    hr = get_class(&proxy_class, &IID_IPSFactoryBuffer, (void **)&factory);
    printf("PRIVATE_VIEW_STAGE=factory HRESULT=%08lx\n", hr);
    if (FAILED(hr) || !factory)
        ++failures;
    else
    {
        hr = IPSFactoryBuffer_CreateProxy(factory, NULL, &private_view, &proxy, (void **)&iface);
        printf("PRIVATE_VIEW_STAGE=create_proxy HRESULT=%08lx\n", hr);
        if (FAILED(hr) || !proxy || !iface) ++failures;
        if (iface) IUnknown_Release(iface);
        if (proxy) IRpcProxyBuffer_Release(proxy);
        IPSFactoryBuffer_Release(factory);
    }
    FreeLibrary(module);
    CoUninitialize();
    printf("RESULT private_view_proxy_loader_failures=%u\n", failures);
    return failures ? 1 : 0;
}
