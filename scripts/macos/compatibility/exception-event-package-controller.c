#define EXCEPTION_PACKAGE_CAPTURE
#include "exception-event-controller.c"
#include <winternl.h>

typedef NTSTATUS (NTAPI *QUERY_PACKAGE)(const UNICODE_STRING *, const UNICODE_STRING *, void *, ULONG *);
typedef NTSTATUS (NTAPI *ACTIVATE_PACKAGE)(HANDLE, const UNICODE_STRING *, const UNICODE_STRING *);
struct package_claim { ULONG flags, origin; };
typedef NTSTATUS (NTAPI *QUERY_CLAIMS)(HANDLE, PWSTR, SIZE_T *, PWSTR, SIZE_T *, GUID *,
                                      struct package_claim *, ULONG64 *);
static WCHAR package_name[] = L"StudioMDHR.20872A364DAA1_1.3.8.2_x64__tm1s6a95559gt";
static WCHAR app_name[] = L"App";
static UNICODE_STRING package_string, app_string;
static ACTIVATE_PACKAGE activate;
static QUERY_CLAIMS claims;

static BOOL package_claims(struct capture *capture, HANDLE process, BOOL expected)
{
    HANDLE token;
    WCHAR package[1024] = {0}, app[1024] = {0};
    SIZE_T package_bytes = sizeof(package), app_bytes = sizeof(app);
    GUID dynamic = {0};
    struct package_claim claim = {0};
    ULONG64 mask = 0;
    DWORD container = 0, bytes = 0;
    NTSTATUS status;
    BOOL ok;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return FALSE;
    status = claims(token, package, &package_bytes, app, &app_bytes, &dynamic, &claim, &mask);
    printf("PACKAGE_IDENTITY expected=%u status=%08lx mask=%llx flags=%lu origin=%lu\n",
           expected, (unsigned long)status, (unsigned long long)mask,
           (unsigned long)claim.flags, (unsigned long)claim.origin);
    if (expected)
        ok = !status && mask == 3 && claim.flags == 1 && claim.origin == 4 &&
            package_bytes <= sizeof(package) && app_bytes <= sizeof(app) &&
            wmemchr(package, 0, ARRAY_SIZE(package)) && wmemchr(app, 0, ARRAY_SIZE(app)) &&
            !wcscmp(package, package_name) && !wcscmp(app, app_name);
    else ok = status == (NTSTATUS)0xc0000225 && !mask;
    if (!GetTokenInformation(token, TokenIsAppContainer, &container, sizeof(container), &bytes) ||
        bytes != sizeof(container) || container) ok = FALSE;
    printf("PACKAGE_APPCONTAINER value=%lu valid=%u\n", (unsigned long)container, ok);
    close_owned(capture, token);
    return ok && !capture->failed;
}

static BOOL package_prepare(struct capture *capture, int argc, char **argv,
                            WCHAR *image, WCHAR *command, WCHAR *cwd)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    QUERY_PACKAGE query = (QUERY_PACKAGE)(ULONG_PTR)GetProcAddress(ntdll, "NtXodusQueryRegisteredPackage");
    WCHAR registered[4096] = {0}, *dos, *slash;
    ULONG bytes = sizeof(registered);
    NTSTATUS status;
    HANDLE file;
    int length;
    if (argc != 4 || strcmp(argv[1], "authorized-capture-only") || strlen(argv[2]) != 64 ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argv[3], -1,
                             capture->dump_prefix, ARRAY_SIZE(capture->dump_prefix))) return FALSE;
    activate = (ACTIVATE_PACKAGE)(ULONG_PTR)GetProcAddress(ntdll, "NtXodusActivateRegisteredPackage");
    claims = (QUERY_CLAIMS)(ULONG_PTR)GetProcAddress(ntdll, "RtlQueryPackageClaims");
    if (!query || !activate || !claims) return FALSE;
    package_string.Buffer = package_name;
    package_string.Length = wcslen(package_name) * sizeof(WCHAR);
    package_string.MaximumLength = package_string.Length + sizeof(WCHAR);
    app_string.Buffer = app_name;
    app_string.Length = wcslen(app_name) * sizeof(WCHAR);
    app_string.MaximumLength = app_string.Length + sizeof(WCHAR);
    if (!package_claims(capture, GetCurrentProcess(), FALSE)) return FALSE;
    status = query(&package_string, &app_string, registered, &bytes);
    printf("CATALOG_QUERY status=%08lx bytes=%lu\n", (unsigned long)status, (unsigned long)bytes);
    if (status || bytes > sizeof(registered) || bytes % sizeof(WCHAR) ||
        !wmemchr(registered, 0, ARRAY_SIZE(registered))) return FALSE;
    dos = !wcsncmp(registered, L"\\??\\", 4) ? registered + 4 : registered;
    slash = wcsrchr(dos, '\\');
    if (!slash || _wcsicmp(slash + 1, L"Cuphead.exe")) return FALSE;
    file = CreateFileW(dos, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    status = hash_file(file, argv[2]) ? 0 : (NTSTATUS)0xc0000428;
    close_owned(capture, file);
    if (status || capture->failed) return FALSE;
    wcscpy(image, dos);
    wcscpy(cwd, dos); *wcsrchr(cwd, '\\') = 0;
    length = _snwprintf(command, 8192, L"\"%ls\" -ServerName:Xodus.Cuphead.App", dos);
    if (length < 0 || length >= 8192) return FALSE;
    printf("REGISTERED_IMAGE=%ls\n", image);
    return TRUE;
}

static BOOL package_activate(struct capture *capture)
{
    NTSTATUS status;
    if (!package_claims(capture, capture->process.hProcess, FALSE)) return FALSE;
    status = activate(capture->process.hProcess, &package_string, &app_string);
    printf("ACTIVATION pass=0 status=%08lx pid=%lu\n",
           (unsigned long)status, (unsigned long)capture->process.dwProcessId);
    if (status || !package_claims(capture, capture->process.hProcess, TRUE) ||
        !package_claims(capture, GetCurrentProcess(), FALSE)) return FALSE;
    status = activate(capture->process.hProcess, &package_string, &app_string);
    printf("PACKAGE_RELABEL_DENIED status=%08lx\n", (unsigned long)status);
    return status == (NTSTATUS)0xc0000022;
}

struct windows { DWORD pid; unsigned total, visible; BOOL failed; };

static BOOL CALLBACK package_window(HWND window, LPARAM argument)
{
    struct windows *observation = (struct windows *)argument;
    DWORD pid = 0;
    RECT bounds;
    if (!GetWindowThreadProcessId(window, &pid)) { observation->failed = TRUE; return FALSE; }
    if (pid != observation->pid) return TRUE;
    ++observation->total;
    if (IsWindowVisible(window)) ++observation->visible;
    if (!GetWindowRect(window, &bounds)) { observation->failed = TRUE; return FALSE; }
    printf("ORIGINAL_APP_WINDOW visible=%u width=%ld height=%ld\n",
           IsWindowVisible(window), (long)(bounds.right - bounds.left), (long)(bounds.bottom - bounds.top));
    return TRUE;
}

static BOOL package_windows(struct capture *capture)
{
    struct windows observation = {capture->process.dwProcessId, 0, 0, FALSE};
    BOOL ok = EnumWindows(package_window, (LPARAM)&observation) && !observation.failed;
    printf("ORIGINAL_APP_WINDOWS total=%u visible=%u valid=%u\n",
           observation.total, observation.visible, ok);
    return ok;
}
