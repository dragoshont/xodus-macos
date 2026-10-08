/* Paired native/Wine probe for ntdll!RtlCheckSandboxedToken.
 * ABI (MrmCoreR!Microsoft::Resources::HasCapability call site): NTSTATUS (HANDLE token, BOOLEAN *sandboxed),
 * called as (NULL, &b) and treated as "not sandboxed" only for STATUS_SUCCESS with b == FALSE.
 * One case per process: "probe N". */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef LONG (WINAPI *fn_t)(HANDLE, BOOLEAN *);
typedef LONG (WINAPI *qit_t)(HANDLE, ULONG, void *, ULONG, ULONG *);

static void query_sweep(void);
static int appcontainer_child(void);

#define KEEP_IL 0xffffffffu

static HANDLE token_with_rid(DWORD rid, DWORD access)
{
    SID_IDENTIFIER_AUTHORITY auth = SECURITY_MANDATORY_LABEL_AUTHORITY;
    TOKEN_MANDATORY_LABEL label;
    HANDLE proc, dup = NULL;
    PSID sid;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &proc)) return NULL;
    if (!DuplicateTokenEx(proc, access | TOKEN_ADJUST_DEFAULT | TOKEN_QUERY | TOKEN_IMPERSONATE, NULL,
                          SecurityImpersonation, TokenImpersonation, &dup)) dup = NULL;
    CloseHandle(proc);
    if (!dup || rid == KEEP_IL) return dup;
    AllocateAndInitializeSid(&auth, 1, rid, 0, 0, 0, 0, 0, 0, 0, &sid);
    label.Label.Attributes = SE_GROUP_INTEGRITY;
    label.Label.Sid = sid;
    if (!SetTokenInformation(dup, TokenIntegrityLevel, &label, sizeof(label) + GetLengthSid(sid)))
        printf("[set IL %#lx failed %lu] ", rid, GetLastError());
    FreeSid(sid);
    return dup;
}

static void show(const char *what, fn_t fn, HANDLE token, BOOLEAN *out)
{
    LONG status;
    if (out) *out = 0xcc;
    printf("%s: ", what);
    fflush(stdout);
    status = fn(token, out);
    printf("status=0x%08lx sandboxed=%d\n", (unsigned long)status, out ? *out : -1);
}

int main(int argc, char **argv)
{
    fn_t fn = (fn_t)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlCheckSandboxedToken");
    BOOLEAN b;
    HANDLE t;
    int which = argc > 2 ? atoi(argv[2]) : -1;

    if (!fn) { printf("no export\n"); return 1; }
    if (argc > 1 && !strcmp(argv[1], "q")) { query_sweep(); return 0; }
    if (argc > 1 && !strcmp(argv[1], "ac")) return appcontainer_child();
    switch (which)
    {
    case 0: show("NULL token", fn, NULL, &b); break;
    case 1:
        OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t);
        show("process token TOKEN_QUERY", fn, t, &b); break;
    case 2:
        OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY_SOURCE, &t);
        show("process token no TOKEN_QUERY", fn, t, &b); break;
    case 3: show("invalid handle", fn, (HANDLE)(ULONG_PTR)0x1234, &b); break;
    case 4: show("pseudo current process", fn, GetCurrentProcess(), &b); break;
    case 5: show("low IL token", fn, token_with_rid(SECURITY_MANDATORY_LOW_RID, 0), &b); break;
    case 6: show("untrusted IL token", fn, token_with_rid(SECURITY_MANDATORY_UNTRUSTED_RID, 0), &b); break;
    case 7: show("medium IL dup token", fn, token_with_rid(KEEP_IL, 0), &b); break;
    case 8:
        t = token_with_rid(SECURITY_MANDATORY_LOW_RID, 0);
        printf("[impersonate=%d] ", SetThreadToken(NULL, t));
        show("NULL token while impersonating low IL", fn, NULL, &b); break;
    case 9: show("NULL out pointer", fn, NULL, NULL); break;
    case 10:
        t = token_with_rid(KEEP_IL, 0);
        printf("[impersonate=%d] ", SetThreadToken(NULL, t));
        show("NULL token while impersonating medium IL", fn, NULL, &b); break;
    case 11: show("current thread pseudo token", fn, (HANDLE)(LONG_PTR)-6, &b); break;
    case 12: show("current process pseudo token", fn, (HANDLE)(LONG_PTR)-4, &b); break;
    default: printf("usage: probe 0..12\n"); return 2;
    }
    return 0;
}

