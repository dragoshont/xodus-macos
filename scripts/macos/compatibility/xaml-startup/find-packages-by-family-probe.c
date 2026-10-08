/* Measures FindPackagesByPackageFamily semantics as reached by bcp47mrm
 * PackageInfoHelper::GetFullName: (family, PACKAGE_FILTER_HEAD, &count=1,
 * names[1], &len=0x80, buffer, NULL). */
#include <windows.h>
#include <stdio.h>

typedef LONG (WINAPI *fpbpf_fn)(const WCHAR *, UINT32, UINT32 *, WCHAR **, UINT32 *, WCHAR *, UINT32 *);

static fpbpf_fn fpbpf;

static void run(const char *label, const WCHAR *family, UINT32 filter, UINT32 count_in, UINT32 len_in,
                BOOL null_count, BOOL null_len, BOOL with_props)
{
    WCHAR *names[8] = {0}, buffer[1024];
    UINT32 count = count_in, len = len_in, props[8], i;
    LONG ret;

    memset(buffer, 0xcc, sizeof(buffer));
    memset(props, 0xcc, sizeof(props));
    ret = fpbpf(family, filter, null_count ? NULL : &count, count_in ? names : NULL,
                null_len ? NULL : &len, len_in ? buffer : NULL, with_props ? props : NULL);
    printf("%s: filter=0x%x ret=%ld count=%u len=%u\n", label, filter, ret, count, len);
    for (i = 0; !ret && i < count && i < 8 && count_in; i++)
        printf("  [%u] %ls props=0x%x off=%d\n", i, names[i] ? names[i] : L"(null)",
               with_props ? props[i] : 0, names[i] ? (int)(names[i] - buffer) : -1);
}

int main(void)
{
    static const UINT32 filters[] = {0x10, 0x20, 0x30, 0x40, 0x80, 0, 0x10 | 0x80000, 0x10 | 0x100000};
    unsigned int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    fpbpf = (fpbpf_fn)GetProcAddress(GetModuleHandleA("kernelbase.dll"), "FindPackagesByPackageFamily");
    printf("fpbpf=%p\n", fpbpf);
    if (!fpbpf) return 1;

    for (i = 0; i < ARRAYSIZE(filters); i++)
        run("gaming", L"Microsoft.GamingApp_8wekyb3d8bbwe", filters[i], 8, 1024, FALSE, FALSE, TRUE);
    run("gaming-bcp47", L"Microsoft.GamingApp_8wekyb3d8bbwe", 0x10, 1, 0x80, FALSE, FALSE, FALSE);
    run("gaming-sizeonly", L"Microsoft.GamingApp_8wekyb3d8bbwe", 0x10, 0, 0, FALSE, FALSE, FALSE);
    run("gaming-shortbuf", L"Microsoft.GamingApp_8wekyb3d8bbwe", 0x10, 1, 4, FALSE, FALSE, FALSE);
    run("gaming-zerocount", L"Microsoft.GamingApp_8wekyb3d8bbwe", 0x10, 0, 0x80, FALSE, FALSE, FALSE);
    run("gaming-lower", L"microsoft.gamingapp_8wekyb3d8bbwe", 0x10, 8, 1024, FALSE, FALSE, FALSE);
    run("xaml-head", L"Microsoft.UI.Xaml.2.8_8wekyb3d8bbwe", 0x10, 8, 1024, FALSE, FALSE, TRUE);
    run("xaml-direct", L"Microsoft.UI.Xaml.2.8_8wekyb3d8bbwe", 0x20, 8, 1024, FALSE, FALSE, TRUE);
    run("unknown", L"Contoso.Missing_8wekyb3d8bbwe", 0x10, 8, 1024, FALSE, FALSE, FALSE);
    run("fullname", L"Microsoft.GamingApp_2609.1001.16.0_x64__8wekyb3d8bbwe", 0x10, 8, 1024, FALSE, FALSE, FALSE);
    run("noseparator", L"Microsoft.GamingApp", 0x10, 8, 1024, FALSE, FALSE, FALSE);
    run("empty", L"", 0x10, 8, 1024, FALSE, FALSE, FALSE);
    run("nullfamily", NULL, 0x10, 8, 1024, FALSE, FALSE, FALSE);
    run("nullcount", L"Microsoft.GamingApp_8wekyb3d8bbwe", 0x10, 8, 1024, TRUE, FALSE, FALSE);
    run("nulllen", L"Microsoft.GamingApp_8wekyb3d8bbwe", 0x10, 8, 1024, FALSE, TRUE, FALSE);
    return 0;
}
