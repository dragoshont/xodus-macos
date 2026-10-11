#include <stdio.h>
#include <wchar.h>
#include "windows.h"
#include "winstring.h"
#include "objbase.h"

static unsigned checks, failures;
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; printf("FAIL line %u: %s\n", __LINE__, #expr); } } while (0)
typedef HRESULT (WINAPI *server_classes_fn)(HSTRING, HSTRING **, DWORD *);

int main(void)
{
    static const WCHAR base[] = L"Software\\Microsoft\\WindowsRuntime\\";
    WCHAR server[100], other[100], names[3][100], path[300], provider[MAX_PATH];
    HKEY keys[4] = { NULL };
    BOOL created[4] = { FALSE };
    DWORD disposition, activation, count = 99;
    HMODULE module = LoadLibraryW(L"combase.dll");
    server_classes_fn query = module ? (void *)GetProcAddress(module, "RoGetServerActivatableClasses") : NULL;
    HSTRING name = NULL, unknown = NULL, *classes = (void *)1;
    HRESULT hr;
    unsigned i;

    CHECK(module && query);
    if (!query) goto done;
    CHECK(GetModuleFileNameW(module, provider, MAX_PATH) != 0);
    printf("SERVER_PROVIDER=%ls\n", provider);
    _snwprintf(server, 100, L"Xodus.Tests.Server.%lu", GetCurrentProcessId());
    _snwprintf(other, 100, L"Xodus.Tests.Other.%lu", GetCurrentProcessId());
    for (i = 0; i < 3; ++i)
        _snwprintf(names[i], 100, L"Xodus.Tests.ServerClass.%lu.%u", GetCurrentProcessId(), i);
    _snwprintf(path, 300, L"%lsServer\\%ls", base, server);
    CHECK(RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL,
                         &keys[0], &disposition) == ERROR_SUCCESS);
    if (!keys[0]) goto cleanup;
    CHECK(disposition == REG_CREATED_NEW_KEY);
    if (disposition != REG_CREATED_NEW_KEY) goto cleanup;
    created[0] = TRUE;
    for (i = 0; i < 3; ++i)
    {
        _snwprintf(path, 300, L"%lsActivatableClassId\\%ls", base, names[i]);
        CHECK(RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL,
                             &keys[i + 1], &disposition) == ERROR_SUCCESS);
        if (!keys[i + 1]) goto cleanup;
        CHECK(disposition == REG_CREATED_NEW_KEY);
        if (disposition != REG_CREATED_NEW_KEY) goto cleanup;
        created[i + 1] = TRUE;
        activation = i == 2 ? 0 : 1;
        CHECK(RegSetValueExW(keys[i + 1], L"ActivationType", 0, REG_DWORD,
                            (BYTE *)&activation, sizeof(activation)) == ERROR_SUCCESS);
        CHECK(RegSetValueExW(keys[i + 1], L"Server", 0, REG_SZ, (BYTE *)(i == 1 ? other : server),
                            (wcslen(i == 1 ? other : server) + 1) * sizeof(WCHAR)) == ERROR_SUCCESS);
    }
    CHECK(WindowsCreateString(server, wcslen(server), &name) == S_OK);
    CHECK(WindowsCreateString(other, wcslen(other), &unknown) == S_OK);
    CHECK(query(name, NULL, &count) == E_POINTER && !count);
    CHECK(query(name, &classes, NULL) == E_POINTER && !classes);
    CHECK(query(NULL, &classes, &count) == REGDB_E_CLASSNOTREG && !classes && !count);
    CHECK(query(unknown, &classes, &count) == REGDB_E_CLASSNOTREG && !classes && !count);
    hr = query(name, &classes, &count);
    CHECK(hr == S_OK && count == 1 && classes);
    if (SUCCEEDED(hr) && count == 1)
    {
        CHECK(!wcscmp(WindowsGetStringRawBuffer(classes[0], NULL), names[0]));
        CHECK(WindowsDeleteString(classes[0]) == S_OK);
        classes[0] = NULL;
        CoTaskMemFree(classes);
        classes = NULL;
    }
    activation = 0;
    CHECK(RegSetValueExW(keys[1], L"ActivationType", 0, REG_DWORD, (BYTE *)&activation,
                        sizeof(activation)) == ERROR_SUCCESS);
    CHECK(query(name, &classes, &count) == REGDB_E_CLASSNOTREG && !classes && !count);
cleanup:
    if (classes && classes != (void *)1)
    {
        for (i = 0; i < count; ++i) WindowsDeleteString(classes[i]);
        CoTaskMemFree(classes);
    }
    WindowsDeleteString(name);
    WindowsDeleteString(unknown);
    for (i = 0; i < 4; ++i)
    {
        if (keys[i]) RegCloseKey(keys[i]);
        if (!created[i]) continue;
        if (!i) _snwprintf(path, 300, L"%lsServer\\%ls", base, server);
        else _snwprintf(path, 300, L"%lsActivatableClassId\\%ls", base, names[i - 1]);
        CHECK(RegDeleteKeyW(HKEY_LOCAL_MACHINE, path) == ERROR_SUCCESS);
    }
    if (module) FreeLibrary(module);
done:
    printf("server_classes_checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
