#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <stdio.h>

typedef HRESULT (WINAPI *ENUM_CLASSES)(HSTRING, HSTRING **, DWORD *);
typedef HRESULT (WINAPI *REGISTER_FACTORIES)(HSTRING *, void *, UINT32, void **);

int main(void)
{
    static const WCHAR *names[] = {
        L"Microsoft.Xbox.AppL.AppXhg7mrtrf7ayze6gb4syw3zwjgyr2xv4r.mca",
        L"-ServerName:Microsoft.Xbox.AppL.AppXhg7mrtrf7ayze6gb4syw3zwjgyr2xv4r.mca"
    };
    HMODULE module = LoadLibraryW(L"combase.dll");
    ENUM_CLASSES enumerate = (ENUM_CLASSES)(ULONG_PTR)GetProcAddress(module, "RoGetServerActivatableClasses");
    REGISTER_FACTORIES reg = (REGISTER_FACTORIES)(ULONG_PTR)GetProcAddress(module, "RoRegisterActivationFactories");
    void *cookie = NULL;
    HRESULT status;
    unsigned int i;
    int failures = 0;
    if (!enumerate || !reg || FAILED(RoInitialize(RO_INIT_MULTITHREADED))) return 2;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    {
        HSTRING name, *classes = NULL;
        DWORD count = 99;
        if (FAILED(WindowsCreateString(names[i], lstrlenW(names[i]), &name))) return 2;
        status = enumerate(name, &classes, &count);
        printf("UNPACKAGED_ENUM prefix=%u hr=%08lx classes=%p count=%lu\n",
               i, (unsigned long)status, classes, count);
        if (status != REGDB_E_CLASSNOTREG || classes || count) ++failures;
        if (SUCCEEDED(status) && count)
        {
            DWORD j;
            for (j = 0; j < count; ++j) WindowsDeleteString(classes[j]);
            CoTaskMemFree(classes);
        }
        WindowsDeleteString(name);
    }
    status = reg(NULL, NULL, 0, &cookie);
    printf("ZERO_FACTORY_REGISTER hr=%08lx cookie=%p\n", (unsigned long)status, cookie);
    RoUninitialize();
    FreeLibrary(module);
    if (status != E_INVALIDARG || cookie) ++failures;
    printf("FACTORY_BOUNDARY_%s failures=%d\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
