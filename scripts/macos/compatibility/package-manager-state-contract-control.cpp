#include <initguid.h>
#include "package-manager-abi.h"
#include <roapi.h>
#include <cstdio>
#include <cwchar>
#include <cstdlib>
#include <cstring>
#include "package-manager-state.h"

/*
 * Exact x64 ABI from the genuine KernelBase export at RVA e6eb0: five
 * arguments, byte-sized buffer capacity, DWORD Win32 status. It calls the
 * documented seven-argument RtlGetPersistedStateLocation with CustomValue=NULL
 * and StateLocationType=0. See the recorded disassembly, not an inferred ABI.
 */
typedef DWORD (WINAPI *StateQuery)(LPCWSTR, LPCWSTR, LPWSTR, DWORD, LPDWORD);
static StateQuery original_query;
static HMODULE captured_module;
static LONG captured_calls;

static LONG WINAPI exception_filter(EXCEPTION_POINTERS *info)
{
    std::printf("NATIVE_EXCEPTION=0x%08lx\n", info->ExceptionRecord->ExceptionCode);
    std::fflush(stdout);
    return EXCEPTION_EXECUTE_HANDLER;
}

static DWORD WINAPI capture_query(LPCWSTR source, LPCWSTR fallback, LPWSTR target,
                                  DWORD bytes, LPDWORD required)
{
    InterlockedIncrement(&captured_calls);
    auto caller = reinterpret_cast<ULONG_PTR>(__builtin_return_address(0));
    std::printf("ACTUAL_STATE_CALLER_RVA=0x%llx SOURCE=%ls DEFAULT=%ls BUFFER_BYTES=%lu SIZE_OUT=%s\n",
        static_cast<unsigned long long>(caller - reinterpret_cast<ULONG_PTR>(captured_module)),
        source ? source : L"<NULL>", fallback ? fallback : L"<NULL>", bytes,
        required ? "present" : "absent");
    std::fflush(stdout);
    DWORD result = original_query(source, fallback, target, bytes, required);
    std::printf("ACTUAL_STATE_RESULT=%lu", result);
    if (required) std::printf(" REQUIRED_BYTES=%lu", *required);
    if (!result && target) std::printf(" PATH=%ls", target);
    std::puts("");
    std::fflush(stdout);
    return result;
}

static StateQuery *find_query_slot(HMODULE dll)
{
    BYTE *base = reinterpret_cast<BYTE *>(dll);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva) return nullptr;
    auto imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + rva);
    for (; imports->Name; ++imports) {
        if (!imports->OriginalFirstThunk) continue;
        auto names = reinterpret_cast<IMAGE_THUNK_DATA *>(base + imports->OriginalFirstThunk);
        auto addresses = reinterpret_cast<IMAGE_THUNK_DATA *>(base + imports->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addresses) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto name = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData);
            if (!std::strcmp(reinterpret_cast<const char *>(name->Name), "GetPersistedRegistryLocationW"))
                return reinterpret_cast<StateQuery *>(&addresses->u1.Function);
        }
    }
    return nullptr;
}

