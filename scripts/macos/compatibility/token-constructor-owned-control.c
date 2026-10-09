#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

typedef NTSTATUS (WINAPI *create_token_fn)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES,
    TOKEN_TYPE, PLUID, PLARGE_INTEGER, PTOKEN_USER, PTOKEN_GROUPS, PTOKEN_PRIVILEGES,
    PTOKEN_OWNER, PTOKEN_PRIMARY_GROUP, PTOKEN_DEFAULT_DACL, PTOKEN_SOURCE);

static unsigned checks, failures;

static void check(BOOL condition)
{
    ++checks;
    if (!condition) ++failures;
}

int main(void)
{
    HANDLE source = NULL, created = NULL, duplicate = NULL;
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    create_token_fn create_token;
    union { FARPROC proc; create_token_fn typed; } entry;
    SID_IDENTIFIER_AUTHORITY world_authority = {SECURITY_WORLD_SID_AUTHORITY};
    SID_IDENTIFIER_AUTHORITY nt_authority = {SECURITY_NT_AUTHORITY};
    TOKEN_USER *user = NULL;
    TOKEN_OWNER owner;
    TOKEN_PRIMARY_GROUP primary;
    TOKEN_DEFAULT_DACL dacl = {NULL};
    TOKEN_PRIVILEGES privileges = {0};
    TOKEN_SOURCE origin = {{'C','o','n','t','r','o','l',0}, {0,0}};
    TOKEN_STATISTICS statistics;
    LUID authentication;
    LARGE_INTEGER expiry;
    OBJECT_ATTRIBUTES attributes;
    union { ULONGLONG alignment; BYTE bytes[offsetof(TOKEN_GROUPS, Groups[2])]; } groups_storage;
    TOKEN_GROUPS *groups = (TOKEN_GROUPS *)groups_storage.bytes;
    PSID world = NULL, users = NULL;
    DWORD bytes = 0;
    NTSTATUS status;
    union { ULONGLONG alignment; BYTE bytes[256]; } owner_buffer, primary_buffer;

    if (!GetProcAddress(ntdll, "wine_get_version"))
    {
        fprintf(stderr, "Refusing constructor fixture outside the owned Wine runtime\n");
        return 2;
    }
    entry.proc = GetProcAddress(ntdll, "NtCreateToken");
    create_token = entry.typed;
    if (!create_token || !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &source))
        goto setup_failed;
    GetTokenInformation(source, TokenUser, NULL, 0, &bytes);
    user = HeapAlloc(GetProcessHeap(), 0, bytes);
    if (!user || !GetTokenInformation(source, TokenUser, user, bytes, &bytes) ||
        !AllocateAndInitializeSid(&world_authority,
                                  1, SECURITY_WORLD_RID, 0,0,0,0,0,0,0, &world) ||
        !AllocateAndInitializeSid(&nt_authority,
                                  2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_USERS,
                                  0,0,0,0,0,0, &users) ||
        !AllocateLocallyUniqueId(&authentication))
        goto setup_failed;
    groups->GroupCount = 2;
    groups->Groups[0].Sid = world;
    groups->Groups[1].Sid = users;
    groups->Groups[0].Attributes = groups->Groups[1].Attributes =
        SE_GROUP_MANDATORY | SE_GROUP_ENABLED | SE_GROUP_ENABLED_BY_DEFAULT;
    owner.Owner = user->User.Sid;
    primary.PrimaryGroup = user->User.Sid;
    expiry.QuadPart = -1;
    InitializeObjectAttributes(&attributes, NULL, 0, NULL, NULL);
    status = create_token(&created, TOKEN_ALL_ACCESS, &attributes, TokenPrimary,
                          &authentication, &expiry, user, groups, &privileges,
                          &owner, &primary, &dacl, &origin);
    if (status)
    {
        fprintf(stderr, "Owned actual-token construction failed status=%08lx\n", status);
        failures++;
        goto done;
    }
    check(GetTokenInformation(created, TokenOwner, owner_buffer.bytes, sizeof(owner_buffer), &bytes) &&
          EqualSid(((TOKEN_OWNER *)owner_buffer.bytes)->Owner, user->User.Sid));
    check(GetTokenInformation(created, TokenPrimaryGroup, primary_buffer.bytes, sizeof(primary_buffer), &bytes) &&
          EqualSid(((TOKEN_PRIMARY_GROUP *)primary_buffer.bytes)->PrimaryGroup, user->User.Sid));
    check(GetTokenInformation(created, TokenStatistics, &statistics, sizeof(statistics), &bytes) &&
          statistics.AuthenticationId.LowPart == authentication.LowPart &&
          statistics.AuthenticationId.HighPart == authentication.HighPart);
    check(DuplicateTokenEx(created, TOKEN_QUERY, NULL, SecurityIdentification, TokenImpersonation, &duplicate));
    if (duplicate)
    {
        check(GetTokenInformation(duplicate, TokenOwner, owner_buffer.bytes, sizeof(owner_buffer), &bytes) &&
              EqualSid(((TOKEN_OWNER *)owner_buffer.bytes)->Owner, user->User.Sid));
        check(GetTokenInformation(duplicate, TokenStatistics, &statistics, sizeof(statistics), &bytes) &&
              statistics.AuthenticationId.LowPart == authentication.LowPart &&
              statistics.AuthenticationId.HighPart == authentication.HighPart);
    }
    goto done;
setup_failed:
    fprintf(stderr, "Owned token fixture setup failed error=%lu\n", GetLastError());
    failures++;
done:
    if (duplicate) CloseHandle(duplicate);
    if (created) CloseHandle(created);
    if (source) CloseHandle(source);
    if (world) FreeSid(world);
    if (users) FreeSid(users);
    if (user) HeapFree(GetProcessHeap(), 0, user);
    printf("OWNED_TOKEN_CONSTRUCTOR checks=%u failures=%u service_identity_created=0\n", checks, failures);
    return failures ? 1 : 0;
}
