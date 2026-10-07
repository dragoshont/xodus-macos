/* v2: measures re-arm/schedule semantics of user32 ordinal 2669 (InitThreadCoreMessagingIocp2) and 2670
 * (DrainThreadCoreMessagingCompletions2) as reached by CoreMessaging.dll:
 *   HANDLE r = Init2(HWND hwnd, DWORD *out);   (r != 0 && *out < 2 required)
 *   BOOL   b = Drain2(HANDLE);                  (argument identity measured)
 * Risky variants run in child processes: audit-cmiocp.exe <mode>. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <string.h>

typedef HANDLE (WINAPI *fnInit2)(HWND, DWORD *);
typedef ULONG_PTR (WINAPI *fnDrain2)(ULONG_PTR);
typedef struct { void *Key; void *Apc; IO_STATUS_BLOCK Iosb; } FIOCI;
typedef NTSTATUS (NTAPI *fnRemEx)(HANDLE, FIOCI *, ULONG, ULONG *, LARGE_INTEGER *, BOOLEAN);
typedef NTSTATUS (NTAPI *fnQO)(HANDLE, ULONG, void *, ULONG, ULONG *);
typedef NTSTATUS (NTAPI *fnQIoC)(HANDLE, ULONG, void *, ULONG, ULONG *);

static fnInit2 pInit2;
static fnDrain2 pDrain2;
static fnRemEx pRemEx;
static fnQO pQO;
static fnQIoC pQIoC;
static HWND g_other;
static HANDLE g_ready, g_go;

static LRESULT CALLBACK wp(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

static HWND mkwin(void)
{
    static ATOM a;
    if (!a) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = wp; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"cmiocp";
        a = RegisterClassExW(&wc);
    }
    return CreateWindowExW(0, L"cmiocp", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandleW(NULL), NULL);
}

static void objinfo(const char *tag, HANDLE h)
{
    unsigned char buf[1024]; ULONG r = 0; NTSTATUS s;
    struct { ULONG Attr, Access, Handles, Pointers; } *bi = (void *)buf;
    s = pQO(h, 0, buf, 56, &r);
    if (!s) printf("%s basic access=%08lx handles=%lu pointers=%lu attr=%lx\n", tag, bi->Access, bi->Handles, bi->Pointers, bi->Attr);
    else printf("%s basic status=%08lx\n", tag, s);
    s = pQO(h, 2, buf, sizeof(buf), &r);
    if (!s) printf("%s type=%.*S\n", tag, ((UNICODE_STRING *)buf)->Length / 2, ((UNICODE_STRING *)buf)->Buffer);
    else printf("%s type status=%08lx\n", tag, s);
    { LONG depth = -1; s = pQIoC(h, 0, &depth, sizeof(depth), &r); printf("%s iocdepth status=%08lx depth=%ld\n", tag, s, depth); }
}

static int drainport(const char *tag, HANDLE h)
{
    FIOCI e[16]; ULONG n = 0, i; LARGE_INTEGER z; NTSTATUS s;
    z.QuadPart = 0; memset(e, 0, sizeof(e));
    s = pRemEx(h, e, 16, &n, &z, FALSE);
    printf("%s remove status=%08lx n=%lu\n", tag, s, s ? 0 : n);
    if (s) return 0;
    for (i = 0; i < n; i++)
        printf("%s   pkt[%lu] key=%p apc=%p status=%08lx info=%llx\n", tag, i, e[i].Key, e[i].Apc,
               (ULONG)e[i].Iosb.Status, (unsigned long long)e[i].Iosb.Information);
    return n;
}

static void qs(const char *tag)
{
    MSG m; DWORD st = GetQueueStatus(QS_ALLINPUT);
    BOOL p = PeekMessageW(&m, NULL, 0, 0, PM_NOREMOVE);
    printf("%s queuestatus=%08lx peek=%d msg=%04x\n", tag, st, p, p ? m.message : 0);
}

static void pump(void) { MSG m; while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&m); }

static DWORD WINAPI other_thread(LPVOID p)
{
    g_other = mkwin();
    SetEvent(g_ready);
    WaitForSingleObject(g_go, INFINITE);
    if (p) { /* sender mode */
        SendNotifyMessageW((HWND)p, WM_USER + 5, 5, 5);
        PostMessageW((HWND)p, WM_USER + 6, 6, 6);
    }
    return 0;
}

