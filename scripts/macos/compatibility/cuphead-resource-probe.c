#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <inspectable.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    WCHAR name[256] = L"Windows.ApplicationModel.Resources.Core.ResourceContext";
    HSTRING string;
    IActivationFactory *factory = NULL;
    HRESULT result;
    if (argc > 3 || (argc >= 2 && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        argv[1], -1, name, sizeof(name) / sizeof(name[0]))) ||
        (argc == 3 && strcmp(argv[2], "--activate")))
    {
        puts("Usage: cuphead-resource-probe.exe [activatable-class-name [--activate]]");
        return 2;
    }
    result = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(result))
    {
        printf("resource: RoInitialize hr=%08lx\n", (unsigned long)result);
        return 2;
    }
    result = WindowsCreateString(name, lstrlenW(name), &string);
    if (SUCCEEDED(result))
    {
        result = RoGetActivationFactory(string, &IID_IActivationFactory, (void **)&factory);
        printf("resource: activation hr=%08lx factory=%p\n",
               (unsigned long)result, (void *)factory);
        if (SUCCEEDED(result) && factory && argc == 3)
        {
            IInspectable *instance = NULL;
            result = IActivationFactory_ActivateInstance(factory, &instance);
            printf("resource: instance hr=%08lx object=%p\n",
                   (unsigned long)result, (void *)instance);
            if (instance) IInspectable_Release(instance);
        }
        if (factory) IActivationFactory_Release(factory);
        WindowsDeleteString(string);
    }
    RoUninitialize();
    return FAILED(result) ? 1 : 0;
}
