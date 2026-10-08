/* Measures shcore ordinal 265 (GetScaleFactorForCoreWindow) semantics as
 * reached by MrmCoreR ScaleQVProvider: (NULL window, &scale preset to 100). */
#include <windows.h>
#include <stdio.h>

typedef HRESULT (WINAPI *gsfcw_fn)(IUnknown *, DWORD *);
typedef HRESULT (WINAPI *gsffm_fn)(HMONITOR, DWORD *);

int main(void)
{
    HMODULE shcore = LoadLibraryA("shcore.dll");
    gsfcw_fn gsfcw = shcore ? (gsfcw_fn)GetProcAddress(shcore, MAKEINTRESOURCEA(265)) : NULL;
    gsffm_fn gsffm = shcore ? (gsffm_fn)GetProcAddress(shcore, "GetScaleFactorForMonitor") : NULL;
    DWORD scale;
    HRESULT hr;
    POINT pt = {0, 0};

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("shcore=%p ord265=%p\n", shcore, gsfcw);
    if (!gsfcw) return 1;

    scale = 100;
    hr = gsfcw(NULL, &scale);
    printf("null-window preset100: hr=0x%08lx scale=%lu\n", hr, scale);
    scale = 0xdeadbeef;
    hr = gsfcw(NULL, &scale);
    printf("null-window presetbad: hr=0x%08lx scale=0x%lx\n", hr, scale);
    if (gsffm)
    {
        scale = 0;
        hr = gsffm(MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY), &scale);
        printf("primary-monitor GetScaleFactorForMonitor: hr=0x%08lx scale=%lu\n", hr, scale);
    }
    printf("system-dpi=%u\n", GetDpiForSystem());
    printf("thread-desktop=%p\n", GetThreadDesktop(GetCurrentThreadId()));
    /* Native stores *out unconditionally: a NULL out pointer faults. Keep last. */
    printf("null-window nullout: calling\n");
    hr = gsfcw(NULL, NULL);
    printf("null-window nullout: hr=0x%08lx\n", hr);
    return 0;
}
