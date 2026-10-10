#include <windows.h>
#include <stdio.h>
#include <wchar.h>

typedef HRESULT (WINAPI *OPEN_STORAGE)(const WCHAR *, const WCHAR *, const WCHAR *, REGSAM, HKEY *);
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
    WCHAR module_path[MAX_PATH];
    UINT path_length = GetSystemDirectoryW(module_path, MAX_PATH);
    HMODULE module;
    OPEN_STORAGE open_storage;
    HKEY key;
    HKEY fixture = NULL, child = NULL, settings = NULL;
    static const WCHAR fixture_path[] =
        L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\"
        L"CurrentVersion\\AppContainer\\Storage\\Xodus.ProfApiStorageTest";
    static const WCHAR child_path[] =
        L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\"
        L"CurrentVersion\\AppContainer\\Storage\\Xodus.ProfApiStorageTest\\Children\\Child";
    static const WCHAR settings_path[] =
        L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\"
        L"CurrentVersion\\AppContainer\\Storage\\Xodus.ProfApiStorageTest\\Children\\Child\\Settings";
    DWORD disposition = 0;
    LONG status;
    HRESULT result;
    if (!path_length || path_length >= MAX_PATH - 13) return 2;
    wcscat(module_path, L"\\profapi.dll");
    module = LoadLibraryExW(module_path, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) return 2;
    open_storage = (OPEN_STORAGE)(ULONG_PTR)GetProcAddress(module, (const char *)(ULONG_PTR)114);
    if (!open_storage) return 3;
    key = (HKEY)(ULONG_PTR)0x1234;
    result = open_storage(NULL, NULL, NULL, KEY_READ, &key);
    check(result == E_INVALIDARG && key == (HKEY)(ULONG_PTR)0x1234,
          "null package preserves output handle");
    check(open_storage(NULL, NULL, NULL, KEY_READ, NULL) == E_INVALIDARG,
          "invalid package requires no output dereference");
    key = (HKEY)(ULONG_PTR)0x1234;
    result = open_storage(L"Xodus.ProfApiProbe.Missing", NULL, NULL, KEY_READ, &key);
    check(result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) && !key,
          "missing state fails and clears registry output");
    key = (HKEY)(ULONG_PTR)0x1234;
    result = open_storage(L"Xodus.ProfApiProbe.Missing", L"", L"", KEY_READ, &key);
    check(result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) && !key,
          "empty optional segments retain missing-state error");
    status = RegOpenKeyExW(HKEY_CURRENT_USER, fixture_path, 0, KEY_READ, &fixture);
    if (!status) {
        RegCloseKey(fixture);
        puts("FAIL synthetic fixture already exists; refusing mutation");
        return 4;
    }
    if (status != ERROR_FILE_NOT_FOUND) {
        printf("FAIL synthetic fixture preflight=%ld\n", status);
        return 4;
    }
    status = RegCreateKeyExW(HKEY_CURRENT_USER, fixture_path, 0, NULL, 0,
                            KEY_ALL_ACCESS, NULL, &fixture, &disposition);
    if (status || disposition != REG_CREATED_NEW_KEY) {
        printf("FAIL synthetic fixture creation=%ld disposition=%lu\n", status, disposition);
        if (!status) RegCloseKey(fixture);
        return 4;
    }
    status = RegCreateKeyExW(fixture, L"Children\\Child", 0, NULL, 0, KEY_ALL_ACCESS,
                            NULL, &child, &disposition);
    if (!status)
        status = RegCreateKeyExW(child, L"Settings", 0, NULL, 0, KEY_ALL_ACCESS,
                                NULL, &settings, &disposition);
    check(!status, "explicit synthetic storage fixture");
    if (!status) {
        key = NULL;
        result = open_storage(L"Xodus.ProfApiStorageTest", NULL, NULL, KEY_READ, &key);
        check(result == S_OK && key != NULL, "existing storage opens with a real handle");
        if (key) RegCloseKey(key);
        key = NULL;
        result = open_storage(L"Xodus.ProfApiStorageTest", L"Child", L"Settings", KEY_READ, &key);
        check(result == S_OK && key != NULL, "existing child and subkey are resolved");
        if (key) RegCloseKey(key);
        key = (HKEY)(ULONG_PTR)0x1234;
        result = open_storage(L"Xodus.ProfApiStorageTest", L"MissingChild", NULL, KEY_READ, &key);
        check(result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) && !key,
              "existing package does not invent a missing child");
    }
    if (settings) RegCloseKey(settings);
    if (child) RegCloseKey(child);
    RegCloseKey(fixture);
    status = RegDeleteKeyW(HKEY_CURRENT_USER, settings_path);
    check(!status || status == ERROR_FILE_NOT_FOUND, "synthetic settings cleanup");
    status = RegDeleteKeyW(HKEY_CURRENT_USER, child_path);
    check(!status || status == ERROR_FILE_NOT_FOUND, "synthetic child cleanup");
    status = RegDeleteKeyW(HKEY_CURRENT_USER,
        L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\"
        L"CurrentVersion\\AppContainer\\Storage\\Xodus.ProfApiStorageTest\\Children");
    check(!status || status == ERROR_FILE_NOT_FOUND, "synthetic children cleanup");
    check(!RegDeleteKeyW(HKEY_CURRENT_USER, fixture_path), "synthetic root cleanup");
    FreeLibrary(module);
    printf("RESULT profapi_storage_checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
