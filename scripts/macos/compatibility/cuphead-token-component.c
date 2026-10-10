#define CheckTokenMembershipEx imported_CheckTokenMembershipEx
#include <windows.h>
#undef CheckTokenMembershipEx

__declspec(dllexport) BOOL WINAPI CheckTokenMembershipEx(
    HANDLE token, PSID sid, DWORD flags, PBOOL member)
{
    DWORD saved_error = GetLastError(), size, appcontainer = 0;
    SECURITY_IMPERSONATION_LEVEL level;
    TOKEN_TYPE type;
    HANDLE owned = NULL;
    BOOL result = FALSE;

    *member = FALSE;
    if (flags & ~3u)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!token)
    {
        if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &owned))
        {
            if (!GetTokenInformation(owned, TokenImpersonationLevel, &level, sizeof(level), &size))
                goto done;
            if (level < SecurityImpersonation)
            {
                SetLastError(ERROR_BAD_IMPERSONATION_LEVEL);
                goto done;
            }
        }
        else
        {
            HANDLE process;
            if (GetLastError() != ERROR_NO_TOKEN) return FALSE;
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE, &process)) return FALSE;
            result = DuplicateTokenEx(process, TOKEN_QUERY, NULL, SecurityImpersonation,
                                      TokenImpersonation, &owned);
            CloseHandle(process);
            if (!result) return FALSE;
            result = FALSE;
        }
        token = owned;
    }
    if (!GetTokenInformation(token, TokenType, &type, sizeof(type), &size)) goto done;
    if (type == TokenPrimary)
    {
        SetLastError(ERROR_NO_IMPERSONATION_TOKEN);
        goto done;
    }
    if (!GetTokenInformation(token, TokenImpersonationLevel, &level, sizeof(level), &size)) goto done;
    if (level == SecurityAnonymous)
    {
        SetLastError(ERROR_BAD_IMPERSONATION_LEVEL);
        goto done;
    }
    if (!GetTokenInformation(token, TokenIsAppContainer, &appcontainer, sizeof(appcontainer), &size))
        goto done;
    if (appcontainer)
    {
        if (!(flags & 1u))
        {
            result = TRUE;
            goto done;
        }
        /* This component has no LPAC membership provider; do not invent membership. */
        if (flags & 2u)
        {
            SetLastError(ERROR_NOT_SUPPORTED);
            goto done;
        }
    }
    result = CheckTokenMembership(token, sid, member);
done:
    if (owned) CloseHandle(owned);
    if (result) SetLastError(saved_error);
    return result;
}
