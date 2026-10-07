#include <windows.h>
#include <stdio.h>

typedef HRESULT (WINAPI *design_fn)(BOOL *);

static void run(HMODULE m, int ord, const char *name)
{
    design_fn fn = (design_fn)GetProcAddress(m, MAKEINTRESOURCEA(ord));
    BOOL v = 0x55;
    HRESULT hr;

    printf("%s #%d byname=%d\n", name, ord, GetProcAddress(m, name) != NULL);
    if (!fn) { printf("  missing\n"); return; }
    hr = fn(NULL);
    printf("  NULL hr=%08lx\n", hr);
    hr = fn(&v);
    printf("  hr=%08lx value=%d\n", hr, v);
    v = 0x55;
    hr = fn(&v);
    printf("  again hr=%08lx value=%d\n", hr, v);
}

int main(void)
{
    HMODULE m = LoadLibraryA("combase.dll");
    HANDLE tok;

    run(m, 90, "RoGetDesignMode");
    run(m, 157, "RoGetDesignModeV2");
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_IMPERSONATE, &tok) &&
        ImpersonateSelf(SecurityImpersonation))
    {
        printf("impersonating\n");
        run(m, 90, "RoGetDesignMode");
        run(m, 157, "RoGetDesignModeV2");
        RevertToSelf();
    }
    return 0;
}
