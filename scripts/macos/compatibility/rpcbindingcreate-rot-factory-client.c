#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <roapi.h>
#include <winstring.h>
#include <stdio.h>
#include <wchar.h>

int main(void)
{
    static const IID factory_iid = {0x00000035, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
    static const WCHAR item[] = L"Xodus.WinRT.Factory.v1|Microsoft.GamingApp_2609.1001.16.0_x64__8wekyb3d8bbwe|"
        L"Microsoft.Xbox.AppL|Microsoft.Xbox.AppL.AppXj1x023cvxecgx8b22mdm03jfv63tazgg.mca";
    IRunningObjectTable *rot;
    IMoniker *moniker;
    IUnknown *object = NULL, *actual = NULL;
    IClassFactory *factory = NULL;
    HRESULT hr;
    int i;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) return 2;
    if (FAILED(GetRunningObjectTable(0, &rot)) || FAILED(CreateItemMoniker(L"!", item, &moniker))) return 2;
    for (i = 0; i < 50; ++i)
    {
        hr = IRunningObjectTable_GetObject(rot, moniker, &object);
        if (SUCCEEDED(hr)) break;
        Sleep(100);
    }
    printf("ROT_PAIRED_CLIENT pid=%lu lookup=%08lx object=%p\n", GetCurrentProcessId(), (unsigned long)hr, object);
    if (SUCCEEDED(hr)) hr = IUnknown_QueryInterface(object, &IID_IClassFactory, (void **)&factory);
    printf("ROT_FACTORY_QI=%08lx factory=%p\n", (unsigned long)hr, factory);
    if (SUCCEEDED(hr))
    {
        hr = IClassFactory_CreateInstance(factory, NULL, &factory_iid, (void **)&actual);
        printf("ROT_LAZY_CREATE=%08lx actual_IActivationFactory=%p\n", (unsigned long)hr, actual);
        if (actual)
        {
            typedef HRESULT (WINAPI *GET_IIDS)(void *, ULONG *, IID **);
            typedef HRESULT (WINAPI *GET_NAME)(void *, HSTRING *);
            void *same = NULL;
            ULONG count = 0;
            IID *iids = NULL;
            HSTRING name = NULL;
            hr = IUnknown_QueryInterface(actual, &factory_iid, &same);
            printf("ROT_REMOTE_FACTORY_QI=%08lx same=%p\n", (unsigned long)hr, same);
            if (same) IUnknown_Release((IUnknown *)same);
            hr = ((GET_IIDS)(*(void ***)actual)[3])(actual, &count, &iids);
            printf("ROT_REMOTE_GETIIDS=%08lx count=%lu\n", (unsigned long)hr, count);
            CoTaskMemFree(iids);
            hr = ((GET_NAME)(*(void ***)actual)[4])(actual, &name);
            printf("ROT_REMOTE_RUNTIMECLASSNAME=%08lx value=%ls\n", (unsigned long)hr,
                   name ? WindowsGetStringRawBuffer(name, NULL) : L"<none>");
            WindowsDeleteString(name);
            IUnknown_Release(actual); actual = NULL;
        }
        Sleep(8500);
        hr = IClassFactory_CreateInstance(factory, NULL, &factory_iid, (void **)&actual);
        printf("ROT_HELD_FACTORY_AFTER_REVOKE=%08lx object=%p\n", (unsigned long)hr, actual);
        if (actual) IUnknown_Release(actual);
        IClassFactory_Release(factory);
    }
    if (object) IUnknown_Release(object);
    object = NULL;
    hr = IRunningObjectTable_GetObject(rot, moniker, &object);
    printf("ROT_LOOKUP_AFTER_REVOKE=%08lx object=%p\n", (unsigned long)hr, object);
    if (object) IUnknown_Release(object);
    IMoniker_Release(moniker); IRunningObjectTable_Release(rot); CoUninitialize();
    return 0;
}
