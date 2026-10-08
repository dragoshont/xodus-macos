/* Paired probe: GetCurrentPackageId semantics vs PackageIdFromFullName.
 * Run inside a package context (native: Invoke-CommandInDesktopPackage;
 * Wine: catalog-registered package launch) and without identity. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <appmodel.h>

typedef LONG (WINAPI *pGCPI)(UINT32 *, BYTE *);
typedef LONG (WINAPI *pGCPFN)(UINT32 *, WCHAR *);
typedef LONG (WINAPI *pPIFFN)(const WCHAR *, UINT32, UINT32 *, BYTE *);

static void dump(const char *tag, LONG r, UINT32 len, BYTE *buf)
{
    PACKAGE_ID *id = (PACKAGE_ID *)buf;
    printf("%s ret=%ld len=%u\n", tag, r, len);
    if (r || !buf) return;
    printf("  reserved=%u arch=%u ver=%u.%u.%u.%u\n", id->reserved, id->processorArchitecture,
           id->version.Major, id->version.Minor, id->version.Build, id->version.Revision);
    printf("  name=+%lld %ls\n", id->name ? (long long)((BYTE *)id->name - buf) : -1LL, id->name ? id->name : L"(null)");
    printf("  publisher=+%lld %ls\n", id->publisher ? (long long)((BYTE *)id->publisher - buf) : -1LL, id->publisher ? id->publisher : L"(null)");
    printf("  resourceId=+%lld %ls\n", id->resourceId ? (long long)((BYTE *)id->resourceId - buf) : -1LL, id->resourceId ? id->resourceId : L"(null)");
    printf("  publisherId=+%lld %ls\n", id->publisherId ? (long long)((BYTE *)id->publisherId - buf) : -1LL, id->publisherId ? id->publisherId : L"(null)");
}

int main(void)
{
    HMODULE k = GetModuleHandleW(L"kernelbase.dll");
    pGCPI gcpi = (pGCPI)GetProcAddress(k, "GetCurrentPackageId");
    pGCPFN gcpfn = (pGCPFN)GetProcAddress(k, "GetCurrentPackageFullName");
    pPIFFN piffn = (pPIFFN)GetProcAddress(k, "PackageIdFromFullName");
    WCHAR full[256];
    UINT32 len, flen = 256;
    BYTE *buf;
    LONG r;

    r = gcpfn(&flen, full);
    printf("GetCurrentPackageFullName ret=%ld len=%u %ls\n", r, flen, r ? L"" : full);

    len = 0; r = gcpi(&len, NULL);
    printf("GCPI(0,NULL) ret=%ld len=%u\n", r, len);
    {
        UINT32 small = 8; BYTE sb[8];
        r = gcpi(&small, sb);
        printf("GCPI(8,buf) ret=%ld len=%u\n", r, small);
    }
    if (len)
    {
        UINT32 l2 = len;
        buf = calloc(1, len + 64);
        r = gcpi(&l2, buf);
        dump("GCPI(full)", r, l2, buf);
        free(buf);
    }
    if (!gcpfn(&flen, full))
    {
        UINT32 flags[2] = { PACKAGE_INFORMATION_BASIC, PACKAGE_INFORMATION_FULL };
        for (int i = 0; i < 2; i++)
        {
            UINT32 l = 0;
            r = piffn(full, flags[i], &l, NULL);
            printf("PIFFN(flags=%#x,NULL) ret=%ld len=%u\n", flags[i], r, l);
            if (l)
            {
                buf = calloc(1, l + 64);
                r = piffn(full, flags[i], &l, buf);
                dump(i ? "PIFFN(FULL)" : "PIFFN(BASIC)", r, l, buf);
                free(buf);
            }
        }
    }
    printf("CPID_PROBE_DONE\n");
    return 0;
}
