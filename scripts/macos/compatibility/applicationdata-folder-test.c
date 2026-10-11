#define COBJMACROS
#include <stdio.h>
#include <wchar.h>
#include "windows.h"
#include "roapi.h"
#include "winstring.h"
#include "appmodel.h"
#include "shlobj.h"
#include "initguid.h"
DEFINE_GUID(IID_IInspectable, 0xaf86e2e0, 0xb12d, 0x4c6a, 0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90);
HRESULT WINAPI test_SHGetKnownFolderPath(REFKNOWNFOLDERID id, DWORD flags, HANDLE token, WCHAR **out);
#define GetCurrentPackageFamilyName test_GetCurrentPackageFamilyName
#define SHGetKnownFolderPath test_SHGetKnownFolderPath
#include "storagefolder.c"
#undef GetCurrentPackageFamilyName
#undef SHGetKnownFolderPath

LONG WINAPI GetCurrentPackageFamilyName(UINT32 *length, WCHAR *value);

static unsigned checks, failures;
static LONG identity_error;
static WCHAR test_root[MAX_PATH];
static const WCHAR family[] = L"Xodus.ApplicationDataFolderTest_123456789abcd";
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; printf("FAIL line %u: %s\n", __LINE__, #expr); } } while (0)

LONG WINAPI test_GetCurrentPackageFamilyName(UINT32 *length, WCHAR *value)
{
    UINT32 required = ARRAY_SIZE(family);
    if (identity_error) return identity_error;
    if (*length < required || !value)
    {
        *length = required;
        return ERROR_INSUFFICIENT_BUFFER;
    }
    memcpy(value, family, sizeof(family));
    *length = required;
    return ERROR_SUCCESS;
}

HRESULT WINAPI test_SHGetKnownFolderPath(REFKNOWNFOLDERID id, DWORD flags, HANDLE token, WCHAR **out)
{
    SIZE_T bytes = (wcslen(test_root) + 1) * sizeof(WCHAR);
    (void)token;
    CHECK(IsEqualGUID(id, &FOLDERID_LocalAppData) && flags == KF_FLAG_CREATE);
    if (!(*out = CoTaskMemAlloc(bytes))) return E_OUTOFMEMORY;
    memcpy(*out, test_root, bytes);
    return S_OK;
}

static void test_leaf(const WCHAR *leaf)
{
    IStorageFolder *folder = NULL, *again = NULL;
    IStorageItem *item = NULL;
    IUnknown *identity = NULL, *agile = NULL;
    WCHAR expected[MAX_PATH];
    HSTRING path = NULL, name = NULL;
    DWORD attributes;
    HRESULT hr;

    swprintf(expected, ARRAY_SIZE(expected), L"%s\\Packages\\%s\\%s", test_root, family, leaf);
    CHECK(application_data_folder_create(leaf, NULL) == E_INVALIDARG);
    hr = application_data_folder_create(leaf, &folder);
    CHECK(hr == S_OK && folder != NULL);
    if (FAILED(hr) || !folder) return;
    CHECK(IStorageFolder_QueryInterface(folder, &IID_IStorageItem, (void **)&item) == S_OK);
    if (item)
    {
        CHECK(IStorageItem_get_Path(item, NULL) == E_POINTER);
        CHECK(IStorageItem_get_Name(item, NULL) == E_POINTER);
        CHECK(IStorageItem_get_Path(item, &path) == S_OK);
        CHECK(path && !wcscmp(WindowsGetStringRawBuffer(path, NULL), expected));
        CHECK(IStorageItem_get_Name(item, &name) == S_OK);
        CHECK(name && !wcscmp(WindowsGetStringRawBuffer(name, NULL), leaf));
        CHECK(IStorageItem_QueryInterface(item, &IID_IUnknown, (void **)&identity) == S_OK);
        CHECK(identity == (IUnknown *)folder);
        if (identity) CHECK(IUnknown_Release(identity) == 2);
        CHECK(IStorageItem_Release(item) == 1);
    }
    CHECK(IStorageFolder_QueryInterface(folder, &IID_IAgileObject, (void **)&agile) == S_OK);
    if (agile) CHECK(IUnknown_Release(agile) == 1);
    attributes = GetFileAttributesW(expected);
    CHECK(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY));
    CHECK(application_data_folder_create(leaf, &again) == S_OK && again != NULL);
    if (again) CHECK(IStorageFolder_Release(again) == 0);
    CHECK(IStorageFolder_Release(folder) == 0);
    /* Returned strings own their references after the folder is destroyed. */
    CHECK(path && !wcscmp(WindowsGetStringRawBuffer(path, NULL), expected));
    WindowsDeleteString(path);
    WindowsDeleteString(name);
    CHECK(RemoveDirectoryW(expected));
}

