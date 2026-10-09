/* Explicit registered-package activation controller. No GUI or token fabrication.
 * Source of names/image is the real server catalog, not this executable.
 */
#define _WIN32_WINNT 0x0602
#include "token-query-provider.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef NTSTATUS (NTAPI *QUERY_PACKAGE)(const UNICODE_STRING *, const UNICODE_STRING *, void *, ULONG *);
typedef NTSTATUS (NTAPI *ACTIVATE_PACKAGE)(HANDLE, const UNICODE_STRING *, const UNICODE_STRING *);
static unsigned checks, failures;
static void check(int result, const char *label)
{
    ++checks;
    if (!result) { ++failures; printf("FAIL %s error=%lu\n", label, GetLastError()); }
}

static UNICODE_STRING us(WCHAR *s)
{
    UNICODE_STRING u;
    u.Length = (USHORT)(wcslen(s) * 2); u.MaximumLength = u.Length + 2; u.Buffer = s;
    return u;
}

static WCHAR *wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    WCHAR *out = n > 0 ? calloc(n, 2) : NULL;
    if (out) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, out, n);
    return out;
}

static NTSTATUS token_claims(XPC_QUERY_CLAIMS claims, HANDLE token,
                             const UNICODE_STRING *package, const UNICODE_STRING *app,
                             const char *label, int expected_identity)
{
    WCHAR pn[1024], an[1024];
    SIZE_T pb = sizeof(pn), ab = sizeof(an);
    XPC_PACKAGE_CLAIM claim;
    GUID dynamic;
    ULONG64 mask = ~((ULONG64)0);
    NTSTATUS status;
    memset(pn, 0xcc, sizeof(pn)); memset(an, 0xcc, sizeof(an));
    memset(&claim, 0xcc, sizeof(claim)); memset(&dynamic, 0xcc, sizeof(dynamic));
    status = claims(token, pn, &pb, an, &ab, &dynamic, &claim, &mask);
    printf("CLAIMS %s status=%08lx mask=%llx flags=%lu origin=%lu package_bytes=%llu app_bytes=%llu\n",
           label, (unsigned long)status, (unsigned long long)mask, claim.Flags, claim.Origin,
           (unsigned long long)pb, (unsigned long long)ab);
    if (expected_identity) {
        check(!status && mask == 3 && claim.Flags == 1 && claim.Origin == 4 &&
              !wcscmp(pn, package->Buffer) && !wcscmp(an, app->Buffer),
              "actual catalog identity equals token identity, DeveloperUnsigned not Store");
    } else check(status == (NTSTATUS)0xc0000225 && !mask,
                 "unpackaged launch remains genuinely NOT_FOUND");
    return status;
}

static int create_suspended(WCHAR *image, const WCHAR *server_name, PROCESS_INFORMATION *pi)
{
    STARTUPINFOW startup;
    WCHAR command[4096], cwd[4096], *slash;
    WCHAR *dos = !wcsncmp(image, L"\\??\\", 4) ? image + 4 : image;
    int command_length;
    memset(&startup, 0, sizeof(startup)); startup.cb = sizeof(startup);
    memset(pi, 0, sizeof(*pi));
    command_length = _snwprintf(command, 4096,
        server_name ? L"\"%ls\" -ServerName:%ls" : L"\"%ls\"", dos, server_name);
    if (command_length < 0 || command_length >= 4096)
    {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return FALSE;
    }
    wcscpy(cwd, dos); slash = wcsrchr(cwd, L'\\'); if (slash) *slash = 0;
    return CreateProcessW(dos, command, NULL, NULL, FALSE, CREATE_SUSPENDED,
                          NULL, slash ? cwd : NULL, &startup, pi);
}

