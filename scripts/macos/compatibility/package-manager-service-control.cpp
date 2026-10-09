#include <initguid.h>
#include "package-manager-abi.h"
#include <roapi.h>
#include <sddl.h>
#include <cstdio>
#include <cwchar>
#include <cstring>

/*
 * Operates only on InstallService in the explicitly selected isolated prefix.
 * No manufactured service status, factory, install result or permission bypass.
 */
static int security(SC_HANDLE service, LPCWSTR sddl)
{
    PSECURITY_DESCRIPTOR expected = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &expected, nullptr))
        return 1;
    BOOL set = SetServiceObjectSecurity(service, DACL_SECURITY_INFORMATION, expected);
    DWORD error = set ? 0 : GetLastError();
    std::printf("REAL_SET_SERVICE_DACL=%u ERROR=%lu\n", set, error);
    DWORD size = 0;
    QueryServiceObjectSecurity(service, DACL_SECURITY_INFORMATION, nullptr, 0, &size);
    auto actual = static_cast<PSECURITY_DESCRIPTOR>(HeapAlloc(GetProcessHeap(), 0, size));
    BOOL read = actual && QueryServiceObjectSecurity(service, DACL_SECURITY_INFORMATION, actual, size, &size);
    std::printf("REAL_QUERY_SERVICE_DACL=%u ERROR=%lu\n", read, read ? 0 : GetLastError());
    WCHAR *actual_sddl = nullptr;
    if (read && ConvertSecurityDescriptorToStringSecurityDescriptorW(actual, SDDL_REVISION_1,
        DACL_SECURITY_INFORMATION, &actual_sddl, nullptr)) {
        std::printf("ACTUAL_SERVICE_DACL=%ls\n", actual_sddl);
        LocalFree(actual_sddl);
    }
    std::printf("EXPECTED_SERVICE_DACL=%ls\n", sddl);
    PACL wanted_acl = nullptr, actual_acl = nullptr;
    BOOL present = FALSE, defaulted;
    bool match = set && read &&
        GetSecurityDescriptorDacl(expected, &present, &wanted_acl, &defaulted) && present &&
        GetSecurityDescriptorDacl(actual, &present, &actual_acl, &defaulted) && present &&
        wanted_acl && actual_acl && wanted_acl->AclSize == actual_acl->AclSize &&
        !std::memcmp(wanted_acl, actual_acl, wanted_acl->AclSize);
    std::printf("GENUINE_SERVICE_DACL_ROUNDTRIP=%s\n", match ? "MATCH" : "FAIL");
    if (actual) HeapFree(GetProcessHeap(), 0, actual);
    LocalFree(expected);
    return match ? 0 : 1;
}

static int status(SC_HANDLE service)
{
    SERVICE_STATUS_PROCESS info;
    DWORD bytes;
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                             reinterpret_cast<BYTE *>(&info), sizeof(info), &bytes)) {
        std::printf("REAL_QUERY_SERVICE_STATUS_ERROR=%lu\n", GetLastError());
        return 1;
    }
    std::printf("REAL_SERVICE_STATE=%lu PID=%lu WIN32_EXIT=%lu SERVICE_EXIT=%lu CHECKPOINT=%lu\n",
        info.dwCurrentState, info.dwProcessId, info.dwWin32ExitCode,
        info.dwServiceSpecificExitCode, info.dwCheckPoint);
    return 0;
}

static int activation()
{
    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(hr)) return 1;
    const WCHAR name[] = L"Windows.Internal.InstallService.Control.InstallServiceControl";
    HSTRING class_name = nullptr;
    hr = WindowsCreateString(name, ARRAYSIZE(name) - 1, &class_name);
    IActivationFactory *factory = nullptr;
    if (SUCCEEDED(hr)) hr = RoGetActivationFactory(class_name, IID_IActivationFactory,
                                                  reinterpret_cast<void **>(&factory));
    std::printf("REAL_INTERNAL_SERVICE_FACTORY_HRESULT=0x%08lx\n", static_cast<unsigned long>(hr));
    if (SUCCEEDED(hr) && factory) {
        IInspectable *instance = nullptr;
        hr = factory->ActivateInstance(&instance);
        std::printf("REAL_INTERNAL_SERVICE_ACTIVATION_HRESULT=0x%08lx OBJECT=%s\n",
            static_cast<unsigned long>(hr), instance ? "present" : "absent");
        if (SUCCEEDED(hr) && !instance) hr = E_UNEXPECTED;
        if (instance) instance->Release();
        factory->Release();
    }
    WindowsDeleteString(class_name);
    RoUninitialize();
    return SUCCEEDED(hr) && factory ? 0 : 1;
}

