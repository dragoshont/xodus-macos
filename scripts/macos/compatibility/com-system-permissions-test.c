#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <string.h>

HRESULT xodus_read_com_permissions(HKEY, int, PSECURITY_DESCRIPTOR *);

int main(void)
{
    const WCHAR *path = L"Software\\XodusComPermissionFixture20261006";
    const WCHAR *value_names[] = {L"DefaultLaunchPermission", L"DefaultAccessPermission",
                                 L"MachineLaunchRestriction", L"MachineAccessRestriction"};
    HKEY fixture, policy;
    DWORD disposition;
    BYTE invalid[24] = {0};
    SECURITY_DESCRIPTOR absolute;
    BYTE relative[256];
    DWORD bytes = sizeof(relative);
    PSECURITY_DESCRIPTOR result = (void *)(ULONG_PTR)0x1234;
    unsigned tests = 0, failures = 0;
    HRESULT hr;
    int i;
#define CHECK(expr) do { ++tests; if (!(expr)) { ++failures; printf("FAIL line=%d\n", __LINE__); } } while (0)
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, NULL, 0, KEY_ALL_ACCESS,
                        NULL, &fixture, &disposition) || disposition != REG_CREATED_NEW_KEY)
    {
        fputs("Unique fixture unavailable; refusing to modify an existing key\n", stderr);
        return 2;
    }
    CHECK(xodus_read_com_permissions(fixture, 4, &result) == E_INVALIDARG &&
          result == (void *)(ULONG_PTR)0x1234);
    CHECK(xodus_read_com_permissions(fixture, -1, &result) == E_INVALIDARG);
    CHECK(xodus_read_com_permissions(fixture, 0, NULL) == E_INVALIDARG);
    CHECK(xodus_read_com_permissions(fixture, 0, &result) == E_FAIL &&
          result == (void *)(ULONG_PTR)0x1234);
    if (RegCreateKeyExW(fixture, L"Software\\Microsoft\\Ole", 0, NULL, 0,
                        KEY_ALL_ACCESS, NULL, &policy, NULL)) return 3;
    CHECK(InitializeSecurityDescriptor(&absolute, SECURITY_DESCRIPTOR_REVISION));
    CHECK(SetSecurityDescriptorDacl(&absolute, TRUE, NULL, FALSE));
    CHECK(MakeSelfRelativeSD(&absolute, relative, &bytes));
    bytes = GetSecurityDescriptorLength(relative);
    for (i = 0; i < 4; ++i)
    {
        CHECK(RegSetValueExW(policy, value_names[i], 0, REG_BINARY, relative, bytes) == ERROR_SUCCESS);
        hr = xodus_read_com_permissions(fixture, i, &result);
        CHECK(hr == S_OK && IsValidSecurityDescriptor(result) &&
              GetSecurityDescriptorLength(result) == bytes && !memcmp(result, relative, bytes));
        if (hr == S_OK) LocalFree(result);
    }
    CHECK(RegSetValueExW(policy, value_names[0], 0, REG_BINARY, invalid, sizeof(invalid)) == ERROR_SUCCESS);
    CHECK(xodus_read_com_permissions(fixture, 0, &result) == HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    ((SECURITY_DESCRIPTOR_RELATIVE *)relative)->Owner = 0xfffffff0;
    CHECK(RegSetValueExW(policy, value_names[0], 0, REG_BINARY, relative, bytes) == ERROR_SUCCESS);
    CHECK(xodus_read_com_permissions(fixture, 0, &result) == HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    CHECK(RegSetValueExW(policy, value_names[0], 0, REG_SZ, relative, bytes) == ERROR_SUCCESS);
    CHECK(xodus_read_com_permissions(fixture, 0, &result) == HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    RegCloseKey(policy);
    RegDeleteTreeW(fixture, L"Software");
    RegCloseKey(fixture);
    RegDeleteKeyW(HKEY_CURRENT_USER, path);
    printf("REAL_COM_POLICY_TEST checks=%u failures=%u\n", tests, failures);
    return failures ? 1 : 0;
}
