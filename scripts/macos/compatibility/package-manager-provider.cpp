#include <initguid.h>
#include "package-manager-abi.h"
#include "package-manager-inventory.h"
#include <new>
#include <cwchar>
#include <cstdio>

static LONG live_objects;
static const WCHAR manager_name[] = L"Windows.Management.Deployment.PackageManager";
static const WCHAR volume_name[] = L"Windows.Management.Deployment.PackageVolume";

static HRESULT unsupported(const char *method, void **out)
{
    if (!out) return E_POINTER;
    *out = nullptr;
    std::fprintf(stderr, "PackageManager compatibility: %s unavailable (E_NOTIMPL); no durable deployment backend\n", method);
    return E_NOTIMPL;
}

static HRESULT make_string(const WCHAR *value, HSTRING *out)
{
    if (!out) return E_POINTER;
    *out = nullptr;
    return WindowsCreateString(value, static_cast<UINT32>(wcslen(value)), out);
}

static HRESULT copy_iids(const GUID *source, ULONG length, ULONG *count, IID **out)
{
    if (count) *count = 0;
    if (out) *out = nullptr;
    if (!count || !out) return E_POINTER;
    *out = static_cast<IID *>(CoTaskMemAlloc(length * sizeof(IID)));
    if (!*out) return E_OUTOFMEMORY;
    CopyMemory(*out, source, length * sizeof(IID));
    *count = length;
    return S_OK;
}

static HRESULT volume_name_from_mount(WCHAR (&name)[MAX_PATH])
{
    if (!GetVolumeNameForVolumeMountPointW(L"C:\\", name, MAX_PATH))
        return HRESULT_FROM_WIN32(GetLastError());
    size_t length = wcslen(name);
    /* PackageVolume.Name omits the Win32 volume-path trailing separator. */
    if (length && name[length - 1] == L'\\') name[length - 1] = 0;
    return S_OK;
}

#define UNKNOWN_BODY(primary) \
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); } \
    ULONG STDMETHODCALLTYPE Release() override { \
        ULONG remaining = InterlockedDecrement(&refs); \
        if (!remaining) delete this; \
        return remaining; \
    } \
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING *out) override { \
        return make_string(primary, out); \
    } \
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel *out) override { \
        if (!out) return E_POINTER; \
        *out = BaseTrust; return S_OK; \
    }
#define NO_OUT(name, args) HRESULT STDMETHODCALLTYPE name args override { \
    std::fprintf(stderr, "PackageManager compatibility: %s unavailable (E_NOTIMPL); no durable deployment backend\n", #name); \
    return E_NOTIMPL; }
#define NO_RESULT(name, args) HRESULT STDMETHODCALLTYPE name args override { return unsupported(#name, out); }

class EmptyPackageIterator final : public PackageIterator {
    LONG refs = 1;
public:
    EmptyPackageIterator() { InterlockedIncrement(&live_objects); }
    ~EmptyPackageIterator() { InterlockedDecrement(&live_objects); }
    UNKNOWN_BODY(L"")
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!IsEqualGUID(iid, IID_IUnknown) && !IsEqualGUID(iid, IID_IInspectable) &&
            !IsEqualGUID(iid, PACKAGE_ITERATOR_IID)) return E_NOINTERFACE;
        *out = static_cast<PackageIterator *>(this);
        AddRef(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG *count, IID **out) override {
        return copy_iids(&PACKAGE_ITERATOR_IID, 1, count, out);
    }
    HRESULT STDMETHODCALLTYPE get_Current(void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return static_cast<HRESULT>(0x8000000b); /* E_BOUNDS */
    }
    HRESULT STDMETHODCALLTYPE get_HasCurrent(BYTE *out) override {
        if (!out) return E_POINTER;
        *out = 0; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MoveNext(BYTE *out) override {
        if (!out) return E_POINTER;
        *out = 0; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMany(UINT32 capacity, void **items, UINT32 *actual) override {
        if (!actual) return E_POINTER;
        *actual = 0;
        if (capacity && !items) return E_POINTER;
        return S_OK;
    }
};

class EmptyPackageIterable final : public PackageIterable {
    LONG refs = 1;
public:
    EmptyPackageIterable() { InterlockedIncrement(&live_objects); }
    ~EmptyPackageIterable() { InterlockedDecrement(&live_objects); }
    UNKNOWN_BODY(L"")
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!IsEqualGUID(iid, IID_IUnknown) && !IsEqualGUID(iid, IID_IInspectable) &&
            !IsEqualGUID(iid, PACKAGE_ITERABLE_IID)) return E_NOINTERFACE;
        *out = static_cast<PackageIterable *>(this);
        AddRef(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG *count, IID **out) override {
        return copy_iids(&PACKAGE_ITERABLE_IID, 1, count, out);
    }
    HRESULT STDMETHODCALLTYPE First(PackageIterator **out) override {
        if (!out) return E_POINTER;
        *out = new(std::nothrow) EmptyPackageIterator;
        return *out ? S_OK : E_OUTOFMEMORY;
    }
};