static void call_init(const char *tag, HWND hwnd, HANDLE *res)
{
    DWORD out = 0xcccccccc; HANDLE r;
    SetLastError(0xdeadbeef);
    r = pInit2(hwnd, &out);
    printf("%s init2(hwnd=%p) ret=%p out=%08lx gle=%lu\n", tag, hwnd, r, out, GetLastError());
    if (r) objinfo(tag, r);
    if (res) *res = r;
}

static void call_drain(const char *tag, ULONG_PTR arg)
{
    ULONG_PTR r;
    SetLastError(0xdeadbeef);
    r = pDrain2(arg);
    printf("%s drain2(%llx) ret=%llx gle=%lu\n", tag, (unsigned long long)arg, (unsigned long long)r, GetLastError());
}

typedef INT (WINAPI *fnSched)(HWND);
static fnSched pSched;
static HWND g_target; static DWORD g_delay; static UINT g_kind;
static DWORD WINAPI delayed_poster(LPVOID p)
{
    Sleep(g_delay);
    if (g_kind == 0) PostMessageW(g_target, WM_USER + 0x20, 1, 2);
    else if (g_kind == 1) SendNotifyMessageW(g_target, WM_USER + 0x21, 3, 4);
    else PostThreadMessageW((DWORD)(ULONG_PTR)p, WM_USER + 0x22, 5, 6);
    return 0;
}
static void waitport(const char *tag, HANDLE h, DWORD ms)
{
    FIOCI e[16]; ULONG n = 0, i; LARGE_INTEGER t; NTSTATUS s; DWORD t0 = GetTickCount();
    t.QuadPart = -(LONGLONG)ms * 10000; memset(e, 0, sizeof(e));
    s = pRemEx(h, e, 16, &n, &t, FALSE);
    printf("%s wait status=%08lx n=%lu dt=%lu\n", tag, s, s ? 0 : n, GetTickCount() - t0);
    if (!s) for (i = 0; i < n; i++)
        printf("%s   pkt[%lu] key=%p apc=%p status=%08lx info=%llx\n", tag, i, e[i].Key, e[i].Apc,
               (ULONG)e[i].Iosb.Status, (unsigned long long)e[i].Iosb.Information);
}
static void sched(const char *tag, HWND w)
{
    INT r; SetLastError(0xdeadbeef); r = pSched(w);
    printf("%s sched(%p) ret=%d gle=%lu\n", tag, w, r, GetLastError());
}
static void delayed(HWND w, DWORD ms, UINT kind)
{
    HANDLE t; g_target = w; g_delay = ms; g_kind = kind;
    t = CreateThread(NULL, 0, delayed_poster, (LPVOID)(ULONG_PTR)GetCurrentThreadId(), 0, NULL);
    CloseHandle(t);
}
int main(int argc, char **argv)
{
    HMODULE u = LoadLibraryA("user32.dll");
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    HWND w, w2, w3; HANDLE h = NULL, h2 = NULL; const char *m = argc > 1 ? argv[1] : "";
    pInit2 = (fnInit2)GetProcAddress(u, MAKEINTRESOURCEA(2669));
    pDrain2 = (fnDrain2)GetProcAddress(u, MAKEINTRESOURCEA(2670));
    pSched = (fnSched)GetProcAddress(u, MAKEINTRESOURCEA(2582));
    pRemEx = (fnRemEx)GetProcAddress(nt, "NtRemoveIoCompletionEx");
    pQO = (fnQO)GetProcAddress(nt, "NtQueryObject");
    pQIoC = (fnQIoC)GetProcAddress(nt, "NtQueryIoCompletion");
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("mode %s init2=%p drain2=%p sched=%p\n", m, pInit2, pDrain2, pSched);
    if (!pInit2 || !pDrain2 || !pSched) return 2;
    w = mkwin();
    if (!strcmp(m, "count")) {
        w2 = mkwin(); w3 = mkwin();
        call_init("K1", w, &h); call_init("K2", w2, NULL); call_init("K3", w3, NULL);
        DestroyWindow(w2); w2 = mkwin(); call_init("K4", w2, NULL);
        printf("K closing\n"); CloseHandle(h); w3 = mkwin(); call_init("K5", w3, NULL);
    } else if (!strcmp(m, "drainx")) {
        w2 = mkwin(); call_drain("DX-unreg-pre", (ULONG_PTR)w);
        call_init("DX", w, &h); call_drain("DX-unreg", (ULONG_PTR)w2); call_drain("DX-reg", (ULONG_PTR)w);
        call_drain("DX-reg2", (ULONG_PTR)w); DestroyWindow(w); call_drain("DX-destroyed", (ULONG_PTR)w);
    } else if (!strcmp(m, "drainother")) {
        HANDLE th; g_ready = CreateEventW(NULL, TRUE, FALSE, NULL); g_go = CreateEventW(NULL, TRUE, FALSE, NULL);
        th = CreateThread(NULL, 0, other_thread, NULL, 0, NULL);
        WaitForSingleObject(g_ready, INFINITE);
        call_drain("DO-noiocp", (ULONG_PTR)g_other);
        call_init("DO", w, &h); call_drain("DO-other", (ULONG_PTR)g_other);
        SetEvent(g_go); WaitForSingleObject(th, INFINITE);
        call_drain("DO-deadthread", (ULONG_PTR)g_other);
    } else if (!strcmp(m, "nulloutbad")) {
        SetLastError(0xdeadbeef); h = pInit2(NULL, NULL); printf("NB ret=%p gle=%lu\n", h, GetLastError());
    } else if (!strcmp(m, "schedpre")) {
        sched("SP", w); sched("SP2", w); sched("SPnull", NULL); qs("SP");
    } else if (!strcmp(m, "sched")) {
        call_init("S", w, &h);
        sched("S1", w); drainport("S1", h); qs("S1"); waitport("S1w", h, 100); qs("S1w");
        pump(); drainport("S1p", h);
        sched("S2", w); sched("S2b", w); drainport("S2", h); qs("S2");
        call_drain("S2", (ULONG_PTR)w); drainport("S2d", h); qs("S2d"); pump(); qs("S2pump");
        PostMessageW(w, WM_USER, 1, 1); sched("S3-pending", w); drainport("S3", h); qs("S3"); pump();
    } else if (!strcmp(m, "rearm")) {
        call_init("RA", w, &h);
        call_drain("RA", (ULONG_PTR)w); drainport("RA0", h);
        PostMessageW(w, WM_USER, 1, 1); drainport("RA1", h); qs("RA1");
        call_drain("RA1", (ULONG_PTR)w); drainport("RA1d", h); qs("RA1d"); pump();
    } else if (!strncmp(m, "wait", 4)) {
        UINT kind = m[4] ? m[4] - '0' : 0; BOOL s = strstr(m, "s") != NULL, dr = strstr(m, "d") != NULL;
        call_init("WT", w, &h);
        if (dr) call_drain("WT", (ULONG_PTR)w);
        if (s) sched("WT", w);
        drainport("WT-pre", h);
        delayed(w, 80, kind);
        waitport("WT", h, 600); qs("WT");
        waitport("WT2", h, 100);
        pump();
        if (s) { sched("WT-again", w); delayed(w, 80, kind); waitport("WT3", h, 600); qs("WT3"); pump(); }
    } else if (!strcmp(m, "msgwait")) {
        DWORD r; call_init("MW", w, &h); sched("MW", w);
        delayed(w, 80, 0);
        r = MsgWaitForMultipleObjectsEx(0, NULL, 600, QS_ALLINPUT, 0);
        printf("MW msgwait=%lx\n", r); drainport("MW", h); qs("MW"); pump();
    }
    printf("done\n");
    return 0;
}

