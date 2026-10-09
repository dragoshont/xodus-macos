#include <initguid.h>
#include "package-manager-abi.h"
#include "package-manager-inventory.h"
#include <cstdio>
#include <cwchar>

static int failures;
#define CHECK(condition) do { if (!(condition)) { \
    std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

class Handler final : public SpaceCompletedHandler {
    LONG refs = 1;
public:
    int calls = 0;
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { return InterlockedDecrement(&refs); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!IsEqualGUID(iid, IID_IUnknown)) return E_NOINTERFACE;
        *out = this; AddRef(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Invoke(void *operation, INT32 status) override {
        ++calls;
        CHECK(status == 1);
        UINT64 bytes = 0;
        CHECK(static_cast<SpaceOperation *>(operation)->GetResults(&bytes) == S_OK);
        CHECK(bytes > 0);
        return S_OK;
    }
    LONG reference_count() const { return refs; }
};

int main(int argc, char **argv)
{
    if (argc != 2) { std::puts("usage: package-manager-test.exe provider.dll"); return 2; }
    HMODULE dll = LoadLibraryA(argv[1]);
    if (!dll) { std::printf("LoadLibrary error %lu\n", GetLastError()); return 2; }
    auto factory_fn = reinterpret_cast<HRESULT (WINAPI *)(HSTRING, IActivationFactory **)>(
        GetProcAddress(dll, "DllGetActivationFactory"));
    auto unload_fn = reinterpret_cast<HRESULT (WINAPI *)()>(GetProcAddress(dll, "DllCanUnloadNow"));
    CHECK(factory_fn && unload_fn);
    if (!factory_fn || !unload_fn) return 2;
    CHECK(unload_fn() == S_OK);
    HSTRING name;
    const WCHAR class_name[] = L"Windows.Management.Deployment.PackageManager";
    CHECK(WindowsCreateString(class_name, ARRAYSIZE(class_name)-1, &name) == S_OK);
    CHECK(factory_fn(name, nullptr) == E_POINTER);
    IActivationFactory *factory = nullptr;
    CHECK(factory_fn(nullptr, &factory) == CLASS_E_CLASSNOTAVAILABLE);
    CHECK(factory == nullptr);
    CHECK(factory_fn(name, &factory) == S_OK);
    WindowsDeleteString(name);
    CHECK(unload_fn() == S_FALSE);
    CHECK(factory->ActivateInstance(nullptr) == E_POINTER);
    IInspectable *object = nullptr;
    CHECK(factory->ActivateInstance(&object) == S_OK);
    PackageManager1 *manager1 = nullptr;
    PackageManager2 *manager2 = nullptr;
    PackageManager3 *manager3 = nullptr;
    CHECK(object->QueryInterface(PM1_IID, reinterpret_cast<void **>(&manager1)) == S_OK);
    CHECK(object->QueryInterface(PM2_IID, reinterpret_cast<void **>(&manager2)) == S_OK);
    CHECK(object->QueryInterface(PM3_IID, reinterpret_cast<void **>(&manager3)) == S_OK);
    IUnknown *identity1 = nullptr, *identity3 = nullptr;
    CHECK(manager1->QueryInterface(IID_IUnknown, reinterpret_cast<void **>(&identity1)) == S_OK);
    CHECK(manager3->QueryInterface(IID_IUnknown, reinterpret_cast<void **>(&identity3)) == S_OK);
    CHECK(identity1 == identity3);
    identity1->Release(); identity3->Release();
    void *out = reinterpret_cast<void *>(1);
    GUID unknown = {};
    CHECK(manager3->QueryInterface(unknown, &out) == E_NOINTERFACE && out == nullptr);
    CHECK(manager3->QueryInterface(PM1_IID, nullptr) == E_POINTER);
    CHECK(manager3->GetDefaultPackageVolume(nullptr) == E_POINTER);
    ULONG count;
    IID *ids;
    CHECK(manager3->GetIids(&count, &ids) == S_OK && count == 3);
    CHECK(IsEqualGUID(ids[0], PM1_IID) && IsEqualGUID(ids[2], PM3_IID));
    CoTaskMemFree(ids);
    CHECK(manager3->GetIids(nullptr, &ids) == E_POINTER && ids == nullptr);
    CHECK(manager3->GetIids(&count, nullptr) == E_POINTER && count == 0);
    CHECK(package_repository_key_absent(HKEY_LOCAL_MACHINE, L"Software", KEY_WOW64_64KEY) == E_NOTIMPL);
    WCHAR current_directory[32768];
    DWORD directory_length = GetCurrentDirectoryW(ARRAYSIZE(current_directory), current_directory);
    CHECK(directory_length && directory_length < ARRAYSIZE(current_directory));
    CHECK(package_repository_path_absent(current_directory) == E_NOTIMPL);
    wcscat(current_directory, L"\\package-manager-no-such-catalog-fixture");
    CHECK(package_repository_path_absent(current_directory) == S_OK);
    CHECK(package_environment_repository_absent(L"XODUS_PACKAGE_MANAGER_MISSING_ENV_FIXTURE",
        L"\\WindowsApps") == HRESULT_FROM_WIN32(ERROR_ENVVAR_NOT_FOUND));
    CHECK(package_fresh_inventory_absent() == S_OK);
    CHECK(package_current_user_only(nullptr) == S_OK);
    CHECK(manager1->FindPackagesByUserSecurityIdPackageFamilyName(nullptr, nullptr, &out) == E_INVALIDARG && !out);
    CHECK(manager1->FindPackagesByUserSecurityIdPackageFamilyName(nullptr, nullptr, nullptr) == E_POINTER);
    HSTRING family;
    const WCHAR arbitrary_family[] = L"Unregistered.Package_123456789abcd";
    CHECK(WindowsCreateString(arbitrary_family, ARRAYSIZE(arbitrary_family)-1, &family) == S_OK);
    PackageIterable *packages = nullptr;
    CHECK(manager1->FindPackagesByUserSecurityIdPackageFamilyName(nullptr, family,
        reinterpret_cast<void **>(&packages)) == S_OK);
    if (!packages) return 2;
    CHECK(packages->GetIids(&count, &ids) == S_OK && count == 1 && IsEqualGUID(ids[0], PACKAGE_ITERABLE_IID));
    CoTaskMemFree(ids);
    CHECK(packages->QueryInterface(PACKAGE_ITERABLE_IID, &out) == S_OK && out == packages);
    static_cast<PackageIterable *>(out)->Release();
    CHECK(packages->First(nullptr) == E_POINTER);
    PackageIterator *iterator = nullptr, *iterator2 = nullptr;
    CHECK(packages->First(&iterator) == S_OK);
    CHECK(packages->First(&iterator2) == S_OK && iterator != iterator2);
    packages->Release();
    CHECK(iterator->GetIids(&count, &ids) == S_OK && count == 1 && IsEqualGUID(ids[0], PACKAGE_ITERATOR_IID));
    CoTaskMemFree(ids);
    CHECK(iterator->QueryInterface(PACKAGE_ITERATOR_IID, &out) == S_OK && out == iterator);
    static_cast<PackageIterator *>(out)->Release();
    CHECK(iterator->QueryInterface(PACKAGE_ITERABLE_IID, &out) == E_NOINTERFACE && !out);
    BYTE has_current = 1;
    CHECK(iterator->get_HasCurrent(&has_current) == S_OK && !has_current);
    CHECK(iterator->get_HasCurrent(nullptr) == E_POINTER);
    CHECK(iterator->get_Current(&out) == static_cast<HRESULT>(0x8000000b) && !out);
    CHECK(iterator->get_Current(nullptr) == E_POINTER);
    CHECK(iterator->MoveNext(&has_current) == S_OK && !has_current);
    CHECK(iterator->MoveNext(nullptr) == E_POINTER);
    UINT32 actual = 1;
    CHECK(iterator->GetMany(0, nullptr, &actual) == S_OK && !actual);
    CHECK(iterator->GetMany(1, nullptr, &actual) == E_POINTER && !actual);
    CHECK(iterator->GetMany(0, nullptr, nullptr) == E_POINTER);
    void *items[2] = {};
    CHECK(iterator->GetMany(2, items, &actual) == S_OK && !actual);
    iterator->Release(); iterator2->Release();
    CHECK(WindowsCreateString(L"not-a-sid", 9, &name) == S_OK);
    CHECK(FAILED(manager1->FindPackagesByUserSecurityIdPackageFamilyName(name, family, &out)) && !out);
    WindowsDeleteString(name);
    CHECK(WindowsCreateString(L"S-1-5-18", 8, &name) == S_OK);
    CHECK(manager1->FindPackagesByUserSecurityIdPackageFamilyName(name, family, &out) == E_NOTIMPL && !out);
    WindowsDeleteString(name);
    HANDLE token;
    CHECK(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token));
    DWORD token_length = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &token_length);
    TOKEN_USER *token_user = static_cast<TOKEN_USER *>(HeapAlloc(GetProcessHeap(), 0, token_length));
    CHECK(GetTokenInformation(token, TokenUser, token_user, token_length, &token_length));
    WCHAR *sid_text;
    CHECK(ConvertSidToStringSidW(token_user->User.Sid, &sid_text));
    CHECK(WindowsCreateString(sid_text, static_cast<UINT32>(wcslen(sid_text)), &name) == S_OK);
    CHECK(manager1->FindPackagesByUserSecurityIdPackageFamilyName(name, family, &out) == S_OK);
    static_cast<PackageIterable *>(out)->Release();
    WindowsDeleteString(name); LocalFree(sid_text);
    HeapFree(GetProcessHeap(), 0, token_user); CloseHandle(token);
    WindowsDeleteString(family);

    PackageVolume1 *volume = nullptr;
    CHECK(manager3->GetDefaultPackageVolume(reinterpret_cast<void **>(&volume)) == S_OK);
    if (!volume) return 2;
    PackageVolume2 *volume2 = nullptr;
    CHECK(volume->QueryInterface(PV2_IID, reinterpret_cast<void **>(&volume2)) == S_OK);
    CHECK(volume->QueryInterface(IID_IUnknown, reinterpret_cast<void **>(&identity1)) == S_OK);
    CHECK(volume2->QueryInterface(IID_IUnknown, reinterpret_cast<void **>(&identity3)) == S_OK);
    CHECK(identity1 == identity3);
    identity1->Release(); identity3->Release();
    CHECK(volume2->GetIids(&count, &ids) == S_OK && count == 2);
    CHECK(IsEqualGUID(ids[0], PV1_IID) && IsEqualGUID(ids[1], PV2_IID));
    CoTaskMemFree(ids);
    CHECK(volume->get_MountPoint(nullptr) == E_POINTER);
    CHECK(volume->get_IsOffline(nullptr) == E_POINTER);
    CHECK(volume->get_IsSystemVolume(nullptr) == E_POINTER);
    CHECK(volume->get_Name(nullptr) == E_POINTER);
    CHECK(volume->get_PackageStorePath(nullptr) == E_POINTER);
    CHECK(volume->get_SupportsHardLinks(nullptr) == E_POINTER);
    CHECK(volume2->get_IsAppxInstallSupported(nullptr) == E_POINTER);
    CHECK(volume2->get_IsFullTrustPackageSupported(nullptr) == E_POINTER);
    CHECK(volume2->GetAvailableSpaceAsync(nullptr) == E_POINTER);
    BYTE boolean;
    CHECK(volume->get_IsOffline(&boolean) == S_OK && !boolean);
    CHECK(volume->get_IsSystemVolume(&boolean) == S_OK && boolean);
    CHECK(volume2->get_IsAppxInstallSupported(&boolean) == S_OK && !boolean);
    CHECK(volume2->get_IsFullTrustPackageSupported(&boolean) == S_OK && !boolean);
    DWORD flags = 0;
    CHECK(GetVolumeInformationW(L"C:\\", nullptr, 0, nullptr, nullptr, &flags, nullptr, 0));
    CHECK(volume->get_SupportsHardLinks(&boolean) == S_OK);
    CHECK(boolean == !!(flags & FILE_SUPPORTS_HARD_LINKS));
    CHECK(volume->get_MountPoint(&name) == S_OK);
    CHECK(!wcscmp(WindowsGetStringRawBuffer(name, nullptr), L"C:"));
    WindowsDeleteString(name);
    CHECK(volume->get_Name(&name) == S_OK);
    if (name) {
        WCHAR expected[MAX_PATH];
        CHECK(GetVolumeNameForVolumeMountPointW(L"C:\\", expected, MAX_PATH));
        size_t length = wcslen(expected);
        if (length && expected[length - 1] == L'\\') expected[length - 1] = 0;
        length = wcslen(expected);
        CHECK(!wcscmp(WindowsGetStringRawBuffer(name, nullptr), expected));
        std::printf("VOLUME_NAME=%ls\n", expected);
        CHECK(manager3->FindPackageVolumeByName(name, &out) == S_OK);
        static_cast<PackageVolume1 *>(out)->Release();
        WindowsDeleteString(name);
        expected[length] = 0;
        expected[length + 1] = L'x';
        CHECK(WindowsCreateString(expected, static_cast<UINT32>(length + 2), &name) == S_OK);
        CHECK(manager3->FindPackageVolumeByName(name, &out) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) && !out);
        WindowsDeleteString(name);
    }
    CHECK(manager3->FindPackageVolumeByName(nullptr, &out) == E_INVALIDARG && out == nullptr);
    CHECK(WindowsCreateString(L"not-a-volume", 12, &name) == S_OK);
    CHECK(manager3->FindPackageVolumeByName(name, &out) == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) && !out);
    WindowsDeleteString(name);
    name = reinterpret_cast<HSTRING>(1);
    CHECK(volume->get_PackageStorePath(&name) == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED) && !name);
    ULARGE_INTEGER before, after;
    CHECK(GetDiskFreeSpaceExW(L"C:\\", &before, nullptr, nullptr));
    SpaceOperation *operation = nullptr;
    CHECK(volume2->GetAvailableSpaceAsync(reinterpret_cast<void **>(&operation)) == S_OK);
    UINT64 bytes = 0;
    CHECK(operation->GetResults(&bytes) == S_OK);
    CHECK(operation->GetResults(nullptr) == E_POINTER);
    CHECK(GetDiskFreeSpaceExW(L"C:\\", &after, nullptr, nullptr));
    const UINT64 tolerance = 64u * 1024u * 1024u;
    CHECK(bytes > 0 && bytes <= before.QuadPart + tolerance && bytes + tolerance >= after.QuadPart);
    std::printf("REAL_AVAILABLE_SPACE=%llu WIN32_BEFORE=%llu WIN32_AFTER=%llu\n",
        static_cast<unsigned long long>(bytes), before.QuadPart, after.QuadPart);
    SpaceAsyncInfo *info = nullptr;
    CHECK(operation->QueryInterface(ASYNC_INFO_IID, reinterpret_cast<void **>(&info)) == S_OK);
    INT32 status = 0;
    HRESULT error;
    UINT32 id;
    CHECK(info->get_Id(&id) == S_OK && id);
    CHECK(info->get_Status(&status) == S_OK && status == 1);
    CHECK(info->get_ErrorCode(&error) == S_OK && error == S_OK);
    CHECK(info->Cancel() == S_OK);
    Handler handler;
    CHECK(operation->put_Completed(&handler) == S_OK && handler.calls == 1);
    CHECK(operation->put_Completed(&handler) == static_cast<HRESULT>(0x80000018));
    SpaceCompletedHandler *saved = nullptr;
    CHECK(operation->get_Completed(&saved) == S_OK && saved == &handler);
    saved->Release();
    CHECK(info->Close() == S_OK);
    CHECK(handler.reference_count() == 1);
    CHECK(operation->GetResults(&bytes) == static_cast<HRESULT>(0x80000013) && bytes == 0);
    CHECK(info->get_Status(&status) == static_cast<HRESULT>(0x80000013));
    CHECK(operation->get_Completed(&saved) == static_cast<HRESULT>(0x80000013) && !saved);
    CHECK(operation->put_Completed(nullptr) == static_cast<HRESULT>(0x80000013));
    info->Release(); operation->Release();

    out = reinterpret_cast<void *>(1);
    CHECK(manager1->AddPackageAsync(nullptr, nullptr, 0, &out) == E_NOTIMPL && !out);
    CHECK(manager1->AddPackageAsync(nullptr, nullptr, 0, nullptr) == E_POINTER);
    CHECK(manager1->RegisterPackageAsync(nullptr, nullptr, 0, &out) == E_NOTIMPL && !out);
    CHECK(manager2->RegisterPackageByFullNameAsync(nullptr, nullptr, 0, &out) == E_NOTIMPL && !out);
    CHECK(manager3->AddPackageToVolumeAsync(nullptr, nullptr, 0, volume, &out) == E_NOTIMPL && !out);
    CHECK(manager3->SetDefaultPackageVolume(volume) == E_NOTIMPL);
    CHECK(manager1->FindPackages(&out) == E_NOTIMPL && !out);
    CHECK(volume->FindPackages(&out) == E_NOTIMPL && !out);
    volume2->Release(); volume->Release();
    manager3->Release(); manager2->Release(); manager1->Release();
    object->Release();
    for (int i = 0; i < 1000; ++i) {
        CHECK(factory->ActivateInstance(&object) == S_OK);
        CHECK(object->QueryInterface(PM3_IID, reinterpret_cast<void **>(&manager3)) == S_OK);
        object->Release();
        CHECK(manager3->GetDefaultPackageVolume(reinterpret_cast<void **>(&volume)) == S_OK);
        manager3->Release();
        CHECK(volume->get_IsOffline(&boolean) == S_OK && !boolean);
        volume->Release();
    }
    factory->Release();
    CHECK(unload_fn() == S_OK);
    FreeLibrary(dll);
    std::printf("PACKAGE_MANAGER_TEST_%s failures=%d\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
