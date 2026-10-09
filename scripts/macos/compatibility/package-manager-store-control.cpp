#include <initguid.h>
#include "package-manager-store-abi.h"
#include <roapi.h>
#include <cstdio>
#include <cstring>

struct RuntimeScope {
    HRESULT status = RoInitialize(RO_INIT_MULTITHREADED);
    ~RuntimeScope() { if (SUCCEEDED(status)) RoUninitialize(); }
};

/*
 * Headless, read-only native-client control. Loads an explicitly supplied
 * genuine Store client, directly requests its activation factory, checks the
 * observed interfaces, and optionally reads the actual installation queue.
 * Never changes activation mapping, identity, entitlement, or install state.
 */
int main(int argc, char **argv)
{
    bool queue = argc == 3 && !std::strcmp(argv[2], "--read-queue");
    if ((argc != 2 && argc != 3) || (argc == 3 && !queue)) {
        std::puts("usage: package-manager-store-control.exe verified-native-client.dll [--read-queue]");
        return 2;
    }
    RuntimeScope runtime;
    if (FAILED(runtime.status)) {
        std::printf("STORE_RUNTIME_INITIALIZE_HRESULT=0x%08lx\n", static_cast<unsigned long>(runtime.status));
        return 1;
    }
    HMODULE dll = LoadLibraryA(argv[1]);
    if (!dll) {
        std::printf("STORE_NATIVE_LOAD_ERROR=%lu\n", GetLastError());
        return 1;
    }
    auto get_factory = reinterpret_cast<HRESULT (WINAPI *)(HSTRING, IActivationFactory **)>(
        GetProcAddress(dll, "DllGetActivationFactory"));
    if (!get_factory) {
        std::printf("STORE_NATIVE_FACTORY_EXPORT_ERROR=%lu\n", GetLastError());
        FreeLibrary(dll);
        return 1;
    }
    const WCHAR name[] = L"Windows.ApplicationModel.Store.Preview.InstallControl.AppInstallManager";
    HSTRING class_name;
    HRESULT hr = WindowsCreateString(name, ARRAYSIZE(name) - 1, &class_name);
    if (FAILED(hr)) { FreeLibrary(dll); return 1; }
    IActivationFactory *factory = nullptr;
    hr = get_factory(class_name, &factory);
    WindowsDeleteString(class_name);
    std::printf("STORE_NATIVE_FACTORY_HRESULT=0x%08lx\n", static_cast<unsigned long>(hr));
    if (FAILED(hr) || !factory) {
        if (factory) factory->Release();
        FreeLibrary(dll);
        return 1;
    }
    IInspectable *manager = nullptr;
    hr = factory->ActivateInstance(&manager);
    factory->Release();
    std::printf("STORE_NATIVE_ACTIVATION_HRESULT=0x%08lx\n", static_cast<unsigned long>(hr));
    if (FAILED(hr) || !manager) {
        if (manager) manager->Release();
        FreeLibrary(dll);
        return 1;
    }
    StoreManager1 *v1 = nullptr;
    StoreManager6 *v6 = nullptr;
    HRESULT hr1 = manager->QueryInterface(STORE_MANAGER1_IID, reinterpret_cast<void **>(&v1));
    HRESULT hr6 = manager->QueryInterface(STORE_MANAGER6_IID, reinterpret_cast<void **>(&v6));
    std::printf("STORE_NATIVE_V1_QI=0x%08lx STORE_NATIVE_V6_QI=0x%08lx\n",
        static_cast<unsigned long>(hr1), static_cast<unsigned long>(hr6));
    if (queue && SUCCEEDED(hr1) && v1) {
        IInspectable *items = nullptr;
        hr = v1->get_AppInstallItems(reinterpret_cast<void **>(&items));
        std::printf("STORE_NATIVE_READ_QUEUE_HRESULT=0x%08lx ACTUAL_QUEUE_OBJECT=%s\n",
            static_cast<unsigned long>(hr), items ? "present" : "absent");
        if (items) items->Release();
        if (SUCCEEDED(hr) && !items) hr = E_UNEXPECTED;
    }
    if (v6) v6->Release();
    if (v1) v1->Release();
    manager->Release();
    FreeLibrary(dll);
    if (FAILED(hr1) || FAILED(hr6) || FAILED(hr)) return 1;
    std::puts("STORE_NATIVE_CLIENT_CONTROL_PASS (no installation requested)");
    return 0;
}