int main(int argc, char **argv)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"), helper;
    QUERY_PACKAGE query = (QUERY_PACKAGE)(ULONG_PTR)GetProcAddress(ntdll, "NtXodusQueryRegisteredPackage");
    ACTIVATE_PACKAGE activate = (ACTIVATE_PACKAGE)(ULONG_PTR)GetProcAddress(ntdll, "NtXodusActivateRegisteredPackage");
    XPC_QUERY_INFORMATION info = (XPC_QUERY_INFORMATION)(ULONG_PTR)GetProcAddress(ntdll, "NtQueryInformationToken");
    XPC_QUERY_CLAIMS claims;
    WCHAR *pkg, *app, *server_name = NULL, image[4096], missing[] = L"Unregistered.Application";
    UNICODE_STRING package, application, bad_app = us(missing);
    ULONG bytes, appcontainer = 0, required = 0;
    NTSTATUS status;
    HANDLE own, child, duplicate;
    PROCESS_INFORMATION pi;
    unsigned pass;
    int run;
    if ((argc != 5 && argc != 6) || !query || !activate || !info) {
        puts("Usage: launch <package-full-name> <manifest-app-id> <Rtl-DLL> <test|run|query> [observed-server-name]");
        return 2;
    }
    run = !strcmp(argv[4], "run");
    pkg = wide(argv[1]); app = wide(argv[2]);
    if (!pkg || !app) return 2;
    if (argc == 6 && !(server_name = wide(argv[5]))) return 2;
    package = us(pkg); application = us(app);
    helper = LoadLibraryA(argv[3]);
    claims = helper ? (XPC_QUERY_CLAIMS)(ULONG_PTR)GetProcAddress(helper, "RtlQueryPackageClaims") : NULL;
    if (!claims || !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &own)) return 2;
    token_claims(claims, own, &package, &application, "launcher-unpackaged", 0);
    bytes = sizeof(image); memset(image, 0, sizeof(image));
    status = query(&package, &application, image, &bytes);
    printf("CATALOG_QUERY status=%08lx bytes=%lu\n", (unsigned long)status, bytes);
    if (status) return 3;
    wprintf(L"REGISTERED_IMAGE=%ls\n", image);
    if (!strcmp(argv[4], "query")) return 0;
    bytes = sizeof(image);
    check(query(&package, &bad_app, image, &bytes) == (NTSTATUS)0xc0000225,
          "unregistered app rejected");
    bytes = sizeof(image);
    status = query(&package, &application, image, &bytes);
    if (status) return 3;
    check(activate((HANDLE)(ULONG_PTR)0x1234, &package, &application) == (NTSTATUS)0xc0000008,
          "invalid process handle rejected");
    check(activate(GetCurrentProcess(), &package, &application) == (NTSTATUS)0xc0000022,
          "own running process cannot self-label");
    if (!run) {
        WCHAR wrong[4096], *dos;
        GetModuleFileNameW(NULL, wrong + 4, 4092);
        memcpy(wrong, L"\\??\\", 8);
        dos = wrong;
        check(create_suspended(dos, NULL, &pi), "actual mismatched-image child created");
        if (pi.hProcess) {
            status = activate(pi.hProcess, &package, &application);
            printf("MISMATCH_ACTIVATION=%08lx\n", (unsigned long)status);
            check(status == (NTSTATUS)0xc0000428, "mismatched image denied");
            TerminateProcess(pi.hProcess, 90); WaitForSingleObject(pi.hProcess, 10000);
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        }
    }
    for (pass = 0; pass < (run ? 1u : 2u); ++pass) {
        check(create_suspended(image, server_name, &pi), "registered original app created suspended");
        if (!pi.hProcess) return 3;
        if (!OpenProcessToken(pi.hProcess, TOKEN_QUERY | TOKEN_DUPLICATE, &child)) return 3;
        token_claims(claims, child, &package, &application, "before-explicit-activation", 0);
        status = activate(pi.hProcess, &package, &application);
        printf("ACTIVATION pass=%u status=%08lx pid=%lu\n", pass, (unsigned long)status, pi.dwProcessId);
        check(!status, "real suspended-image activation");
        if (status) {
            TerminateProcess(pi.hProcess, 91); WaitForSingleObject(pi.hProcess, 10000);
            return 4;
        }
        token_claims(claims, child, &package, &application, "activated-original-app", 1);
        check(activate(pi.hProcess, &package, &application) == (NTSTATUS)0xc0000022,
              "cannot relabel activated token");
        if (DuplicateTokenEx(child, TOKEN_QUERY, NULL, SecurityImpersonation, TokenPrimary, &duplicate)) {
            token_claims(claims, duplicate, &package, &application, "actual-token-duplicate", 1);
            CloseHandle(duplicate);
        } else check(0, "actual token duplication");
        status = info(child, TokenIsAppContainer, &appcontainer, sizeof(appcontainer), &required);
        check(!status && !appcontainer, "no fabricated AppContainer/capability grant");
        CloseHandle(child);
        if (run) {
            ULONGLONG start = GetTickCount64();
            DWORD resumed = ResumeThread(pi.hThread), waited;
            check(resumed != (DWORD)-1, "resume real registered original app");
            waited = resumed == (DWORD)-1 ? WAIT_FAILED : WaitForSingleObject(pi.hProcess, 11000);
            printf("ORIGINAL_APP_FIRST_WAIT=%lu ELAPSED_MS=%llu\n", waited,
                   (unsigned long long)(GetTickCount64() - start));
            if (waited == WAIT_TIMEOUT) {
                puts("ORIGINAL_APP_ALIVE_GT_10S=1"); fflush(stdout);
                waited = WaitForSingleObject(pi.hProcess, 49000);
            }
            if (waited == WAIT_FAILED) {
                check(0, "original app process wait");
                TerminateProcess(pi.hProcess, 94); WaitForSingleObject(pi.hProcess, 10000);
            } else if (waited == WAIT_TIMEOUT) {
                puts("APP_TIMEOUT=60000"); TerminateProcess(pi.hProcess, 92);
                WaitForSingleObject(pi.hProcess, 10000);
            }
            GetExitCodeProcess(pi.hProcess, &required);
            printf("ORIGINAL_APP_EXIT=%lu\n", required);
        } else {
            TerminateProcess(pi.hProcess, 93); WaitForSingleObject(pi.hProcess, 10000);
        }
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    }
    token_claims(claims, own, &package, &application, "launcher-still-unpackaged", 0);
    CloseHandle(own); FreeLibrary(helper); free(pkg); free(app); free(server_name);
    printf("RESULT activation_checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
