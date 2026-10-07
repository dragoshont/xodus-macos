#include <windows.h>
#include <stdio.h>

typedef BOOL (WINAPI *fn_is)(void);

static void probe(const char *dll)
{
    HMODULE h = LoadLibraryA(dll);
    fn_is f;
    BOOL r;
    if (!h) { printf("%s load=0 gle=%lu\n", dll, GetLastError()); return; }
    f = (fn_is)GetProcAddress(h, "IsOneCoreTransformMode");
    printf("%s byname=%d\n", dll, f != NULL);
    if (!f) return;
    SetLastError(0xdead);
    r = f();
    printf("%s ret=%d gle=%lu\n", dll, r, GetLastError());
}

static DWORD WINAPI thr(void *p)
{
    fn_is f = (fn_is)GetProcAddress(GetModuleHandleA("user32.dll"), "IsOneCoreTransformMode");
    if (f) printf("thread ret=%d\n", f());
    return 0;
}

int main(void)
{
    HANDLE t;
    probe("user32.dll");
    probe("api-ms-win-rtcore-ntuser-private-l1-1-7.dll");
    printf("en byname=%d\n", GetProcAddress(GetModuleHandleA("user32.dll"), "EnableOneCoreTransformMode") != NULL);
    t = CreateThread(NULL, 0, thr, NULL, 0, NULL);
    WaitForSingleObject(t, INFINITE);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    probe("user32.dll");
    return 0;
}
