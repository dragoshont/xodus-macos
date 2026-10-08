/* Paired probe: MrtResourceManager {DBCE7E40-7345-439D-B12C-114A11819A09} from MrmCoreR.dll.
 * Calls DllGetClassObject directly and CoCreateInstance with the IID XAML requests. Run on native and Wine. */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>

static const CLSID CLSID_MrtResourceManager =
    { 0xdbce7e40, 0x7345, 0x439d, { 0xb1, 0x2c, 0x11, 0x4a, 0x11, 0x81, 0x9a, 0x09 } };
static const IID IID_XamlRequested =
    { 0x130a2f65, 0x2be7, 0x4309, { 0x9a, 0x58, 0xa9, 0x05, 0x2f, 0xf2, 0xb6, 0x1c } };

typedef HRESULT (WINAPI *fnDGCO)(REFCLSID, REFIID, void **);

int main(void)
{
    HMODULE mod;
    fnDGCO dgco;
    IClassFactory *cf = NULL;
    IUnknown *unk = NULL;
    HRESULT hr;

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    printf("CoInitializeEx hr=%08lx\n", hr);
    mod = LoadLibraryW(L"MrmCoreR.dll");
    printf("LoadLibrary MrmCoreR=%d err=%lu\n", mod != NULL, mod ? 0 : GetLastError());
    if (!mod) return 1;
    dgco = (fnDGCO)GetProcAddress(mod, "DllGetClassObject");
    hr = dgco(&CLSID_MrtResourceManager, &IID_IClassFactory, (void **)&cf);
    printf("DllGetClassObject(IClassFactory) hr=%08lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IClassFactory_CreateInstance(cf, NULL, &IID_XamlRequested, (void **)&unk);
        printf("CreateInstance({130A2F65}) hr=%08lx\n", hr);
        if (unk) IUnknown_Release(unk), unk = NULL;
        IClassFactory_Release(cf);
    }
    hr = CoCreateInstance(&CLSID_MrtResourceManager, NULL, CLSCTX_INPROC_SERVER, &IID_XamlRequested, (void **)&unk);
    printf("CoCreateInstance({130A2F65}) hr=%08lx\n", hr);
    if (unk) IUnknown_Release(unk), unk = NULL;
    hr = CoCreateInstance(&CLSID_MrtResourceManager, NULL, CLSCTX_INPROC_SERVER, &IID_IUnknown, (void **)&unk);
    printf("CoCreateInstance(IUnknown) hr=%08lx\n", hr);
    if (unk) IUnknown_Release(unk);
    printf("MRT_PROBE_DONE\n");
    return 0;
}
