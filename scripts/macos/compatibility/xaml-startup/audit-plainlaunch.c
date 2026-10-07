/* Native control: plain CreateProcess of the packaged exe with no activation.
 * The caller decides the context; the probe reports its own and the child's
 * package identity (15700 = APPMODEL_ERROR_NO_PACKAGE). It samples the child's
 * top-level window count every second. If the child is still alive at the end,
 * it lists every top-level and child window the child owns, then terminates
 * only that exact child. */
#include <windows.h>
#include <stdio.h>

static DWORD target, owned;
static FILE *out;

static BOOL CALLBACK count_cb(HWND h, LPARAM l)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == target) owned++;
    return TRUE;
}

static BOOL CALLBACK child_cb(HWND h, LPARAM l)
{
    WCHAR cls[256]; DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    GetClassNameW(h, cls, 256);
    if (pid == target) fwprintf(out, L"    child hwnd=%p class=%ls visible=%d\n", h, cls, IsWindowVisible(h));
    return TRUE;
}

static BOOL CALLBACK top_cb(HWND h, LPARAM l)
{
    WCHAR cls[256], title[256]; DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    GetClassNameW(h, cls, 256);
    GetWindowTextW(h, title, 256);
    if (pid == target)
        fwprintf(out, L"  top hwnd=%p class=%ls visible=%d title=%ls\n", h, cls, IsWindowVisible(h), title);
    EnumChildWindows(h, child_cb, 0);
    return TRUE;
}

int wmain(int argc, WCHAR **argv)
{
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    ULONGLONG t0;
    DWORD wait = WAIT_TIMEOUT, code = 0, secs;

    if (argc < 4) return 2;
    out = _wfopen(argv[2], L"w");
    if (!out) return 3;
    secs = _wtoi(argv[3]);
    t0 = GetTickCount64();
    if (!CreateProcessW(argv[1], NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        fwprintf(out, L"CreateProcess failed err=%lu\n", GetLastError());
        fclose(out);
        return 1;
    }
    target = pi.dwProcessId;
    fwprintf(out, L"created pid=%lu\n", pi.dwProcessId);
    {
        typedef LONG (WINAPI *GPFN)(HANDLE, UINT32 *, WCHAR *);
        GPFN gpfn = (GPFN)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetPackageFullName");
        WCHAR name[256]; UINT32 len = 256; LONG r;
        Sleep(500);
        r = gpfn ? gpfn(pi.hProcess, &len, name) : -1;
        fwprintf(out, L"child GetPackageFullName=%ld %ls\n", r, r == 0 ? name : L"");
        len = 256;
        r = gpfn ? gpfn(GetCurrentProcess(), &len, name) : -1;
        fwprintf(out, L"self GetPackageFullName=%ld %ls\n", r, r == 0 ? name : L"");
    }
    fflush(out);
    for (DWORD s = 1; s <= secs; s++)
    {
        wait = WaitForSingleObject(pi.hProcess, 1000);
        if (wait == WAIT_OBJECT_0) break;
        owned = 0;
        EnumWindows(count_cb, 0);
        fwprintf(out, L"t=%lus child alive top-level-windows=%lu\n", s, owned);
        fflush(out);
    }
    if (wait == WAIT_OBJECT_0)
    {
        GetExitCodeProcess(pi.hProcess, &code);
        fwprintf(out, L"exited code=0x%08lx after %.1fs\n", code, (GetTickCount64() - t0) / 1000.0);
    }
    else
    {
        fwprintf(out, L"alive after %lus; windows:\n", secs);
        EnumWindows(top_cb, 0);
        if (TerminateProcess(pi.hProcess, 1))
            fwprintf(out, L"terminated exact child pid=%lu\n", pi.dwProcessId);
        else
            fwprintf(out, L"TerminateProcess(pid=%lu) failed err=%lu\n", pi.dwProcessId, GetLastError());
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    fclose(out);
    return 0;
}
