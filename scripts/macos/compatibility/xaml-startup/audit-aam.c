/* Boundary probe for documented IApplicationActivationManager::ActivateApplication.
 * Mode "create": instantiate the documented class (in-proc) and the inner
 * activation-manager class that twinui.appcore requests with CLSCTX_LOCAL_SERVER.
 * Mode "activate": additionally call ActivateApplication(argv[2]) (Wine stage only). */
#define COBJMACROS
#include <windows.h>
#include <shobjidl.h>
#include <stdio.h>

static const CLSID clsid_aam = {0x45ba127d,0x10a8,0x46ea,{0x8a,0xb7,0x56,0xea,0x90,0x78,0x94,0x3c}};
static const CLSID clsid_inner = {0x6c3ee638,0xb588,0x4d7d,{0xb3,0x0a,0xe7,0xe3,0x67,0x59,0x30,0x5d}};
static const IID iid_aam = {0x2e941141,0x7f97,0x4756,{0xba,0x1d,0x9d,0xec,0xde,0x89,0x4a,0x3d}};

int wmain(int argc, WCHAR **argv)
{
    IApplicationActivationManager *aam = NULL;
    IUnknown *inner = NULL;
    MULTI_QI qi = {&IID_IUnknown, NULL, 0};
    HRESULT hr;
    DWORD pid = 0;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    printf("CoInitializeEx hr=%08lx\n", hr);

    hr = CoCreateInstance(&clsid_aam, NULL, CLSCTX_INPROC_SERVER, &iid_aam, (void **)&aam);
    printf("aam inproc hr=%08lx obj=%d\n", hr, aam != NULL);

    hr = CoCreateInstanceEx(&clsid_inner, NULL, CLSCTX_LOCAL_SERVER, NULL, 1, &qi);
    printf("inner local_server hr=%08lx qi.hr=%08lx obj=%d\n", hr, qi.hr, qi.pItf != NULL);
    if (qi.pItf) IUnknown_Release(qi.pItf);

    if (argc > 2 && !wcscmp(argv[1], L"activate") && aam)
    {
        hr = IApplicationActivationManager_ActivateApplication(aam, argv[2], NULL, AO_NONE, &pid);
        printf("ActivateApplication hr=%08lx pid=%lu\n", hr, pid);
    }
    if (aam) IApplicationActivationManager_Release(aam);
    (void)inner;
    CoUninitialize();
    return 0;
}
