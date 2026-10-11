#include <windows.h>
#include <winternl.h>
#include <stdio.h>

typedef NTSTATUS (NTAPI *CHECK_SANDBOX)(HANDLE, BOOLEAN *);
static unsigned checks, failures;
static void check(int condition, const char *label)
{
    checks++;
    if (!condition) {
        failures++;
        printf("FAIL %s\n", label);
    }
}

int main(void)
{
    CHECK_SANDBOX query = (CHECK_SANDBOX)(ULONG_PTR)GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "RtlCheckSandboxedToken");
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_MANDATORY_LABEL_AUTHORITY;
    TOKEN_MANDATORY_LABEL label;
    HANDLE own = NULL, low = NULL, denied = NULL, copy = NULL;
    BOOLEAN sandboxed;
    NTSTATUS status;
    PSID sid = NULL;
    if (!query || !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &own)) {
        puts("FAIL sandbox export or token setup");
        return 2;
    }
    sandboxed = 0xcc;
    status = query(own, &sandboxed);
    check(!status && !sandboxed, "ordinary own token not sandboxed");
    sandboxed = 0xcc;
    status = query(NULL, &sandboxed);
    check(!status && !sandboxed, "effective own token not sandboxed");
    sandboxed = 0xcc;
    status = query((HANDLE)(ULONG_PTR)0x1234, &sandboxed);
    check(status == (NTSTATUS)0xc0000008 && !sandboxed, "invalid handle and initialized output");
    if (!DuplicateTokenEx(own, TOKEN_DUPLICATE, NULL, SecurityImpersonation, TokenPrimary, &denied)) return 3;
    sandboxed = 0xcc;
    status = query(denied, &sandboxed);
    check(status == (NTSTATUS)0xc0000022 && !sandboxed, "query access required");
    if (!DuplicateTokenEx(own, TOKEN_QUERY | TOKEN_ADJUST_DEFAULT | TOKEN_DUPLICATE, NULL, SecurityImpersonation,
                          TokenPrimary, &low) ||
        !AllocateAndInitializeSid(&authority, 1, SECURITY_MANDATORY_LOW_RID,
                                  0, 0, 0, 0, 0, 0, 0, &sid)) return 3;
    label.Label.Sid = sid;
    label.Label.Attributes = SE_GROUP_INTEGRITY;
    if (!SetTokenInformation(low, TokenIntegrityLevel, &label,
                             sizeof(label) + GetLengthSid(sid))) return 3;
    sandboxed = 0xcc;
    status = query(low, &sandboxed);
    check(!status && sandboxed, "actual low-integrity token is sandboxed");
    if (!DuplicateTokenEx(low, TOKEN_QUERY, NULL, SecurityImpersonation, TokenPrimary, &copy)) return 3;
    sandboxed = 0xcc;
    status = query(copy, &sandboxed);
    check(!status && sandboxed, "low-integrity state survives token duplication");
    sandboxed = 0xcc;
    status = query(own, &sandboxed);
    check(!status && !sandboxed, "lowered duplicate leaves original token unchanged");
    ((SID *)sid)->SubAuthority[0] = SECURITY_MANDATORY_MEDIUM_RID;
    SetLastError(0);
    {
        BOOL raised = SetTokenInformation(low, TokenIntegrityLevel, &label,
                                          sizeof(label) + GetLengthSid(sid));
        printf("NATIVE_LABEL_RAISE result=%u error=%lu\n", raised, GetLastError());
        check(!raised && GetLastError() == ERROR_PRIVILEGE_NOT_HELD,
              "raising a lowered token requires relabel privilege");
    }
    check(SetThreadToken(NULL, low) == FALSE, "primary token cannot impersonate");
    CloseHandle(low);
    CloseHandle(copy);
    FreeSid(sid);
    CloseHandle(denied);
    CloseHandle(own);
    printf("RESULT sandbox_checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
