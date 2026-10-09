#ifndef XODUS_PACKAGE_MANAGER_INVENTORY_H
#define XODUS_PACKAGE_MANAGER_INVENTORY_H

#include <windows.h>
#include <winstring.h>
#include <sddl.h>
#include <cwchar>
#include <initializer_list>

/* Absence is evidence only on a fresh prefix. An existing/unknown repository
 * must never be presented as an empty registered-package catalog. */
static HRESULT package_repository_path_absent(const WCHAR *path)
{
    DWORD attributes = GetFileAttributesW(path);
    if (attributes != INVALID_FILE_ATTRIBUTES) return E_NOTIMPL;
    DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return S_OK;
    return HRESULT_FROM_WIN32(error);
}

static HRESULT package_repository_key_absent(HKEY hive, const WCHAR *path, REGSAM view)
{
    HKEY key;
    LSTATUS status = RegOpenKeyExW(hive, path, 0, KEY_READ | view, &key);
    if (status == ERROR_SUCCESS) {
        RegCloseKey(key);
        return E_NOTIMPL;
    }
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) return S_OK;
    return HRESULT_FROM_WIN32(status);
}

static HRESULT package_current_user_only(HSTRING requested)
{
    UINT32 length;
    const WCHAR *text = WindowsGetStringRawBuffer(requested, &length);
    if (!length) return S_OK;
    if (wcslen(text) != length) return E_INVALIDARG;
    PSID sid;
    if (!ConvertStringSidToSidW(text, &sid)) return HRESULT_FROM_WIN32(GetLastError());
    HANDLE token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        DWORD error = GetLastError();
        LocalFree(sid);
        return HRESULT_FROM_WIN32(error);
    }
    DWORD needed = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &needed);
    DWORD error = GetLastError();
    HRESULT hr = HRESULT_FROM_WIN32(error);
    if (error == ERROR_INSUFFICIENT_BUFFER && needed >= sizeof(TOKEN_USER)) {
        TOKEN_USER *user = static_cast<TOKEN_USER *>(HeapAlloc(GetProcessHeap(), 0, needed));
        if (!user) hr = E_OUTOFMEMORY;
        else {
            if (!GetTokenInformation(token, TokenUser, user, needed, &needed))
                hr = HRESULT_FROM_WIN32(GetLastError());
            else hr = EqualSid(sid, user->User.Sid) ? S_OK : E_NOTIMPL;
            HeapFree(GetProcessHeap(), 0, user);
        }
    }
    CloseHandle(token);
    LocalFree(sid);
    return hr;
}

static HRESULT package_environment_repository_absent(const WCHAR *variable, const WCHAR *suffix)
{
    WCHAR root[32768];
    DWORD length = GetEnvironmentVariableW(variable, root, ARRAYSIZE(root));
    if (!length) {
        DWORD error = GetLastError();
        return HRESULT_FROM_WIN32(error ? error : ERROR_ENVVAR_NOT_FOUND);
    }
    size_t extra = wcslen(suffix);
    if (length >= ARRAYSIZE(root) || length + extra >= ARRAYSIZE(root))
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    if (length < 3 || root[1] != L':' || root[2] != L'\\') return E_INVALIDARG;
    wcscpy(root + length, suffix);
    return package_repository_path_absent(root);
}

static HRESULT package_fresh_inventory_absent()
{
    const struct {
        HKEY hive;
        const WCHAR *path;
    } registrations[] = {
        {HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Appx"},
        {HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\StateRepository"},
        {HKEY_CURRENT_USER, L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\Repository\\Packages"},
        {HKEY_LOCAL_MACHINE, L"Software\\Classes\\ActivatableClasses\\Package"},
        {HKEY_CURRENT_USER, L"Software\\Classes\\ActivatableClasses\\Package"}
    };
    for (const auto &registration : registrations) {
        for (REGSAM view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
            HRESULT hr = package_repository_key_absent(registration.hive, registration.path, view);
            if (FAILED(hr)) return hr;
        }
    }
    HRESULT hr = package_environment_repository_absent(L"ProgramData", L"\\Microsoft\\Windows\\AppRepository");
    if (FAILED(hr)) return hr;
#ifdef _WIN64
    hr = package_environment_repository_absent(L"ProgramFiles", L"\\WindowsApps");
#else
    /* WOW64 must inspect the actual system package store, not just x86 files. */
    hr = package_environment_repository_absent(L"ProgramW6432", L"\\WindowsApps");
#endif
    if (FAILED(hr)) return hr;
    return package_environment_repository_absent(L"ProgramFiles(x86)", L"\\WindowsApps");
}
#endif
