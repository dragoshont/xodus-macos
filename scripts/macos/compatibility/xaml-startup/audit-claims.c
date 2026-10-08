/* Reports RtlQueryPackageClaims for the token of each given PID (or the
 * current process when no PID is given): package full name, application id,
 * PS_PKG_CLAIM flags/origin and the attributes-present mask. Read-only. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { ULONG Flags; ULONG Origin; } PS_PKG_CLAIM;
typedef LONG (NTAPI *query_fn)(HANDLE, WCHAR *, SIZE_T *, WCHAR *, SIZE_T *, GUID *, PS_PKG_CLAIM *, ULONG64 *);

static void report(query_fn q, HANDLE token, const char *label)
{
    WCHAR pkg[256] = {0}, app[130] = {0};
    SIZE_T pkg_len = ARRAYSIZE(pkg), app_len = ARRAYSIZE(app);
    PS_PKG_CLAIM claim = {0xffffffff, 0xffffffff};
    ULONG64 attrs = 0;
    GUID dyn = {0};
    LONG status = q(token, pkg, &pkg_len, app, &app_len, &dyn, &claim, &attrs);
    printf("%s status=%08lx pkg=%ls app=%ls flags=%#lx origin=%lu attributes=%#llx\n",
           label, (unsigned long)status, pkg, app, claim.Flags, claim.Origin, attrs);
}

int main(int argc, char **argv)
{
    query_fn q = (query_fn)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlQueryPackageClaims");
    int i;
    if (!q) { printf("RtlQueryPackageClaims missing\n"); return 1; }
    if (argc < 2) { report(q, (HANDLE)(LONG_PTR)-4, "self"); return 0; }
    for (i = 1; i < argc; i++)
    {
        DWORD pid = strtoul(argv[i], NULL, 10);
        HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid), token = NULL;
        char label[32];
        snprintf(label, sizeof(label), "pid=%lu", pid);
        if (!proc || !OpenProcessToken(proc, TOKEN_QUERY, &token))
            printf("%s open failed err=%lu\n", label, GetLastError());
        else
            report(q, token, label);
        if (token) CloseHandle(token);
        if (proc) CloseHandle(proc);
    }
    return 0;
}
