#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <sddl.h>
#include <string.h>

typedef HRESULT (WINAPI *GET_POLICY)(int, PSECURITY_DESCRIPTOR *);

int main(int argc, char **argv)
{
    WCHAR path[MAX_PATH];
    HMODULE module;
    GET_POLICY get_policy;
    PSECURITY_DESCRIPTOR descriptor;
    UINT length = GetSystemDirectoryW(path, MAX_PATH);
    HRESULT result;
    unsigned int kind, failures = 0;
    if (!length || length >= MAX_PATH - 13) return 2;
    wcscat(path, L"\\combase.dll");
    module = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) return 2;
    get_policy = (GET_POLICY)(ULONG_PTR)GetProcAddress(module, "CoGetSystemSecurityPermissions");
    if (!get_policy) return 2;
    descriptor = (void *)(ULONG_PTR)0x1234;
    result = get_policy(4, &descriptor);
    if (result != E_INVALIDARG || descriptor != (void *)(ULONG_PTR)0x1234) failures++;
    if (get_policy(0, NULL) != E_INVALIDARG) failures++;
    for (kind = 0; kind < 4; kind++) {
        descriptor = (void *)(ULONG_PTR)0x1234;
        result = get_policy(kind, &descriptor);
        if (result == S_OK) {
            if (!descriptor || descriptor == (void *)(ULONG_PTR)0x1234 ||
                !IsValidSecurityDescriptor(descriptor)) failures++;
            else LocalFree(descriptor);
        } else if (result != E_FAIL || descriptor != (void *)(ULONG_PTR)0x1234) failures++;
        printf("COM_POLICY kind=%u hr=%08lx\n", kind, (unsigned long)result);
    }
    if (argc == 2 && !strcmp(argv[1], "--fixture")) {
        HKEY key = NULL;
        PSECURITY_DESCRIPTOR fixture = NULL;
        DWORD bytes = 0, existing_bytes = 0;
        LONG status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Ole", 0,
                                   KEY_QUERY_VALUE | KEY_SET_VALUE, &key);
        if (status) return 3;
        status = RegQueryValueExW(key, L"DefaultAccessPermission", NULL, NULL, NULL, &existing_bytes);
        if (status != ERROR_FILE_NOT_FOUND) {
            puts("REFUSING existing COM policy fixture overwrite");
            RegCloseKey(key);
            return 3;
        }
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"O:BAG:BAD:(A;;CC;;;BA)", SDDL_REVISION_1, &fixture, &bytes)) {
            RegCloseKey(key);
            return 3;
        }
        status = RegSetValueExW(key, L"DefaultAccessPermission", 0, REG_BINARY, fixture, bytes);
        if (!status) {
            descriptor = NULL;
            result = get_policy(1, &descriptor);
            if (result != S_OK || !descriptor || GetSecurityDescriptorLength(descriptor) != bytes ||
                memcmp(descriptor, fixture, bytes)) failures++;
            if (descriptor) LocalFree(descriptor);
            ((SECURITY_DESCRIPTOR_RELATIVE *)fixture)->Owner = bytes + 4;
            if (RegSetValueExW(key, L"DefaultAccessPermission", 0, REG_BINARY, fixture, bytes)) failures++;
            descriptor = (void *)(ULONG_PTR)0x1234;
            if (get_policy(1, &descriptor) != E_FAIL || descriptor != (void *)(ULONG_PTR)0x1234)
                failures++;
            if (RegDeleteValueW(key, L"DefaultAccessPermission")) failures++;
            descriptor = (void *)(ULONG_PTR)0x1234;
            if (get_policy(1, &descriptor) != E_FAIL || descriptor != (void *)(ULONG_PTR)0x1234)
                failures++;
        } else failures++;
        LocalFree(fixture);
        RegCloseKey(key);
        printf("RESULT configured_com_policy_checks=5 failures=%u\n", failures);
    } else if (argc != 1) return 3;
    printf("RESULT com_policy_checks=6 failures=%u\n", failures);
    FreeLibrary(module);
    return failures ? 1 : 0;
}