static PFNGETACTIVATIONFACTORY native_factory;
static LONG native_callback_calls;
static HRESULT WINAPI native_callback(HSTRING name, IActivationFactory **out)
{
    InterlockedIncrement(&native_callback_calls);
    return native_factory(name, out);
}

static int registration(LPCWSTR client)
{
    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(hr)) return 1;
    HMODULE dll = LoadLibraryExW(client, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!dll) { std::printf("REGISTER_NATIVE_CLIENT_LOAD_ERROR=%lu\n", GetLastError()); RoUninitialize(); return 1; }
    native_factory = reinterpret_cast<PFNGETACTIVATIONFACTORY>(GetProcAddress(dll, "DllGetActivationFactory"));
    if (!native_factory) { FreeLibrary(dll); RoUninitialize(); return 1; }
    const WCHAR genuine_name[] = L"Windows.ApplicationModel.Store.Preview.InstallControl.AppInstallManager";
    HSTRING class_name = nullptr;
    hr = WindowsCreateString(genuine_name, ARRAYSIZE(genuine_name) - 1, &class_name);
    IActivationFactory *direct = nullptr;
    if (SUCCEEDED(hr)) hr = native_factory(class_name, &direct);
    std::printf("DIRECT_GENUINE_FACTORY_HRESULT=0x%08lx\n", static_cast<unsigned long>(hr));
    if (FAILED(hr) || !direct) {
        if (direct) direct->Release();
        WindowsDeleteString(class_name); FreeLibrary(dll); RoUninitialize(); return 1;
    }
    direct->Release();
    PFNGETACTIVATIONFACTORY callback = native_callback;
    RO_REGISTRATION_COOKIE cookie = reinterpret_cast<RO_REGISTRATION_COOKIE>(static_cast<ULONG_PTR>(0x12345678));
    hr = RoRegisterActivationFactories(&class_name, &callback, 1, &cookie);
    std::printf("REAL_RO_REGISTER_HRESULT=0x%08lx COOKIE_CHANGED=%u\n", static_cast<unsigned long>(hr),
        cookie != reinterpret_cast<RO_REGISTRATION_COOKIE>(static_cast<ULONG_PTR>(0x12345678)));
    IActivationFactory *registered = nullptr;
    HRESULT lookup = RoGetActivationFactory(class_name, IID_IActivationFactory,
                                            reinterpret_cast<void **>(&registered));
    std::printf("REAL_REGISTERED_FACTORY_LOOKUP=0x%08lx GENUINE_CALLBACK_CALLS=%ld OBJECT=%s\n",
        static_cast<unsigned long>(lookup), native_callback_calls, registered ? "present" : "absent");
    if (registered) registered->Release();
    if (SUCCEEDED(hr) && cookie != reinterpret_cast<RO_REGISTRATION_COOKIE>(static_cast<ULONG_PTR>(0x12345678)))
        RoRevokeActivationFactories(cookie);
    WindowsDeleteString(class_name);
    FreeLibrary(dll);
    RoUninitialize();
    return SUCCEEDED(lookup) && native_callback_calls && registered ? 0 : 1;
}

int wmain(int argc, WCHAR **argv)
{
    if (argc == 2 && !wcscmp(argv[1], L"--activate")) return activation();
    if (argc == 3 && !wcscmp(argv[1], L"--register-native")) return registration(argv[2]);
    if (argc < 2) { std::puts("usage: --security sddl | --query | --start | --stop | --activate"); return 2; }
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) { std::printf("REAL_OPEN_SCM_ERROR=%lu\n", GetLastError()); return 1; }
    SC_HANDLE service = OpenServiceW(manager, L"InstallService",
        SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP | READ_CONTROL | WRITE_DAC);
    if (!service) { std::printf("REAL_OPEN_SERVICE_ERROR=%lu\n", GetLastError()); CloseServiceHandle(manager); return 1; }
    int result = 0;
    if (argc == 3 && !wcscmp(argv[1], L"--security")) result = security(service, argv[2]);
    else if (argc == 2 && !wcscmp(argv[1], L"--query")) result = status(service);
    else if (argc == 2 && !wcscmp(argv[1], L"--start")) {
        BOOL started = StartServiceW(service, 0, nullptr);
        std::printf("REAL_START_SERVICE=%u ERROR=%lu\n", started, started ? 0 : GetLastError());
        result = started ? 0 : 1;
        status(service);
    } else if (argc == 2 && !wcscmp(argv[1], L"--stop")) {
        SERVICE_STATUS info;
        BOOL stopped = ControlService(service, SERVICE_CONTROL_STOP, &info);
        DWORD error = stopped ? 0 : GetLastError();
        std::printf("REAL_STOP_SERVICE=%u ERROR=%lu\n", stopped, error);
        result = stopped || error == ERROR_SERVICE_NOT_ACTIVE ? 0 : 1;
        status(service);
    } else result = 2;
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return result;
}