static void test_unpacked_provider(void)
{
    typedef HRESULT (WINAPI *activation_fn)(HSTRING, IActivationFactory **);
    HMODULE module = LoadLibraryW(L"windows.storage.applicationdata.dll");
    IActivationFactory *factory = NULL;
    IApplicationDataStatics *statics = NULL;
    IApplicationData *data = NULL;
    IStorageFolder *folder;
    HSTRING name = NULL;
    activation_fn activate;
    HRESULT hr;
    UINT32 length = 0;

    CHECK(GetCurrentPackageFamilyName(&length, NULL) == APPMODEL_ERROR_NO_PACKAGE);
    CHECK(module != NULL);
    if (!module) return;
    activate = (activation_fn)(void *)GetProcAddress(module, "DllGetActivationFactory");
    CHECK(activate != NULL);
    if (!activate) goto done;
    hr = WindowsCreateString(L"Windows.Storage.ApplicationData", 31, &name);
    CHECK(hr == S_OK);
    if (FAILED(hr)) goto done;
    hr = activate(name, &factory);
    CHECK(hr == S_OK && factory != NULL);
    if (FAILED(hr) || !factory) goto done;
    hr = IActivationFactory_QueryInterface(factory, &IID_IApplicationDataStatics, (void **)&statics);
    CHECK(hr == S_OK && statics != NULL);
    if (FAILED(hr) || !statics) goto done;
    hr = IApplicationDataStatics_get_Current(statics, &data);
    CHECK(hr == S_OK && data != NULL);
    if (FAILED(hr) || !data) goto done;
    folder = (IStorageFolder *)0xdeadbeef;
    CHECK(IApplicationData_get_LocalFolder(data, &folder) == HRESULT_FROM_WIN32(APPMODEL_ERROR_NO_PACKAGE));
    CHECK(folder == NULL);
    folder = (IStorageFolder *)0xdeadbeef;
    CHECK(IApplicationData_get_RoamingFolder(data, &folder) == HRESULT_FROM_WIN32(APPMODEL_ERROR_NO_PACKAGE));
    CHECK(folder == NULL);
    folder = (IStorageFolder *)0xdeadbeef;
    CHECK(IApplicationData_get_TemporaryFolder(data, &folder) == HRESULT_FROM_WIN32(APPMODEL_ERROR_NO_PACKAGE));
    CHECK(folder == NULL);
    CHECK(IApplicationData_get_LocalFolder(data, NULL) == E_INVALIDARG);
    CHECK(IApplicationData_get_RoamingFolder(data, NULL) == E_INVALIDARG);
    CHECK(IApplicationData_get_TemporaryFolder(data, NULL) == E_INVALIDARG);
done:
    if (data) CHECK(IApplicationData_Release(data) == 0);
    if (statics) IApplicationDataStatics_Release(statics);
    if (factory) IActivationFactory_Release(factory);
    WindowsDeleteString(name);
    FreeLibrary(module);
}

int main(void)
{
    WCHAR temporary[MAX_PATH], packages[MAX_PATH], package[MAX_PATH];
    IStorageFolder *folder = (IStorageFolder *)0xdeadbeef;
    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);

    CHECK(SUCCEEDED(hr));
    if (FAILED(hr)) goto done;
    CHECK(GetTempPathW(ARRAY_SIZE(temporary), temporary) != 0);
    CHECK(GetTempFileNameW(temporary, L"xaf", 0, test_root) != 0);
    CHECK(DeleteFileW(test_root));
    CHECK(CreateDirectoryW(test_root, NULL));
    identity_error = APPMODEL_ERROR_NO_PACKAGE;
    CHECK(application_data_folder_create(L"TempState", &folder) == HRESULT_FROM_WIN32(identity_error));
    CHECK(folder == NULL);
    identity_error = 0;
    test_leaf(L"LocalState");
    test_leaf(L"RoamingState");
    test_leaf(L"TempState");
    swprintf(package, ARRAY_SIZE(package), L"%s\\Packages\\%s", test_root, family);
    swprintf(packages, ARRAY_SIZE(packages), L"%s\\Packages", test_root);
    CHECK(RemoveDirectoryW(package));
    CHECK(RemoveDirectoryW(packages));
    CHECK(RemoveDirectoryW(test_root));
    test_unpacked_provider();
    RoUninitialize();
done:
    printf("applicationdata-folders: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
