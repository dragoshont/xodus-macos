#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <inspectable.h>
#include <stdio.h>

int main(void)
{
    const WCHAR name[] = L"Windows.ApplicationModel.Resources.Core.ResourceContext";
    HSTRING string;
    IActivationFactory *factory = NULL;
    HRESULT result = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(result))
    {
        printf("resource: RoInitialize hr=%08lx\n", (unsigned long)result);
        return 2;
    }
    result = WindowsCreateString(name, sizeof(name) / sizeof(name[0]) - 1, &string);
    if (SUCCEEDED(result))
    {
        result = RoGetActivationFactory(string, &IID_IActivationFactory, (void **)&factory);
        printf("resource: activation hr=%08lx factory=%p\n",
               (unsigned long)result, (void *)factory);
        if (factory) IActivationFactory_Release(factory);
        WindowsDeleteString(string);
    }
    RoUninitialize();
    return FAILED(result) ? 1 : 0;
}
