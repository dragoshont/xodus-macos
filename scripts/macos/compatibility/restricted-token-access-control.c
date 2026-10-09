#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <stdio.h>

static unsigned checks, failures;

static void check(BOOL condition, const char *name)
{
    ++checks;
    if (!condition) { ++failures; printf("FAIL %s error=%lu\n", name, GetLastError()); }
}

static void query_shape(HANDLE token, const char *label)
{
    DWORD bytes = 0, value = 0;
    BOOL ok;
    GetTokenInformation(token, TokenRestrictedSids, NULL, 0, &bytes);
    printf("RESTRICTED_QUERY_SHAPE %s required=%lu error=%lu\n", label, bytes, GetLastError());
    ok = GetTokenInformation(token, TokenHasRestrictions, &value, sizeof(value), &bytes);
    printf("HAS_RESTRICTIONS %s ok=%u value=%lu bytes=%lu\n", label, ok, value, bytes);
    value = 0;
    ok = GetTokenInformation(token, TokenIsRestricted, &value, sizeof(value), &bytes);
    printf("IS_RESTRICTED %s ok=%u value=%lu bytes=%lu\n", label, ok, value, bytes);
}

static void access_case(HANDLE token, const WCHAR *sddl, DWORD desired, BOOL expected, const char *label)
{
    PSECURITY_DESCRIPTOR relative = NULL, absolute;
    DWORD relative_bytes, absolute_bytes = 0, dacl_bytes = 0, sacl_bytes = 0;
    DWORD owner_bytes = 0, group_bytes = 0, granted = 0;
    PACL dacl = NULL, sacl = NULL;
    PSID owner = NULL, group = NULL;
    union { ULONGLONG alignment; BYTE bytes[1024]; } privileges;
    DWORD privilege_bytes = sizeof(privileges);
    GENERIC_MAPPING mapping = {1, 2 | READ_CONTROL, 4, 7};
    BOOL allowed = FALSE, result;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl, SDDL_REVISION_1, &relative, &relative_bytes))
    {
        check(FALSE, "synthetic-descriptor");
        return;
    }
    MakeAbsoluteSD(relative, NULL, &absolute_bytes, NULL, &dacl_bytes,
                   NULL, &sacl_bytes, NULL, &owner_bytes, NULL, &group_bytes);
    absolute = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, absolute_bytes);
    if (dacl_bytes) dacl = HeapAlloc(GetProcessHeap(), 0, dacl_bytes);
    if (sacl_bytes) sacl = HeapAlloc(GetProcessHeap(), 0, sacl_bytes);
    if (owner_bytes) owner = HeapAlloc(GetProcessHeap(), 0, owner_bytes);
    if (group_bytes) group = HeapAlloc(GetProcessHeap(), 0, group_bytes);
    if (!absolute || (dacl_bytes && !dacl) || (sacl_bytes && !sacl) ||
        (owner_bytes && !owner) || (group_bytes && !group) ||
        !MakeAbsoluteSD(relative, absolute, &absolute_bytes, dacl, &dacl_bytes,
                        sacl, &sacl_bytes, owner, &owner_bytes, group, &group_bytes))
    {
        check(FALSE, "synthetic-descriptor-absolute");
        goto done;
    }
    result = AccessCheck(absolute, token, desired, &mapping, (PPRIVILEGE_SET)privileges.bytes,
                         &privilege_bytes, &granted, &allowed);
    check(result && allowed == expected &&
          (!allowed || ((desired & MAXIMUM_ALLOWED) ? granted != 0 : (granted & desired) == desired)), label);
    printf("RESTRICTED_ACCESS %s api_success=%u allowed=%u expected=%u\n",
           label, result, allowed, expected);
done:
    if (group) HeapFree(GetProcessHeap(), 0, group);
    if (owner) HeapFree(GetProcessHeap(), 0, owner);
    if (sacl) HeapFree(GetProcessHeap(), 0, sacl);
    if (dacl) HeapFree(GetProcessHeap(), 0, dacl);
    if (absolute) HeapFree(GetProcessHeap(), 0, absolute);
    LocalFree(relative);
}

