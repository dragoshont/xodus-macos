/* Measures PackageFamilyNameFromFullName, reached by MrmCoreR after the
 * StateRepository cache lookup. */
#include <windows.h>
#include <stdio.h>

typedef LONG (WINAPI *pfnff_fn)(const WCHAR *, UINT32 *, WCHAR *);

static pfnff_fn pfnff;

static void run(const char *label, const WCHAR *name, UINT32 len, BOOL null_len, BOOL null_buf)
{
    WCHAR buf[300];
    UINT32 l = len;
    LONG r;

    memset(buf, 0xcc, sizeof(buf));
    r = pfnff(name, null_len ? NULL : &l, null_buf ? NULL : buf);
    printf("%-14s len_in=%-3u ret=%-4ld len=%-3u", label, len, r, null_len ? 0 : l);
    if (!null_buf)
    {
        if (buf[0] == 0xcccc) printf(" buf=untouched");
        else printf(" buf=%ls", buf);
    }
    printf("\n");
}

int wmain(int argc, WCHAR **argv)
{
    static const WCHAR *cases[] = {
        L"Microsoft.GamingApp_2609.1001.16.0_x64__8wekyb3d8bbwe",
        L"Microsoft.VCLibs.140.00_14.0.33519.0_arm64__8wekyb3d8bbwe",
        L"Contoso.App_1.2.3.4_neutral_split.scale-100_8wekyb3d8bbwe",
        L"Contoso.App_1.2.3.4_x86_~_8wekyb3d8bbwe",
        L"Contoso.App_1.2.3_x64__8wekyb3d8bbwe",
        L"Contoso.App_1.2.3.4_foo__8wekyb3d8bbwe",
        L"Contoso.App_1.2.3.4_x64__8wekyb3d8bbw",
        L"Contoso.App_1.2.3.4_x64__8WEKYB3D8BBWE",
        L"Contoso.App_1.2.3.4_x64_",
        L"Contoso.App_8wekyb3d8bbwe",
        L"abc",
        L"",
    };
    HMODULE mod = LoadLibraryW(L"kernelbase.dll");
    unsigned int i;
    UINT32 n;

    pfnff = (pfnff_fn)GetProcAddress(mod, "PackageFamilyNameFromFullName");
    printf("export=%d\n", pfnff != NULL);
    if (!pfnff) return 1;

    for (i = 0; i < ARRAYSIZE(cases); i++)
    {
        printf("[%ls]\n", cases[i]);
        run("  size", cases[i], 0, FALSE, TRUE);
        run("  size+buf", cases[i], 0, FALSE, FALSE);
        n = 0;
        pfnff(cases[i], &n, NULL);
        if (n > 1) run("  short", cases[i], n - 1, FALSE, FALSE);
        if (n) run("  exact", cases[i], n, FALSE, FALSE);
        run("  big", cases[i], 300, FALSE, FALSE);
    }
    printf("[edges]\n");
    run("  NULL name", NULL, 300, FALSE, FALSE);
    run("  NULL len", cases[0], 0, TRUE, FALSE);
    run("  len NULL buf", cases[0], 300, FALSE, TRUE);
    for (i = 1; i < (UINT32)argc; i++)
    {
        printf("[%ls]\n", argv[i]);
        run("  big", argv[i], 300, FALSE, FALSE);
    }
    return 0;
}
