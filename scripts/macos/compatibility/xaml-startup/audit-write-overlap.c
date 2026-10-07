/* Write-restricted AccessCheck with real FILE/KEY generic mappings where
 * READ_CONTROL/SYNCHRONIZE appear in read, write and execute. The DACL grants
 * Everyone full access; the only restricting SID is an unrelated service SID,
 * so the restricted pass grants nothing. Run unchanged on native and Wine. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>

struct map_case { const char *name; GENERIC_MAPPING map; const WCHAR *sddl; };

static BOOL check(HANDLE token, const struct map_case *mc, DWORD desired, BOOL *allowed, DWORD *granted)
{
    PSECURITY_DESCRIPTOR sd = NULL;
    union { ULONGLONG a; BYTE b[1024]; } privs;
    DWORD bytes = sizeof(privs);
    GENERIC_MAPPING map = mc->map;
    BOOL ret;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(mc->sddl, SDDL_REVISION_1, &sd, NULL))
        return FALSE;
    *allowed = FALSE; *granted = 0;
    ret = AccessCheck(sd, token, desired, &map, (PPRIVILEGE_SET)privs.b, &bytes, granted, allowed);
    LocalFree(sd);
    return ret;
}

int main(void)
{
    static const struct map_case cases[] = {
        { "FILE", { 0x120089, 0x120116, 0x1200a0, 0x1f01ff }, L"O:SYG:SYD:(A;;0x1f01ff;;;WD)" },
        { "KEY",  { 0x020019, 0x020006, 0x020019, 0x0f003f }, L"O:SYG:SYD:(A;;0xf003f;;;WD)" },
        /* SYNCHRONIZE in write+execute only, READ_CONTROL in write+read only */
        { "WX_RW", { 0x020001, 0x120002, 0x100004, 0x1f0007 }, L"O:SYG:SYD:(A;;0x1f0007;;;WD)" },
        /* bit 1 in write+execute only, bit 4 in write+read only */
        { "SPLIT", { 0x000005, 0x000007, 0x000003, 0x1f0007 }, L"O:SYG:SYD:(A;;0x1f0007;;;WD)" },
    };
    static const DWORD rights[] = {
        SYNCHRONIZE, READ_CONTROL, READ_CONTROL | SYNCHRONIZE, 0x00100020 /* FILE_EXECUTE|SYNC */,
        0x00120089 /* FILE_GENERIC_READ */, 0x00020019 /* KEY_READ */, GENERIC_READ, GENERIC_EXECUTE,
        1, 2, 4, 0x00120116 /* FILE_GENERIC_WRITE */, 0x00020006 /* KEY_WRITE */, GENERIC_WRITE, MAXIMUM_ALLOWED,
    };
    HANDLE src = NULL, imp = NULL, wr = NULL;
    SID_AND_ATTRIBUTES rs;
    PSID sid = NULL;
    unsigned i, j, rows = 0, failures = 0;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &src) ||
        !DuplicateTokenEx(src, TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_IMPERSONATE, NULL,
                          SecurityImpersonation, TokenImpersonation, &imp) ||
        !ConvertStringSidToSidW(L"S-1-5-80-1021139062-1866602279-1255292388-1008060685-2498416891", &sid))
    { printf("OVERLAP_SETUP_FAILED error=%lu\n", GetLastError()); return 1; }
    rs.Sid = sid; rs.Attributes = 0;
    if (!CreateRestrictedToken(imp, WRITE_RESTRICTED, 0, NULL, 0, NULL, 1, &rs, &wr))
    { printf("OVERLAP_CREATE_FAILED error=%lu\n", GetLastError()); return 1; }

    for (i = 0; i < ARRAYSIZE(cases); ++i)
        for (j = 0; j < ARRAYSIZE(rights); ++j)
        {
            BOOL allowed; DWORD granted;
            BOOL ok = check(wr, &cases[i], rights[j], &allowed, &granted);
            printf("OVERLAP map=%s desired=%08lx api=%u allowed=%u granted=%08lx error=%lu\n",
                   cases[i].name, rights[j], ok, allowed, granted, ok ? 0 : GetLastError());
            ++rows; if (!ok) ++failures;
        }
    printf("OVERLAP_MATRIX rows=%u api_failures=%u\n", rows, failures);
    CloseHandle(wr); CloseHandle(imp); CloseHandle(src); LocalFree(sid);
    return failures ? 1 : 0;
}
