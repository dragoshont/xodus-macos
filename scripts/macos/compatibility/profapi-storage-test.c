#include <windows.h>
#include <stdio.h>

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
    HMODULE module = LoadLibraryW(L"profapi.dll");
    OPEN_STORAGE open_storage;
    HKEY key;
    HRESULT result;
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
    FreeLibrary(module);
    printf("RESULT profapi_storage_checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
