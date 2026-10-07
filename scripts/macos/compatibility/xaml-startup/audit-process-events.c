#include <windows.h>
#include <objbase.h>
#include <stdio.h>

typedef void (WINAPI *ctx_fn)(void *);
typedef void (WINAPI *void_fn)(void);
typedef DWORD (WINAPI *wait_fn)(void *, DWORD, const HANDLE *, DWORD, DWORD, DWORD);
typedef BOOL (WINAPI *prio_fn)(int);

static HMODULE cb;

static FARPROC ord(int n) { return GetProcAddress(cb, MAKEINTRESOURCEA(n)); }

static void run(const char *label)
{
    ctx_fn begin = (ctx_fn)ord(86), end = (ctx_fn)ord(88), setd = (ctx_fn)ord(110);
    wait_fn wait = (wait_fn)ord(87);
    void_fn prio = (void_fn)ord(111);
    prio_fn sched = (prio_fn)ord(133);
    unsigned char ctx[0x70];
    HANDLE ev = CreateEventA(NULL, TRUE, TRUE, NULL), hs[0x39];
    DWORD r, i, zero = 0;
    MSG msg;

    printf("[%s] present=%d%d%d%d%d%d\n", label, !!begin, !!wait, !!end, !!setd, !!prio, !!sched);
    if (!begin || !wait || !end || !setd || !prio || !sched) return;
    memset(ctx, 0xcc, sizeof(ctx));
    begin(ctx);
    for (i = 0; i < 0x60; i++) zero += !ctx[i];
    printf("  begin zeroed=%lu after=%02x\n", zero, ctx[0x60]);
    for (i = 0; i < 0x39; i++) hs[i] = ev;
    SetLastError(0xdead); r = wait(ctx, 0x39, hs, 0, QS_ALLINPUT, 0);
    printf("  wait count=0x39 r=%08lx gle=%lu\n", r, GetLastError());
    SetLastError(0xdead); r = wait(ctx, 1, hs, 0, QS_ALLINPUT, MWMO_WAITALL);
    printf("  wait waitall r=%08lx gle=%lu\n", r, GetLastError());
    SetLastError(0xdead); r = wait(ctx, 1, hs, 0, QS_ALLINPUT, 0);
    printf("  wait signaled r=%08lx gle=%lu\n", r, GetLastError());
    ResetEvent(ev);
    r = wait(ctx, 1, hs, 0, QS_ALLINPUT, 0);
    printf("  wait timeout r=%08lx\n", r);
    r = wait(ctx, 0, NULL, 0, QS_ALLINPUT, 0);
    printf("  wait zero r=%08lx\n", r);
    PostThreadMessageA(GetCurrentThreadId(), WM_USER, 0, 0);
    r = wait(ctx, 1, hs, 0, QS_ALLINPUT, 0);
    printf("  wait message r=%08lx\n", r);
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE));
    printf("  sched0=%d sched1=%d sched2=%d\n", sched(0), sched(1), sched(2));
    setd(NULL); prio(); end(ctx);
    printf("  after end first=%02x\n", ctx[0]);
    CloseHandle(ev);
}

int main(void)
{
    cb = LoadLibraryA("combase.dll");
    run("nocom");
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    run("sta");
    CoUninitialize();
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    run("mta");
    CoUninitialize();
    return 0;
}