/* NtQueryInformationToken(TokenIsSandboxed = 47): the kernel rule RtlCheckSandboxedToken wraps. */
static void query_one(qit_t q, const char *what, HANDLE token, ULONG len, BOOL null_buf)
{
    BYTE buf[16];
    ULONG ret = 0xdead;
    LONG status;

    memset(buf, 0xcc, sizeof(buf));
    status = q(token, 47, null_buf ? NULL : buf, len, &ret);
    printf("q %s len=%lu%s: status=0x%08lx ret=%#lx value=%02x %02x %02x %02x %02x\n", what, len,
           null_buf ? " NULL buf" : "", (unsigned long)status, ret, buf[0], buf[1], buf[2], buf[3], buf[4]);
}

static void query_sweep(void)
{
    static const DWORD rids[] = { 0, 0x800, 0xfff, 0x1000, 0x1001, 0x17ff, 0x1fff, 0x2000, 0x2100 };
    qit_t q = (qit_t)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationToken");
    char what[64];
    unsigned int i;

    query_one(q, "thread effective", (HANDLE)(LONG_PTR)-6, 4, FALSE);
    query_one(q, "thread effective", (HANDLE)(LONG_PTR)-6, 8, FALSE);
    query_one(q, "thread effective", (HANDLE)(LONG_PTR)-6, 3, FALSE);
    query_one(q, "thread effective", (HANDLE)(LONG_PTR)-6, 1, FALSE);
    query_one(q, "thread effective", (HANDLE)(LONG_PTR)-6, 0, FALSE);
    query_one(q, "thread effective", (HANDLE)(LONG_PTR)-6, 0, TRUE);
    for (i = 0; i < ARRAYSIZE(rids); i++)
    {
        sprintf(what, "rid %#lx", rids[i]);
        query_one(q, what, token_with_rid(rids[i], 0), 4, FALSE);
    }
}

/* Re-run this probe inside an AppContainer process (derived SID; the exe must grant S-1-15-2-1 RX). */
static int appcontainer_child(void)
{
    char self[MAX_PATH];
    typedef HRESULT (WINAPI *create_t)(PCWSTR, PCWSTR, PCWSTR, void *, DWORD, PSID *);
    typedef HRESULT (WINAPI *delete_t)(PCWSTR);
    HMODULE userenv = LoadLibraryA("userenv.dll");
    create_t create = (create_t)GetProcAddress(userenv, "CreateAppContainerProfile");
    delete_t del = (delete_t)GetProcAddress(userenv, "DeleteAppContainerProfile");
    static const char *modes[] = { "probe 0", "q" };
    SECURITY_CAPABILITIES caps = { 0 };
    STARTUPINFOEXA si = { { sizeof(si) } };
    PROCESS_INFORMATION pi;
    SIZE_T size = 0;
    char cmd[MAX_PATH + 32];
    unsigned int i;
    HRESULT hr;

    GetModuleFileNameA(NULL, self, sizeof(self));
    del(L"Xodus.SandboxProbe");
    hr = create(L"Xodus.SandboxProbe", L"Xodus sandbox probe", L"Xodus sandbox probe", NULL, 0, &caps.AppContainerSid);
    printf("create profile hr=0x%08lx\n", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    InitializeProcThreadAttributeList(NULL, 1, 0, &size);
    si.lpAttributeList = HeapAlloc(GetProcessHeap(), 0, size);
    InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &size);
    UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, &caps,
                              sizeof(caps), NULL, NULL);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdOutput = si.StartupInfo.hStdError = GetStdHandle(STD_OUTPUT_HANDLE);
    for (i = 0; i < ARRAYSIZE(modes); i++)
    {
        fflush(stdout);
        sprintf(cmd, "\"%s\" %s", self, modes[i]);
        if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, EXTENDED_STARTUPINFO_PRESENT, NULL, NULL,
                            &si.StartupInfo, &pi))
        {
            printf("appcontainer CreateProcess failed %lu\n", GetLastError());
            continue;
        }
        WaitForSingleObject(pi.hProcess, 10000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    printf("delete profile hr=0x%08lx\n", (unsigned long)del(L"Xodus.SandboxProbe"));
    return 0;
}