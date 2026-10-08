/* Paired native/Wine probe for GetStagedPackagePathByFullName2 (and the v1/GetPackagePathByFullName
 * relatives). Documented ABI: LONG (PCWSTR fullName, PackagePathType type, UINT32 *pathLength, PWSTR path).
 * Usage: staged-package-path-probe.exe <fullName> [<fullName> ...] */
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef LONG (WINAPI *v2_t)(const WCHAR *, UINT32, UINT32 *, WCHAR *);
typedef LONG (WINAPI *v1_t)(const WCHAR *, UINT32 *, WCHAR *);

static WCHAR buf[4096];

static void v2_case(v2_t fn, const char *tag, const WCHAR *name, UINT32 type)
{
    UINT32 len = 0, need;
    LONG ret;

    ret = fn(name, type, &len, NULL);
    printf("  v2 %s type %u size: ret=%ld len=%u\n", tag, type, ret, len);
    if (ret != ERROR_INSUFFICIENT_BUFFER || !len || len > ARRAYSIZE(buf)) return;
    need = len;
    len = need - 1;
    memset(buf, 0xcc, sizeof(buf));
    ret = fn(name, type, &len, buf);
    printf("  v2 %s type %u short: ret=%ld len=%u buf0=%04x\n", tag, type, ret, len, buf[0]);
    len = need;
    ret = fn(name, type, &len, buf);
    printf("  v2 %s type %u fill: ret=%ld len=%u path=%ls tail=%04x\n", tag, type, ret, len,
           ret ? L"-" : buf, buf[need]);
    len = need + 5;
    memset(buf, 0xcc, sizeof(buf));
    ret = fn(name, type, &len, buf);
    printf("  v2 %s type %u big: ret=%ld len=%u\n", tag, type, ret, len);
}

static void v1_case(v1_t fn, const char *api, const WCHAR *name)
{
    UINT32 len = 0;
    LONG ret = fn(name, &len, NULL);
    printf("  %s size: ret=%ld len=%u", api, ret, len);
    if (ret == ERROR_INSUFFICIENT_BUFFER && len <= ARRAYSIZE(buf))
    {
        ret = fn(name, &len, buf);
        printf(" fill: ret=%ld len=%u path=%ls", ret, len, ret ? L"-" : buf);
    }
    printf("\n");
}

int wmain(int argc, WCHAR **argv)
{
    HMODULE kb = GetModuleHandleA("kernelbase.dll");
    v2_t v2 = (v2_t)GetProcAddress(kb, "GetStagedPackagePathByFullName2");
    v1_t v1 = (v1_t)GetProcAddress(kb, "GetStagedPackagePathByFullName");
    v1_t pp = (v1_t)GetProcAddress(kb, "GetPackagePathByFullName");
    UINT32 len, type;
    LONG ret;
    int i;

    printf("exports v2=%d v1=%d GetPackagePathByFullName=%d\n", !!v2, !!v1, !!pp);
    if (!v2) return 1;
    for (i = 1; i < argc; i++)
    {
        printf("%ls\n", argv[i]);
        for (type = 0; type <= 6; type++) v2_case(v2, "name", argv[i], type);
        if (v1) v1_case(v1, "v1", argv[i]);
        if (pp) v1_case(pp, "GetPackagePathByFullName", argv[i]);
    }
    printf("edge cases\n");
    v2_case(v2, "unknown", L"Contoso.Missing_1.0.0.0_x64__8wekyb3d8bbwe", 0);
    v2_case(v2, "malformed", L"not a full name", 0);
    v2_case(v2, "empty", L"", 0);
    len = 0;
    ret = v2(NULL, 0, &len, NULL);
    printf("  v2 NULL name: ret=%ld len=%u\n", ret, len);
    if (argc > 1)
    {
        ret = v2(argv[1], 0, NULL, NULL);
        printf("  v2 NULL len: ret=%ld\n", ret);
        len = 8;
        ret = v2(argv[1], 0, &len, NULL);
        printf("  v2 len 8 NULL path: ret=%ld len=%u\n", ret, len);
        len = 0;
        ret = v2(argv[1], 7, &len, NULL);
        printf("  v2 type 7: ret=%ld len=%u\n", ret, len);
    }
    return 0;
}
