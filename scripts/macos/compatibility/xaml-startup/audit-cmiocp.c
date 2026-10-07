/* Measures user32 ordinal 2669 (InitThreadCoreMessagingIocp2) and 2670
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
    s = pQO(h, 0, buf, sizeof(buf), &r);
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

int main(int argc, char **argv)
{
    HMODULE u = LoadLibraryA("user32.dll");
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    HWND w, w2;
    HANDLE h = NULL, h2 = NULL, th;
    pInit2 = (fnInit2)GetProcAddress(u, MAKEINTRESOURCEA(2669));
    pDrain2 = (fnDrain2)GetProcAddress(u, MAKEINTRESOURCEA(2670));
    pRemEx = (fnRemEx)GetProcAddress(nt, "NtRemoveIoCompletionEx");
    pQO = (fnQO)GetProcAddress(nt, "NtQueryObject");
    pQIoC = (fnQIoC)GetProcAddress(nt, "NtQueryIoCompletion");
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("init2=%p drain2=%p isgui=%d\n", pInit2, pDrain2, IsGUIThread(FALSE));
    if (!pInit2 || !pDrain2) return 2;

    if (argc > 1) {
        const char *m = argv[1];
        printf("mode %s\n", m);
        if (!strcmp(m, "nullhwnd")) call_init("N", NULL, NULL);
        else if (!strcmp(m, "badhwnd")) call_init("B", (HWND)(ULONG_PTR)0x123456, NULL);
        else if (!strcmp(m, "nullout")) { w = mkwin(); SetLastError(0xdeadbeef); h = pInit2(w, NULL); printf("O ret=%p gle=%lu\n", h, GetLastError()); }
        else if (!strcmp(m, "drainnull")) call_drain("D0", 0);
        else if (!strcmp(m, "drainbad")) call_drain("DB", 0x1234);
        else if (!strcmp(m, "drainhwnd")) { w = mkwin(); call_init("DH", w, &h); call_drain("DH", (ULONG_PTR)w); drainport("DH", h); }
        else if (!strcmp(m, "drainpre")) { call_drain("DP", (ULONG_PTR)GetCurrentThreadId()); }
        else if (!strcmp(m, "otherhwnd")) {
            g_ready = CreateEventW(NULL, TRUE, FALSE, NULL); g_go = CreateEventW(NULL, TRUE, FALSE, NULL);
            th = CreateThread(NULL, 0, other_thread, NULL, 0, NULL);
            WaitForSingleObject(g_ready, INFINITE);
            call_init("X", g_other, NULL);
            SetEvent(g_go); WaitForSingleObject(th, INFINITE);
        }
        else if (!strcmp(m, "closethenre")) {
            w = mkwin(); call_init("C1", w, &h); CloseHandle(h);
            call_init("C2", w, &h2);
            PostMessageW(w, WM_USER, 1, 1); drainport("C2", h2);
        }
        else if (!strcmp(m, "destroywin")) {
            w = mkwin(); call_init("W1", w, &h); drainport("W1", h);
            DestroyWindow(w); drainport("W1d", h);
            PostThreadMessageW(GetCurrentThreadId(), WM_USER, 2, 2); drainport("W1p", h); qs("W1p");
            w2 = mkwin(); call_init("W2", w2, &h2);
        }
        return 0;
    }

    w = mkwin();
    printf("hwnd=%p tid=%lu isgui=%d\n", w, GetCurrentThreadId(), IsGUIThread(FALSE));
    qs("pre");
    pump();
    call_init("A", w, &h);
    if (!h) return 3;
    drainport("A0", h);
    qs("A0");

    PostMessageW(w, WM_USER, 0x11, 0x22);
    drainport("A1-post", h); qs("A1");
    PostMessageW(w, WM_USER + 1, 0x33, 0x44);
    drainport("A2-post2", h); qs("A2");
    pump(); qs("A2-pumped");
    drainport("A2-afterpump", h);
    PostMessageW(w, WM_USER + 2, 0, 0);
    drainport("A3-post-after-pump", h);
    call_drain("A3", (ULONG_PTR)h);
    drainport("A3-afterdrain", h); qs("A3-afterdrain");
    PostMessageW(w, WM_USER + 3, 0, 0);
    drainport("A4-post-after-drain", h);
    pump();
    call_drain("A5", (ULONG_PTR)h);
    drainport("A5-afterdrain-empty", h);
    PostMessageW(w, WM_USER + 4, 0, 0);
    drainport("A5-post", h);
    call_drain("A5b", (ULONG_PTR)h);
    drainport("A5b", h); pump();

    PostThreadMessageW(GetCurrentThreadId(), WM_USER + 7, 7, 7);
    drainport("B-postthread", h); call_drain("B", (ULONG_PTR)h); pump();

    SetTimer(w, 9, 10, NULL); Sleep(60);
    drainport("T-timer", h); qs("T"); call_drain("T", (ULONG_PTR)h); pump();
    drainport("T-afterpump", h);
    KillTimer(w, 9);

    g_ready = CreateEventW(NULL, TRUE, FALSE, NULL); g_go = CreateEventW(NULL, TRUE, FALSE, NULL);
    th = CreateThread(NULL, 0, other_thread, w, 0, NULL);
    WaitForSingleObject(g_ready, INFINITE);
    SetEvent(g_go); WaitForSingleObject(th, INFINITE);
    drainport("S-crossthread", h); qs("S"); call_drain("S", (ULONG_PTR)h); pump();
    drainport("S-afterpump", h);

    call_init("R-again", w, &h2);
    printf("R same=%d\n", h == h2);
    if (h2 && h2 != h) { PostMessageW(w, WM_USER, 0, 0); drainport("R-old", h); drainport("R-new", h2); pump(); }
    w2 = mkwin();
    call_init("R-win2", w2, NULL);
    printf("done\n");
    return 0;
}
