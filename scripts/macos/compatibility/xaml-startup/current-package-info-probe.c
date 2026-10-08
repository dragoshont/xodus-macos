/* Paired probe: GetCurrentPackageInfo / GetCurrentPackageInfo2 package-graph semantics.
 * Run inside a package context (native: Invoke-CommandInDesktopPackage). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <appmodel.h>

typedef LONG (WINAPI *pGCPI)(UINT32, UINT32 *, BYTE *, UINT32 *);
typedef LONG (WINAPI *pGCPI2)(UINT32, UINT32, UINT32 *, BYTE *, UINT32 *);

static long long off(const void *p, const BYTE *b) { return p ? (long long)((const BYTE *)p - b) : -1LL; }

static void dump(const BYTE *buf, UINT32 count, UINT32 len)
{
    const PACKAGE_INFO *pi = (const PACKAGE_INFO *)buf;
    for (UINT32 i = 0; i < count; i++)
    {
        const PACKAGE_ID *id = &pi[i].packageId;
        printf("  [%u] reserved=%u flags=%#x path=+%lld %ls\n", i, pi[i].reserved, pi[i].flags,
               off(pi[i].path, buf), pi[i].path ? pi[i].path : L"(null)");
        printf("      full=+%lld %ls fam=+%lld %ls\n", off(pi[i].packageFullName, buf),
               pi[i].packageFullName ? pi[i].packageFullName : L"(null)", off(pi[i].packageFamilyName, buf),
               pi[i].packageFamilyName ? pi[i].packageFamilyName : L"(null)");
        printf("      id: res=%u arch=%u ver=%u.%u.%u.%u name=+%lld pub=+%lld %ls resId=+%lld %ls pubId=+%lld\n",
               id->reserved, id->processorArchitecture, id->version.Major, id->version.Minor,
               id->version.Build, id->version.Revision, off(id->name, buf), off(id->publisher, buf),
               id->publisher ? id->publisher : L"(null)", off(id->resourceId, buf),
               id->resourceId ? id->resourceId : L"(null)", off(id->publisherId, buf));
    }
    (void)len;
}

int main(void)
{
    HMODULE k = GetModuleHandleW(L"kernelbase.dll");
    pGCPI gcpi = (pGCPI)GetProcAddress(k, "GetCurrentPackageInfo");
    pGCPI2 gcpi2 = (pGCPI2)GetProcAddress(k, "GetCurrentPackageInfo2");
    static const UINT32 flags[] = { 0, PACKAGE_FILTER_HEAD, PACKAGE_FILTER_DIRECT, PACKAGE_FILTER_HEAD | PACKAGE_FILTER_DIRECT,
                                    PACKAGE_FILTER_RESOURCE, PACKAGE_FILTER_BUNDLE, PACKAGE_FILTER_OPTIONAL,
                                    PACKAGE_FILTER_IS_IN_RELATED_SET, PACKAGE_FILTER_STATIC, PACKAGE_FILTER_DYNAMIC,
                                    0xffffffff };
    UINT32 len, count, i, t;
    LONG r;
    BYTE *buf;

    printf("sizeof(PACKAGE_INFO)=%u\n", (UINT32)sizeof(PACKAGE_INFO));
    len = 0; count = 77; r = gcpi(0, &len, NULL, &count);
    printf("GCPI(0) ret=%ld len=%u count=%u\n", r, len, count);
    if (len)
    {
        buf = calloc(1, len + 64); r = gcpi(0, &len, buf, &count);
        printf("GCPI(0,full) ret=%ld len=%u count=%u\n", r, len, count);
        if (!r) dump(buf, count, len);
        free(buf);
    }
    for (t = 0; t < 3; t++)
        for (i = 0; i < ARRAYSIZE(flags); i++)
        {
            len = 0; count = 77;
            r = gcpi2(flags[i], t, &len, NULL, &count);
            printf("GCPI2(flags=%#x,type=%u,NULL) ret=%ld len=%u count=%u\n", flags[i], t, r, len, count);
            if (!len || r != ERROR_INSUFFICIENT_BUFFER) continue;
            if (t != 2 && flags[i] != 0) continue;
            buf = calloc(1, len + 64);
            {
                UINT32 small = len - 1, c2 = 77;
                LONG r2 = gcpi2(flags[i], t, &small, buf, &c2);
                printf("  short ret=%ld len=%u count=%u\n", r2, small, c2);
            }
            r = gcpi2(flags[i], t, &len, buf, &count);
            printf("  full ret=%ld len=%u count=%u\n", r, len, count);
            if (!r) dump(buf, count, len);
            free(buf);
        }
    printf("GCPI2_PROBE_DONE\n");
    return 0;
}