static int capture(LPCWSTR path)
{
    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(hr)) return 1;
    HMODULE dll = LoadLibraryW(path);
    if (!dll) { std::printf("LOAD_ERROR=%lu\n", GetLastError()); RoUninitialize(); return 1; }
    StateQuery *slot = find_query_slot(dll);
    if (!slot) { FreeLibrary(dll); RoUninitialize(); return 1; }
    DWORD protection;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) {
        FreeLibrary(dll); RoUninitialize(); return 1;
    }
    original_query = *slot;
    captured_module = dll;
    *slot = capture_query;
    DWORD ignored;
    VirtualProtect(slot, sizeof(*slot), protection, &ignored);
    auto factory_query = reinterpret_cast<HRESULT (WINAPI *)(HSTRING, IActivationFactory **)>(
        GetProcAddress(dll, "DllGetActivationFactory"));
    IActivationFactory *factory = nullptr;
    IInspectable *manager = nullptr;
    const WCHAR name[] = L"Windows.ApplicationModel.Store.Preview.InstallControl.AppInstallManager";
    HSTRING class_name = nullptr;
    hr = WindowsCreateString(name, ARRAYSIZE(name) - 1, &class_name);
    if (SUCCEEDED(hr)) hr = factory_query ? factory_query(class_name, &factory) : E_NOINTERFACE;
    WindowsDeleteString(class_name);
    std::printf("GENUINE_FACTORY_HRESULT=0x%08lx\n", static_cast<unsigned long>(hr));
    if (SUCCEEDED(hr) && factory) hr = factory->ActivateInstance(&manager);
    std::printf("GENUINE_ACTIVATION_HRESULT=0x%08lx\n", static_cast<unsigned long>(hr));
    if (manager) manager->Release();
    if (factory) factory->Release();
    VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &ignored);
    *slot = original_query;
    VirtualProtect(slot, sizeof(*slot), protection, &ignored);
    FreeLibrary(dll);
    RoUninitialize();
    std::printf("ACTUAL_STATE_CAPTURE_COUNT=%ld\n", captured_calls);
    return captured_calls ? 0 : 1;
}

int wmain(int argc, WCHAR **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(exception_filter);
    if (argc == 2 && !wcscmp(argv[1], L"--selector")) {
        PM_STATE_FACTS facts = pm_state_query_context();
        auto native = reinterpret_cast<BYTE (WINAPI *)()>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlIsStateSeparationEnabled"));
        std::printf("ACTUAL_STATE_CONTEXT=%d SHARED_FLAGS=0x%08lx SILO_SCOPE=%u STATE_ENABLED=%u\n",
            facts.context, facts.shared_flags, facts.silo_scope, facts.state_enabled);
        if (native) std::printf("GENUINE_STATE_SELECTOR=%u\n", native());
        else std::puts("GENUINE_STATE_SELECTOR_EXPORT=absent");
        return facts.context == PM_STATE_UNAVAILABLE ? 1 : 0;
    }
    if (argc == 3 && !wcscmp(argv[1], L"--capture")) return capture(argv[2]);
    if (argc != 7 || wcscmp(argv[1], L"--case")) {
        std::puts("usage: --capture genuine-InstallService.dll | --case source default bytes buffer|null sizeout|none");
        return 2;
    }
    auto query = reinterpret_cast<StateQuery>(
        GetProcAddress(GetModuleHandleW(L"kernelbase.dll"), "GetPersistedRegistryLocationW"));
    if (!query) { std::puts("NATIVE_STATE_EXPORT_ABSENT"); return 1; }
    LPCWSTR source = !wcscmp(argv[2], L"@null") ? nullptr : argv[2];
    LPCWSTR fallback = !wcscmp(argv[3], L"@null") ? nullptr : argv[3];
    if (source && !wcscmp(source, L"@empty")) source = L"";
    if (fallback && !wcscmp(fallback, L"@empty")) fallback = L"";
    DWORD bytes = wcstoul(argv[4], nullptr, 10);
    if (bytes > 4096) return 2;
    WCHAR data[2048];
    for (auto &unit : data) unit = 0x5a5a;
    LPWSTR target = !wcscmp(argv[5], L"null") ? nullptr : data;
    DWORD required = 0xffffffff;
    LPDWORD out = !wcscmp(argv[6], L"none") ? nullptr : &required;
    SetLastError(0x13579bdf);
    DWORD result = query(source, fallback, target, bytes, out);
    std::printf("STATE_RESULT=%lu REQUIRED_BYTES=%lu LAST_ERROR=0x%08lx TARGET_FIRST=0x%04x\n",
        result, required, GetLastError(), data[0]);
    if (!result && target) {
        std::printf("STATE_PATH=%ls\nSTATE_UTF16=", target);
        for (DWORD i = 0; i < bytes / sizeof(WCHAR) && target[i]; ++i)
            std::printf("%04x", static_cast<unsigned>(target[i]));
        std::puts("");
    }
    return 0;
}
