#define _WIN32_WINNT 0x0602
#include "debug-event-capture.h"
#include <winternl.h>

typedef NTSTATUS (NTAPI *QUERY_PACKAGE)(const UNICODE_STRING *, const UNICODE_STRING *, void *, ULONG *);
typedef NTSTATUS (NTAPI *ACTIVATE_PACKAGE)(HANDLE, const UNICODE_STRING *, const UNICODE_STRING *);

static UNICODE_STRING capture_us(WCHAR *text)
{
    UNICODE_STRING string;
    string.Buffer = text; string.Length = (USHORT)(wcslen(text) * sizeof(WCHAR));
    string.MaximumLength = string.Length + sizeof(WCHAR);
    return string;
}

int main(int argc, char **argv)
{
    WCHAR package_name[] = L"StudioMDHR.20872A364DAA1_1.3.8.2_x64__tm1s6a95559gt";
    WCHAR app_name[] = L"App", image[4096], command[8192], cwd[4096], *dos, *slash;
    UNICODE_STRING package = capture_us(package_name), application = capture_us(app_name);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    QUERY_PACKAGE query = (QUERY_PACKAGE)(ULONG_PTR)GetProcAddress(ntdll, "NtXodusQueryRegisteredPackage");
    ACTIVATE_PACKAGE activate = (ACTIVATE_PACKAGE)(ULONG_PTR)GetProcAddress(ntdll, "NtXodusActivateRegisteredPackage");
    struct debug_capture capture;
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    NTSTATUS status;
    ULONG bytes = sizeof(image);
    HANDLE file;
    unsigned i;
    static const DWORD rvas[CAPTURE_SITES] = {0x8380, 0x32c0, 0x32e0, 0x3d20c, 0x3b50c};
    static const unsigned kinds[CAPTURE_SITES] = {1, 2, 2, 3, 3};
    static const char mrt_pin[] = "6eb3c9c626c2514b39301969f318d1b7c839d3efe1a86a90783ec6f78f19e3ec";
    static const char twin_pin[] = "0d3e56c5068633f975bcab528267789f520f82a6d6a50f1330104e7597289b43";
    setvbuf(stdout, NULL, _IONBF, 0);
    /* Explicit future admission token plus independently supplied executable pin. */
    if (argc != 3 || strcmp(argv[1], "authorized-capture-only") ||
        strlen(argv[2]) != 64 || !query || !activate) return 2;
    memset(&capture, 0, sizeof(capture));
    memset(&startup, 0, sizeof(startup)); startup.cb = sizeof(startup);
    memset(image, 0, sizeof(image));
    status = query(&package, &application, image, &bytes);
    printf("CATALOG_QUERY status=%08lx bytes=%lu\n", (unsigned long)status, (unsigned long)bytes);
    if (status || bytes > sizeof(image) || !wmemchr(image, 0, ARRAY_SIZE(image))) return 3;
    dos = !wcsncmp(image, L"\\??\\", 4) ? image + 4 : image;
    slash = wcsrchr(dos, '\\');
    if (!slash || _wcsicmp(slash + 1, L"Cuphead.exe")) return 3;
    file = CreateFileW(dos, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return 3;
    if (!capture_hash(file, argv[2])) { CloseHandle(file); return 3; }
    CloseHandle(file);
    wcscpy(cwd, dos); *wcsrchr(cwd, '\\') = 0;
    if (_snwprintf(command, ARRAY_SIZE(command), L"\"%ls\" -ServerName:Xodus.Cuphead.App", dos) < 0) return 3;
    for (i = 0; i < CAPTURE_SITES; ++i)
    {
        capture.sites[i].module = i < 3 ? L"mrt100_app.dll" : L"twinapi.appcore.dll";
        capture.sites[i].sha256 = i < 3 ? mrt_pin : twin_pin;
        capture.sites[i].rva = rvas[i]; capture.sites[i].kind = kinds[i];
    }
    if (!CreateProcessW(dos, command, NULL, NULL, FALSE,
                        CREATE_SUSPENDED | DEBUG_ONLY_THIS_PROCESS, NULL, cwd, &startup, &process)) return 4;
    capture.process = process.hProcess; capture.pid = process.dwProcessId;
    printf("DEBUG_OWNED_PROCESS pid=%lu\n", (unsigned long)capture.pid);
    status = activate(process.hProcess, &package, &application);
    printf("ACTIVATION pass=0 status=%08lx pid=%lu\n", (unsigned long)status, (unsigned long)capture.pid);
    if (status || ResumeThread(process.hThread) == (DWORD)-1)
    {
        fprintf(stderr, "DEBUG_ACTIVATION_OR_RESUME_FAILED status=%08lx error=%lu\n",
                (unsigned long)status, (unsigned long)GetLastError());
        capture_abort_initial(&process);
        return 5;
    }
    CloseHandle(process.hThread);
    capture_loop(&capture, 60000, 20000000);
    CloseHandle(process.hProcess);
    if (capture.exited) printf("ORIGINAL_APP_EXIT=%lu\n", (unsigned long)capture.exit_code);
    else puts("ORIGINAL_APP_EXIT_UNOBSERVED=1");
    return capture.failed ? 6 : 0;
}
