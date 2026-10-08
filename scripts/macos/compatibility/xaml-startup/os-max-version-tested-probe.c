/* Paired native/Wine probe for the private kernelbase export AppXGetOSMaxVersionTested.
 * ABI from MrmCoreR call sites: HRESULT (PCWSTR packageFullName, UINT64 *version); callers
 * preset *version and test the result with a signed check. */
#include <windows.h>
#include <stdio.h>

typedef LONG (WINAPI *fn_t)(const WCHAR *, UINT64 *);

static void run(fn_t fn, const WCHAR *name, int null_out)
{
    UINT64 v = 0x1111222233334444ull;
    LONG hr = fn(name, null_out ? NULL : &v);
    printf("%ls null_out=%d hr=0x%08lx v=%u.%u.%u.%u (0x%016llx)\n", name ? name : L"(null)", null_out,
           (unsigned long)hr, (unsigned)(v >> 48), (unsigned)(v >> 32 & 0xffff), (unsigned)(v >> 16 & 0xffff),
           (unsigned)(v & 0xffff), (unsigned long long)v);
}

int wmain(int argc, WCHAR **argv)
{
    HMODULE k = GetModuleHandleW(L"kernelbase.dll");
    fn_t fn = (fn_t)GetProcAddress(k, "AppXGetOSMaxVersionTested");
    WCHAR full[256];
    UINT32 len = ARRAYSIZE(full);
    LONG (WINAPI *gcpfn)(UINT32 *, WCHAR *) = (void *)GetProcAddress(k, "GetCurrentPackageFullName");
    int i;

    if (!fn) { printf("no export\n"); return 1; }
    if (gcpfn && !gcpfn(&len, full)) run(fn, full, 0);
    else printf("no current package\n");
    for (i = 1; i < argc; i++) run(fn, argv[i], 0);
    run(fn, L"Missing.Package_1.0.0.0_x64__8wekyb3d8bbwe", 0);
    run(fn, L"not a full name", 0);
    run(fn, L"", 0);
    fflush(stdout);
    run(fn, NULL, 0);
    fflush(stdout);
    if (len < ARRAYSIZE(full)) run(fn, full, 1);
    return 0;
}
