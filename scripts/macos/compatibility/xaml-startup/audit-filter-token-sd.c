/* Does a filtered (restricted) token inherit the source token's object SD? Run on native and Wine. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>

static void dump(const char *tag, HANDLE h)
{
    BYTE buf[4096];
    DWORD len = 0;
    LPSTR s = NULL;
    if (!GetKernelObjectSecurity(h, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                                 DACL_SECURITY_INFORMATION, buf, sizeof(buf), &len))
    { printf("TOKSD %s error=%lu\n", tag, GetLastError()); return; }
    if (!ConvertSecurityDescriptorToStringSecurityDescriptorA(buf, SDDL_REVISION_1, OWNER_SECURITY_INFORMATION |
            GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &s, NULL))
    { printf("TOKSD %s convert_error=%lu\n", tag, GetLastError()); return; }
    printf("TOKSD %s %s\n", tag, s);
    LocalFree(s);
}

int main(void)
{
    HANDLE self, dup = NULL, wr = NULL, plain = NULL;
    PSECURITY_DESCRIPTOR sd = NULL;
    SID_AND_ATTRIBUTES rs;
    PSID wsid = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &self)) { printf("OPEN_FAILED %lu\n", GetLastError()); return 1; }
    if (!DuplicateTokenEx(self, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &dup)) { printf("DUP_FAILED %lu\n", GetLastError()); return 1; }
    dump("dup-default", dup);
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA("D:(A;;GA;;;SY)(A;;RC;;;OW)(A;;0x8;;;BA)(A;;GA;;;WD)", SDDL_REVISION_1, &sd, NULL) ||
        !SetKernelObjectSecurity(dup, DACL_SECURITY_INFORMATION, sd))
    { printf("SETSD_FAILED %lu\n", GetLastError()); return 1; }
    dump("dup-custom", dup);
    ConvertStringSidToSidA("S-1-5-33", &wsid);
    rs.Sid = wsid; rs.Attributes = 0;
    if (CreateRestrictedToken(dup, WRITE_RESTRICTED, 0, NULL, 0, NULL, 1, &rs, &wr)) dump("filtered-write-restricted", wr);
    else printf("WR_FAILED %lu\n", GetLastError());
    if (CreateRestrictedToken(dup, 0, 0, NULL, 0, NULL, 0, NULL, &plain)) dump("filtered-plain", plain);
    else printf("PLAIN_FAILED %lu\n", GetLastError());
    printf("TOKSD_DONE\n");
    return 0;
}
