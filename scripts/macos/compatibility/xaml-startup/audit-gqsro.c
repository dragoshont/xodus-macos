#include <windows.h>
#include <stdio.h>

#ifndef QS_SMRESULT
#define QS_SMRESULT 0x8000
#endif

typedef DWORD (WINAPI *gqs_fn)(UINT);

int main(void)
{
    HMODULE u = LoadLibraryA("user32.dll");
    gqs_fn byname = (gqs_fn)GetProcAddress(u, "GetQueueStatusReadonly");
    gqs_fn byord = (gqs_fn)GetProcAddress(u, MAKEINTRESOURCEA(2541));
    gqs_fn ro = byname ? byname : byord;
    MSG msg;
    DWORD r;

    printf("byname=%d byord=%d same=%d\n", !!byname, !!byord, byname == byord);
    if (!ro) return 1;

    SetLastError(0xdead); r = ro(0x80000000);
    printf("invalid ro=%08lx gle=%lu\n", r, GetLastError());
    SetLastError(0xdead); r = GetQueueStatus(0x80000000);
    printf("invalid gqs=%08lx gle=%lu\n", r, GetLastError());
    SetLastError(0xdead); r = ro(QS_ALLINPUT | QS_ALLPOSTMESSAGE | QS_SMRESULT);
    printf("maxflags ro=%08lx gle=%lu\n", r, GetLastError());

    PeekMessageA(&msg, NULL, 0, 0, PM_NOREMOVE);
    GetQueueStatus(QS_ALLINPUT);
    printf("empty ro=%08lx\n", ro(QS_ALLINPUT));
    PostThreadMessageA(GetCurrentThreadId(), WM_USER, 0, 0);
    printf("posted ro1=%08lx ro2=%08lx\n", ro(QS_ALLINPUT), ro(QS_ALLINPUT));
    SetLastError(0xdead);
    printf("mixed inv=%08lx smres=%08lx b16=%08lx allpost=%08lx gle=%lu\n", ro(0x80000000 | QS_POSTMESSAGE),
           ro(QS_SMRESULT | QS_POSTMESSAGE), ro(0x10000 | QS_POSTMESSAGE), ro(QS_ALLPOSTMESSAGE), GetLastError());
    printf("subset ro_timer=%08lx ro_post=%08lx ro0=%08lx\n", ro(QS_TIMER), ro(QS_POSTMESSAGE), ro(0));
    printf("gqs=%08lx\n", GetQueueStatus(QS_POSTMESSAGE));
    printf("after-gqs ro=%08lx\n", ro(QS_ALLINPUT));
    PostThreadMessageA(GetCurrentThreadId(), WM_USER + 1, 0, 0);
    printf("posted2 ro=%08lx gqs_timer=%08lx ro=%08lx\n", ro(QS_ALLINPUT), GetQueueStatus(QS_TIMER), ro(QS_ALLINPUT));
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE));
    printf("drained ro=%08lx gqs=%08lx ro=%08lx\n", ro(QS_ALLINPUT), GetQueueStatus(QS_ALLINPUT), ro(QS_ALLINPUT));
    return 0;
}
