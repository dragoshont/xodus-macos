/* Measures GetCurrentPackageGlobalizationContext / GetPackageGlobalizationProperty
 * (kernelbase, api-ms-win-appmodel-runtime-internal-l1-1-6+), reached from
 * MrmCoreR UseWindowsDisplayLanguageFromAppxManifest. */
#include <windows.h>
#include <appmodel.h>
#include <stdio.h>

typedef LONG (WINAPI *gcpgc_t)(UINT32, UINT32, void **);
typedef LONG (WINAPI *gpgp_t)(void *, UINT32, UINT32 *, BYTE *);
typedef LONG (WINAPI *gcpfn_t)(UINT32 *, WCHAR *);

int main(void)
{
    HMODULE kb = GetModuleHandleW(L"kernelbase.dll");
    gcpgc_t gcpgc = (gcpgc_t)GetProcAddress(kb, "GetCurrentPackageGlobalizationContext");
    gpgp_t gpgp = (gpgp_t)GetProcAddress(kb, "GetPackageGlobalizationProperty");
    void *ctx[8] = {0}, *c;
    UINT32 kind, prop, size;
    BYTE buf[16];
    LONG r;
    WCHAR name[256]; UINT32 len = 256;

    if (!gcpgc || !gpgp) { printf("missing exports %p %p\n", gcpgc, gpgp); return 1; }
    r = GetCurrentPackageFullName(&len, name);
    printf("package r=%ld %ls\n", r, r ? L"-" : name);
    printf("null-out r=%ld\n", gcpgc(0, 0, NULL));
    for (kind = 0; kind < 8; kind++)
    {
        c = (void *)0xdeadbeef;
        r = gcpgc(kind, 0, &c);
        ctx[kind] = r ? NULL : c;
        printf("kind=%u r=%ld ctx=%s", kind, r, c == (void *)0xdeadbeef ? "untouched" : c ? "set" : "null");
        if (!r && kind) printf(" same-as-kind0=%d", c == ctx[0]);
        /* measurement only: native node layout read by kernelbase (flags at +8, name at +0x14) */
        if (!r) printf(" node-flags=%08lx node-name=%.80ls", *(DWORD *)((BYTE *)c + 8), (WCHAR *)((BYTE *)c + 0x14));
        if (!r)
            for (prop = 0; prop < 4; prop++)
            {
                memset(buf, 0xcc, sizeof(buf)); size = 8;
                r = gpgp(c, prop, &size, buf);
                printf(" p%u:r=%ld,size=%u,val=%08lx,b4=%02x", prop, r, size, *(DWORD *)buf, buf[4]);
            }
        printf("\n");
    }
    r = gcpgc(0, 1, &c); printf("flags1 r=%ld same=%d\n", r, !r && c == ctx[0]);
    r = gcpgc(0, 0xffffffff, &c); printf("flagsff r=%ld same=%d\n", r, !r && c == ctx[0]);
    if (ctx[0])
    {
        size = 3; memset(buf, 0xcc, 8); r = gpgp(ctx[0], 2, &size, buf); printf("short3 r=%ld size=%u b0=%02x\n", r, size, buf[0]);
        size = 0; r = gpgp(ctx[0], 2, &size, buf); printf("zero r=%ld size=%u\n", r, size);
        size = 4; r = gpgp(ctx[0], 2, &size, NULL); printf("nullbuf r=%ld size=%u\n", r, size);
        r = gpgp(ctx[0], 2, NULL, buf); printf("nullsize r=%ld\n", r);
    }
    size = 4; r = gpgp(NULL, 2, &size, buf); printf("nullctx r=%ld\n", r);
    return 0;
}