class AvailableSpace final : public SpaceOperation, public SpaceAsyncInfo {
    LONG refs = 1;
    UINT64 available;
    UINT32 id;
    CRITICAL_SECTION lock;
    bool closed = false, assigned = false;
    SpaceCompletedHandler *handler = nullptr;
    static LONG next_id;
public:
    explicit AvailableSpace(UINT64 bytes) : available(bytes) {
        id = InterlockedIncrement(&next_id);
        InitializeCriticalSection(&lock);
        InterlockedIncrement(&live_objects);
    }
    ~AvailableSpace() {
        if (handler) handler->Release();
        DeleteCriticalSection(&lock);
        InterlockedDecrement(&live_objects);
    }
    UNKNOWN_BODY(L"Xodus.Compatibility.AvailableSpaceOperation")
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (IsEqualGUID(iid, IID_IUnknown) || IsEqualGUID(iid, IID_IInspectable) ||
            IsEqualGUID(iid, SPACE_IID)) *out = static_cast<SpaceOperation *>(this);
        else if (IsEqualGUID(iid, ASYNC_INFO_IID)) *out = static_cast<SpaceAsyncInfo *>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG *count, IID **out) override {
        const GUID ids[] = {SPACE_IID, ASYNC_INFO_IID};
        return copy_iids(ids, 2, count, out);
    }
    HRESULT STDMETHODCALLTYPE put_Completed(SpaceCompletedHandler *value) override {
        EnterCriticalSection(&lock);
        HRESULT hr = closed ? static_cast<HRESULT>(0x80000013) :
            assigned ? static_cast<HRESULT>(0x80000018) : S_OK;
        if (SUCCEEDED(hr)) {
            assigned = true;
            handler = value;
            if (handler) handler->AddRef();
            if (value) value->AddRef();
            AddRef();
        }
        LeaveCriticalSection(&lock);
        if (SUCCEEDED(hr)) {
            /* Already completed. Invoke outside the lock, with strong references. */
            if (value) { value->Invoke(static_cast<SpaceOperation *>(this), 1); value->Release(); }
            Release();
        }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE get_Completed(SpaceCompletedHandler **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        EnterCriticalSection(&lock);
        HRESULT hr = closed ? static_cast<HRESULT>(0x80000013) : S_OK;
        if (SUCCEEDED(hr) && handler) { handler->AddRef(); *out = handler; }
        LeaveCriticalSection(&lock);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetResults(UINT64 *out) override {
        if (!out) return E_POINTER;
        *out = 0;
        EnterCriticalSection(&lock);
        HRESULT hr = closed ? static_cast<HRESULT>(0x80000013) : S_OK;
        if (SUCCEEDED(hr)) *out = available;
        LeaveCriticalSection(&lock);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE get_Id(UINT32 *out) override {
        if (!out) return E_POINTER;
        *out = 0;
        EnterCriticalSection(&lock);
        HRESULT hr = closed ? static_cast<HRESULT>(0x80000013) : S_OK;
        if (SUCCEEDED(hr)) *out = id;
        LeaveCriticalSection(&lock);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE get_Status(INT32 *out) override {
        if (!out) return E_POINTER;
        *out = 0;
        EnterCriticalSection(&lock);
        HRESULT hr = closed ? static_cast<HRESULT>(0x80000013) : S_OK;
        if (SUCCEEDED(hr)) *out = 1;
        LeaveCriticalSection(&lock);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT *out) override {
        if (!out) return E_POINTER;
        *out = S_OK;
        EnterCriticalSection(&lock);
        HRESULT hr = closed ? static_cast<HRESULT>(0x80000013) : S_OK;
        LeaveCriticalSection(&lock);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Cancel() override {
        EnterCriticalSection(&lock);
        HRESULT hr = closed ? static_cast<HRESULT>(0x80000013) : S_OK;
        LeaveCriticalSection(&lock);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Close() override {
        EnterCriticalSection(&lock);
        closed = true;
        SpaceCompletedHandler *old = handler;
        handler = nullptr;
        LeaveCriticalSection(&lock);
        if (old) old->Release();
        return S_OK;
    }
};
LONG AvailableSpace::next_id;

class Volume final : public PackageVolume1, public PackageVolume2 {
    LONG refs = 1;
public:
    Volume() { InterlockedIncrement(&live_objects); }
    ~Volume() { InterlockedDecrement(&live_objects); }
    UNKNOWN_BODY(volume_name)
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (IsEqualGUID(iid, IID_IUnknown) || IsEqualGUID(iid, IID_IInspectable) ||
            IsEqualGUID(iid, PV1_IID)) *out = static_cast<PackageVolume1 *>(this);
        else if (IsEqualGUID(iid, PV2_IID)) *out = static_cast<PackageVolume2 *>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG *count, IID **out) override {
        const GUID ids[] = {PV1_IID, PV2_IID};
        return copy_iids(ids, 2, count, out);
    }
    HRESULT STDMETHODCALLTYPE get_IsOffline(BYTE *out) override {
        if (!out) return E_POINTER;
        *out = 0;
        DWORD attributes = GetFileAttributesW(L"C:\\");
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            DWORD error = GetLastError();
            if (error == ERROR_PATH_NOT_FOUND || error == ERROR_NOT_READY ||
                error == ERROR_FILE_NOT_FOUND) { *out = 1; return S_OK; }
            return HRESULT_FROM_WIN32(error);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsSystemVolume(BYTE *out) override {
        WCHAR windows[MAX_PATH], root[MAX_PATH];
        if (!out) return E_POINTER;
        *out = 0;
        UINT size = GetWindowsDirectoryW(windows, MAX_PATH);
        if (!size) return HRESULT_FROM_WIN32(GetLastError());
        if (size >= MAX_PATH) return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        if (!GetVolumePathNameW(windows, root, MAX_PATH))
            return HRESULT_FROM_WIN32(GetLastError());
        *out = !_wcsicmp(root, L"C:\\");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_MountPoint(HSTRING *out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        DWORD attributes = GetFileAttributesW(L"C:\\");
        if (attributes == INVALID_FILE_ATTRIBUTES) return HRESULT_FROM_WIN32(GetLastError());
        return make_string(L"C:", out);
    }
    HRESULT STDMETHODCALLTYPE get_Name(HSTRING *out) override {
        WCHAR name[MAX_PATH];
        if (!out) return E_POINTER;
        *out = nullptr;
        HRESULT hr = volume_name_from_mount(name);
        if (FAILED(hr)) return hr;
        return make_string(name, out);
    }
    HRESULT STDMETHODCALLTYPE get_PackageStorePath(HSTRING *out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        /* No durable package store exists. Never invent WindowsApps state. */
        std::fprintf(stderr, "PackageManager compatibility: PackageStorePath unavailable (ERROR_NOT_SUPPORTED); no durable package store\n");
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }
    HRESULT STDMETHODCALLTYPE get_SupportsHardLinks(BYTE *out) override {
        DWORD flags;
        if (!out) return E_POINTER;
        *out = 0;
        if (!GetVolumeInformationW(L"C:\\", nullptr, 0, nullptr, nullptr, &flags, nullptr, 0))
            return HRESULT_FROM_WIN32(GetLastError());
        *out = !!(flags & FILE_SUPPORTS_HARD_LINKS);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsFullTrustPackageSupported(BYTE *out) override {
        if (!out) return E_POINTER;
        *out = 0; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_IsAppxInstallSupported(BYTE *out) override {
        if (!out) return E_POINTER;
        *out = 0; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetAvailableSpaceAsync(void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        ULARGE_INTEGER available;
        if (!GetDiskFreeSpaceExW(L"C:\\", &available, nullptr, nullptr))
            return HRESULT_FROM_WIN32(GetLastError());
        AvailableSpace *operation = new(std::nothrow) AvailableSpace(available.QuadPart);
        if (!operation) return E_OUTOFMEMORY;
        *out = static_cast<SpaceOperation *>(operation);
        return S_OK;
    }
    NO_RESULT(FindPackages, (void **out))
    NO_RESULT(FindPackagesByNamePublisher, (HSTRING, HSTRING, void **out))
    NO_RESULT(FindPackagesByPackageFamilyName, (HSTRING, void **out))
    NO_RESULT(FindPackagesWithPackageTypes, (UINT32, void **out))
    NO_RESULT(FindPackagesByNamePublisherWithPackagesTypes, (UINT32, HSTRING, HSTRING, void **out))
    NO_RESULT(FindPackagesByPackageFamilyNameWithPackageTypes, (UINT32, HSTRING, void **out))
    NO_RESULT(FindPackageByPackageFullName, (HSTRING, void **out))
    NO_RESULT(FindPackagesByUserSecurityId, (HSTRING, void **out))
    NO_RESULT(FindPackagesByUserSecurityIdNamePublisher, (HSTRING, HSTRING, HSTRING, void **out))
    NO_RESULT(FindPackagesByUserSecurityIdPackageFamilyName, (HSTRING, HSTRING, void **out))
    NO_RESULT(FindPackagesByUserSecurityIdWithPackageTypes, (HSTRING, UINT32, void **out))
    NO_RESULT(FindPackagesByUserSecurityIdNamePublisherWithPackageTypes, (HSTRING, UINT32, HSTRING, HSTRING, void **out))
    NO_RESULT(FindPackagesByUserSecurityIdPackageFamilyNameWithPackagesTypes, (HSTRING, UINT32, HSTRING, void **out))
    NO_RESULT(FindPackageByUserSecurityIdPackageFullName, (HSTRING, HSTRING, void **out))
};

class Manager final : public PackageManager1, public PackageManager2, public PackageManager3 {
    LONG refs = 1;
public:
    Manager() { InterlockedIncrement(&live_objects); }
    ~Manager() { InterlockedDecrement(&live_objects); }
    UNKNOWN_BODY(manager_name)
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (IsEqualGUID(iid, IID_IUnknown) || IsEqualGUID(iid, IID_IInspectable) ||
            IsEqualGUID(iid, PM1_IID)) *out = static_cast<PackageManager1 *>(this);
        else if (IsEqualGUID(iid, PM2_IID)) *out = static_cast<PackageManager2 *>(this);
        else if (IsEqualGUID(iid, PM3_IID)) *out = static_cast<PackageManager3 *>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG *count, IID **out) override {
        const GUID ids[] = {PM1_IID, PM2_IID, PM3_IID};
        return copy_iids(ids, 3, count, out);
    }
    HRESULT STDMETHODCALLTYPE GetDefaultPackageVolume(void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        DWORD attributes = GetFileAttributesW(L"C:\\");
        if (attributes == INVALID_FILE_ATTRIBUTES) return HRESULT_FROM_WIN32(GetLastError());
        Volume *volume = new(std::nothrow) Volume;
        if (!volume) return E_OUTOFMEMORY;
        *out = static_cast<PackageVolume1 *>(volume);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE FindPackageVolumeByName(HSTRING name, void **out) override {
        WCHAR actual[MAX_PATH];
        if (!out) return E_POINTER;
        *out = nullptr;
        UINT32 length;
        const WCHAR *requested = WindowsGetStringRawBuffer(name, &length);
        if (!length) return E_INVALIDARG;
        HRESULT hr = volume_name_from_mount(actual);
        if (FAILED(hr)) return hr;
        if (length != wcslen(actual) || _wcsnicmp(requested, actual, length))
            return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        return GetDefaultPackageVolume(out);
    }
    NO_RESULT(AddPackageAsync, (void *, void *, UINT32, void **out))
    NO_RESULT(UpdatePackageAsync, (void *, void *, UINT32, void **out))
    NO_RESULT(RemovePackageAsync, (HSTRING, void **out))
    NO_RESULT(StagePackageAsync, (void *, void *, void **out))
    NO_RESULT(RegisterPackageAsync, (void *, void *, UINT32, void **out))
    NO_RESULT(FindPackages, (void **out))
    NO_RESULT(FindPackagesByUserSecurityId, (HSTRING, void **out))
    NO_RESULT(FindPackagesByNamePublisher, (HSTRING, HSTRING, void **out))
    NO_RESULT(FindPackagesByUserSecurityIdNamePublisher, (HSTRING, HSTRING, HSTRING, void **out))
    NO_RESULT(FindUsers, (HSTRING, void **out))
    NO_OUT(SetPackageState, (HSTRING, INT32))
    NO_RESULT(FindPackageByPackageFullName, (HSTRING, void **out))
    NO_RESULT(CleanupPackageForUserAsync, (HSTRING, HSTRING, void **out))
    NO_RESULT(FindPackagesByPackageFamilyName, (HSTRING, void **out))
    HRESULT STDMETHODCALLTYPE FindPackagesByUserSecurityIdPackageFamilyName(
        HSTRING user, HSTRING family, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        UINT32 length;
        const WCHAR *requested = WindowsGetStringRawBuffer(family, &length);
        if (!length || wcslen(requested) != length) return E_INVALIDARG;
        HRESULT hr = package_current_user_only(user);
        if (FAILED(hr)) return hr;
        hr = package_fresh_inventory_absent();
        if (FAILED(hr)) {
            std::fprintf(stderr, "PackageManager compatibility: FindPackagesByUserSecurityIdPackageFamilyName inventory not proven empty (0x%08lx)\n", static_cast<unsigned long>(hr));
            return hr;
        }
        EmptyPackageIterable *packages = new(std::nothrow) EmptyPackageIterable;
        if (!packages) return E_OUTOFMEMORY;
        *out = static_cast<PackageIterable *>(packages);
        std::fprintf(stderr, "PackageManager compatibility: FindPackagesByUserSecurityIdPackageFamilyName verified absent package repositories; empty current-user inventory\n");
        return S_OK;
    }
    NO_RESULT(FindPackageByUserSecurityIdPackageFullName, (HSTRING, HSTRING, void **out))
    NO_RESULT(RemovePackageWithOptionsAsync, (HSTRING, UINT32, void **out))
    NO_RESULT(StagePackageWithOptionsAsync, (void *, void *, UINT32, void **out))
    NO_RESULT(RegisterPackageByFullNameAsync, (HSTRING, void *, UINT32, void **out))
    NO_RESULT(FindPackagesWithPackageTypes, (UINT32, void **out))
    NO_RESULT(FindPackagesByUserSecurityIdWithPackageTypes, (HSTRING, UINT32, void **out))
    NO_RESULT(FindPackagesByNamePublisherWithPackageTypes, (HSTRING, HSTRING, UINT32, void **out))
    NO_RESULT(FindPackagesByUserSecurityIdNamePublisherWithPackageTypes, (HSTRING, HSTRING, HSTRING, UINT32, void **out))
    NO_RESULT(FindPackagesByPackageFamilyNameWithPackageTypes, (HSTRING, UINT32, void **out))
    NO_RESULT(FindPackagesByUserSecurityIdPackageFamilyNameWithPackageTypes, (HSTRING, HSTRING, UINT32, void **out))
    NO_RESULT(StageUserDataAsync, (HSTRING, void **out))
    NO_RESULT(AddPackageVolumeAsync, (HSTRING, void **out))
    NO_RESULT(AddPackageToVolumeAsync, (void *, void *, UINT32, void *, void **out))
    NO_OUT(ClearPackageStatus, (HSTRING, UINT32))
    NO_RESULT(RegisterPackageWithAppDataVolumeAsync, (void *, void *, UINT32, void *, void **out))
    NO_RESULT(FindPackageVolumes, (void **out))
    NO_RESULT(MovePackageToVolumeAsync, (HSTRING, UINT32, void *, void **out))
    NO_RESULT(RemovePackageVolumeAsync, (void *, void **out))
    NO_OUT(SetDefaultPackageVolume, (void *))
    NO_OUT(SetPackageStatus, (HSTRING, UINT32))
    NO_RESULT(SetPackageVolumeOfflineAsync, (void *, void **out))
    NO_RESULT(SetPackageVolumeOnlineAsync, (void *, void **out))
    NO_RESULT(StagePackageToVolumeAsync, (void *, void *, UINT32, void *, void **out))
    NO_RESULT(StageUserDataWithOptionsAsync, (HSTRING, UINT32, void **out))
};

class Factory final : public IActivationFactory {
    LONG refs = 1;
public:
    Factory() { InterlockedIncrement(&live_objects); }
    ~Factory() { InterlockedDecrement(&live_objects); }
    UNKNOWN_BODY(manager_name)
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!IsEqualGUID(iid, IID_IUnknown) && !IsEqualGUID(iid, IID_IInspectable) &&
            !IsEqualGUID(iid, IID_IActivationFactory)) return E_NOINTERFACE;
        *out = static_cast<IActivationFactory *>(this);
        AddRef(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG *count, IID **out) override {
        return copy_iids(&IID_IActivationFactory, 1, count, out);
    }
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        Manager *manager = new(std::nothrow) Manager;
        if (!manager) return E_OUTOFMEMORY;
        *out = static_cast<PackageManager1 *>(manager);
        return S_OK;
    }
};

extern "C" __declspec(dllexport) HRESULT WINAPI DllGetActivationFactory(
    HSTRING name, IActivationFactory **out)
{
    if (!out) return E_POINTER;
    *out = nullptr;
    UINT32 length;
    const WCHAR *requested = WindowsGetStringRawBuffer(name, &length);
    if (length != wcslen(manager_name) ||
        wmemcmp(requested, manager_name, length)) return CLASS_E_CLASSNOTAVAILABLE;
    *out = new(std::nothrow) Factory;
    return *out ? S_OK : E_OUTOFMEMORY;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllCanUnloadNow()
{
    return InterlockedCompareExchange(&live_objects, 0, 0) ? S_FALSE : S_OK;
}
