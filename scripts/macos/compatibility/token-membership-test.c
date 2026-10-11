#include <windows.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    BOOL (WINAPI *check)(HANDLE, PSID, DWORD, PBOOL);
    BYTE buffer[SECURITY_MAX_SID_SIZE];
    DWORD size = sizeof(buffer);
    HANDLE primary, impersonation;
    DWORD flags[] = {0, 1, 2, 0xffffffff};
    unsigned i, j;
    HANDLE handles[3];
    HMODULE module = argc > 1 ? LoadLibraryA(argv[1]) : GetModuleHandleA("kernelbase.dll");
    unsigned failures = 0;
    if (!module)
    {
        printf("contract: module load failed error=%lu\n", GetLastError());
        return 2;
    }
    check = (void *)GetProcAddress(module, "CheckTokenMembershipEx");
    if (!check)
    {
        printf("contract: export resolution failed error=%lu\n", GetLastError());
        return 2;
    }
    if (!CreateWellKnownSid(WinWorldSid, NULL, buffer, &size) ||
        !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &primary) ||
        !DuplicateToken(primary, SecurityImpersonation, &impersonation))
    {
        printf("contract: token setup failed error=%lu\n", GetLastError());
        return 2;
    }
    handles[0] = NULL;
    handles[1] = primary;
    handles[2] = impersonation;
    for (j = 0; j < 3; j++)
        for (i = 0; i < sizeof(flags) / sizeof(flags[0]); i++)
        {
            BOOL member = FALSE, result;
            SetLastError(0x12345678);
            result = check(handles[j], buffer, flags[i], &member);
            {
                BOOL expected = i != 3 && j != 1;
                DWORD error = i == 3 ? ERROR_INVALID_PARAMETER :
                              j == 1 ? ERROR_NO_IMPERSONATION_TOKEN : 0x12345678;
                if (result != expected || member != expected || GetLastError() != error) failures++;
            }
            printf("membership: token=%u flags=%08lx result=%d member=%d error=%lu\n",
                   j, flags[i], result, member, GetLastError());
        }
    CloseHandle(impersonation);
    CloseHandle(primary);
    {
        SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
        PSID absent = NULL;
        BOOL member = TRUE, result;
        DWORD error;
        if (!AllocateAndInitializeSid(&authority, 1, 0x7fffffff, 0, 0, 0, 0, 0, 0, 0, &absent))
            return 2;
        SetLastError(0x12345678);
        result = check(NULL, absent, 0, &member);
        error = GetLastError();
        if (!result || member || error != 0x12345678) failures++;
        printf("absent-sid: result=%d member=%d error=%lu\n", result, member, error);
        FreeSid(absent);
        member = TRUE;
        SetLastError(0x12345678);
        result = check((HANDLE)(ULONG_PTR)0x1234, buffer, 0, &member);
        error = GetLastError();
        if (result || member || error != ERROR_INVALID_HANDLE) failures++;
        printf("invalid-token: result=%d member=%d error=%lu\n", result, member, error);
    }
    printf("contract: failures=%u\n", failures);
    return failures ? 1 : 0;
}