int main(void)
{
    HANDLE source = NULL, normal = NULL, restricted = NULL, duplicate = NULL;
    HANDLE write_restricted = NULL, empty_filter = NULL, twice_filtered = NULL, deny_only = NULL;
    PSID restricting_sid = NULL, world_sid = NULL;
    SID_AND_ATTRIBUTES restrict_to;
    DWORD bytes = 0, restrictions = 0, i;
    TOKEN_GROUPS *groups = NULL;
    BOOL found = FALSE;
    DWORD attributes = 0;
    const WCHAR *service_sid =
        L"S-1-5-80-1021139062-1866602279-1255292388-1008060685-2498416891";
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &source) ||
        !DuplicateTokenEx(source, TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_IMPERSONATE,
                          NULL, SecurityImpersonation, TokenImpersonation, &normal) ||
        !ConvertStringSidToSidW(service_sid, &restricting_sid))
    {
        fprintf(stderr, "Owned restricted-token control setup failed error=%lu\n", GetLastError());
        failures++;
        goto done;
    }
    query_shape(normal, "normal");
    if (CreateRestrictedToken(normal, 0, 0, NULL, 0, NULL, 0, NULL, &empty_filter))
        query_shape(empty_filter, "empty-filter");
    else
        check(FALSE, "empty-filter-creation");
    restrict_to.Sid = restricting_sid;
    restrict_to.Attributes = 0;
    if (!CreateRestrictedToken(normal, 0, 0, NULL, 0, NULL, 1, &restrict_to, &restricted))
    {
        fprintf(stderr, "Actual restricted-SID construction unavailable error=%lu\n", GetLastError());
        failures++;
        goto done;
    }
    GetTokenInformation(restricted, TokenRestrictedSids, NULL, 0, &bytes);
    groups = bytes ? HeapAlloc(GetProcessHeap(), 0, bytes) : NULL;
    if (!groups || !GetTokenInformation(restricted, TokenRestrictedSids, groups, bytes, &bytes))
    {
        check(FALSE, "actual-restricted-sid-query");
        goto done;
    }
    for (i = 0; i < groups->GroupCount; ++i)
        if (EqualSid(groups->Groups[i].Sid, restricting_sid))
        {
            found = TRUE;
            attributes = groups->Groups[i].Attributes;
        }
    check(found, "actual-restricted-list-preserved");
    printf("RESTRICTED_LIST count=%lu service_sid_found=%u attributes=%lx\n",
           groups->GroupCount, found, attributes);
    check(GetTokenInformation(restricted, TokenHasRestrictions, &restrictions,
                             sizeof(restrictions), &bytes) && restrictions,
          "actual-token-restrictions-reported");
    access_case(normal, L"O:SYG:SYD:(A;;0x1;;;WD)", 1, TRUE, "normal-world-allowed");
    access_case(restricted, L"O:SYG:SYD:(A;;0x1;;;WD)", 1, FALSE, "normal-pass-restricted-fail-denied");
    access_case(restricted,
        L"O:SYG:SYD:(A;;0x1;;;S-1-5-80-1021139062-1866602279-1255292388-1008060685-2498416891)",
        1, FALSE, "restricted-pass-normal-fail-denied");
    access_case(restricted,
        L"O:SYG:SYD:(A;;0x1;;;WD)(A;;0x1;;;S-1-5-80-1021139062-1866602279-1255292388-1008060685-2498416891)",
        1, TRUE, "both-pass-allowed");
    access_case(restricted, L"O:SYG:SYD:(A;;0x1;;;WD)",
                MAXIMUM_ALLOWED, FALSE, "maximum-allowed-still-restricted");
    access_case(restricted,
        L"O:SYG:SYD:(A;;0x1;;;WD)(A;;0x1;;;S-1-5-80-1021139062-1866602279-1255292388-1008060685-2498416891)",
        MAXIMUM_ALLOWED, TRUE, "maximum-allowed-both-pass");
    if (!DuplicateTokenEx(restricted, TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_IMPERSONATE,
                          NULL, SecurityImpersonation, TokenImpersonation, &duplicate))
        check(FALSE, "duplicate-restricted-token");
    else
    {
        access_case(duplicate, L"O:SYG:SYD:(A;;0x1;;;WD)", 1, FALSE, "duplicate-preserves-restricted-denial");
        access_case(duplicate,
            L"O:SYG:SYD:(A;;0x1;;;WD)(A;;0x1;;;S-1-5-80-1021139062-1866602279-1255292388-1008060685-2498416891)",
            1, TRUE, "duplicate-preserves-both-pass");
    }
    if (!CreateRestrictedToken(normal, WRITE_RESTRICTED, 0, NULL, 0, NULL,
                               1, &restrict_to, &write_restricted))
        check(FALSE, "write-restricted-token-creation");
    else
    {
        access_case(write_restricted, L"O:SYG:SYD:(A;;0x1;;;WD)", 1, TRUE, "write-restricted-read-normal-only");
        access_case(write_restricted, L"O:SYG:SYD:(A;;0x2;;;WD)", 2, FALSE, "write-restricted-write-requires-both");
        access_case(write_restricted,
            L"O:SYG:SYD:(A;;0x2;;;WD)(A;;0x2;;;S-1-5-80-1021139062-1866602279-1255292388-1008060685-2498416891)",
            2, TRUE, "write-restricted-write-both-pass");
        access_case(write_restricted,
            L"O:SYG:SYD:(A;;0x3;;;WD)(A;;0x2;;;S-1-5-80-1021139062-1866602279-1255292388-1008060685-2498416891)",
            3, TRUE, "write-restricted-mixed-both-pass");
        access_case(write_restricted, L"O:SYG:SYD:(A;;0x3;;;WD)",
                    3, FALSE, "write-restricted-mixed-must-deny");
        access_case(write_restricted, L"O:SYG:SYD:(A;;0x40000;;;WD)",
                    WRITE_DAC, FALSE, "write-restricted-write-dac-denied");
        access_case(write_restricted, L"O:SYG:SYD:(A;;0x80000;;;WD)",
                    WRITE_OWNER, FALSE, "write-restricted-write-owner-denied");
        access_case(write_restricted, L"O:SYG:SYD:(A;;0x10000;;;WD)",
                    DELETE, FALSE, "write-restricted-delete-denied");
        access_case(write_restricted, L"O:SYG:SYD:(A;;0x20000;;;WD)",
                    READ_CONTROL, TRUE, "write-restricted-read-control-allowed");
    }
    if (ConvertStringSidToSidW(L"S-1-1-0", &world_sid))
    {
        restrict_to.Sid = world_sid;
        if (CreateRestrictedToken(normal, 0, 1, &restrict_to, 0, NULL, 0, NULL, &deny_only))
        {
            access_case(deny_only, L"O:SYG:SYD:(A;;0x1;;;WD)",
                        1, FALSE, "deny-only-world-cannot-grant");
            access_case(deny_only, L"O:SYG:SYD:(D;;0x1;;;WD)(A;;0x1;;;BU)",
                        1, FALSE, "deny-only-world-still-denies");
        }
        else
            check(FALSE, "deny-only-filter-creation");
        if (CreateRestrictedToken(restricted, 0, 0, NULL, 0, NULL, 1, &restrict_to, &twice_filtered))
        {
            query_shape(twice_filtered, "twice-filtered-disjoint");
            check(FALSE, "disjoint-filter-must-reject");
        }
        else
            check(GetLastError() == ERROR_INVALID_PARAMETER, "disjoint-filter-rejected");
    }
    else
        check(FALSE, "world-sid-conversion");
done:
    if (groups) HeapFree(GetProcessHeap(), 0, groups);
    if (restricted) CloseHandle(restricted);
    if (duplicate) CloseHandle(duplicate);
    if (write_restricted) CloseHandle(write_restricted);
    if (empty_filter) CloseHandle(empty_filter);
    if (twice_filtered) CloseHandle(twice_filtered);
    if (deny_only) CloseHandle(deny_only);
    if (world_sid) LocalFree(world_sid);
    if (restricting_sid) LocalFree(restricting_sid);
    if (normal) CloseHandle(normal);
    if (source) CloseHandle(source);
    printf("RESTRICTED_TOKEN_ACCESS checks=%u failures=%u no_service_or_policy_change=1\n",
           checks, failures);
    return failures ? 1 : 0;
}
