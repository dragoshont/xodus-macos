#ifndef XODUS_PACKAGE_MANAGER_STORE_ABI_H
#define XODUS_PACKAGE_MANAGER_STORE_ABI_H

#include "package-manager-abi.h"

/*
 * Extracted without activation from the genuine Windows.ApplicationModel.winmd
 * already staged for the original installer. Exact SHA256 and method/slot dump
 * are recorded in package-manager-store-notes.md. This is a native-client
 * diagnostic contract, not a replacement Store implementation.
 */
static const GUID STORE_MANAGER1_IID = {0x9353e170,0x8441,0x4b45,{0xbd,0x72,0x7c,0x2f,0xa9,0x25,0xbe,0xee}};
static const GUID STORE_MANAGER6_IID = {0xc9e7d408,0xf27a,0x4471,{0xb2,0xf4,0xe7,0x6e,0xfc,0xbe,0xbc,0xca}};

#define STORE_METHOD(name, args) virtual HRESULT STDMETHODCALLTYPE name args = 0
struct StoreManager1 : IInspectable {
    STORE_METHOD(get_AppInstallItems, (void **));
    STORE_METHOD(Cancel, (HSTRING));
    STORE_METHOD(Pause, (HSTRING));
    STORE_METHOD(Restart, (HSTRING));
    STORE_METHOD(add_ItemCompleted, (void *, INT64 *));
    STORE_METHOD(remove_ItemCompleted, (INT64));
    STORE_METHOD(add_ItemStatusChanged, (void *, INT64 *));
    STORE_METHOD(remove_ItemStatusChanged, (INT64));
    STORE_METHOD(get_AutoUpdateSetting, (INT32 *));
    STORE_METHOD(put_AutoUpdateSetting, (INT32));
    STORE_METHOD(get_AcquisitionIdentity, (HSTRING *));
    STORE_METHOD(put_AcquisitionIdentity, (HSTRING));
    STORE_METHOD(GetIsApplicableAsync, (HSTRING, HSTRING, void **));
    STORE_METHOD(StartAppInstallAsync, (HSTRING, HSTRING, BYTE, BYTE, void **));
    STORE_METHOD(UpdateAppByPackageFamilyNameAsync, (HSTRING, void **));
    STORE_METHOD(SearchForUpdatesAsync, (HSTRING, HSTRING, void **));
    STORE_METHOD(SearchForAllUpdatesAsync, (void **));
    STORE_METHOD(IsStoreBlockedByPolicyAsync, (HSTRING, HSTRING, void **));
    STORE_METHOD(GetIsAppAllowedToInstallAsync, (HSTRING, void **));
};
struct StoreManager6 : IInspectable {
    STORE_METHOD(SearchForAllUpdatesAsync, (HSTRING, HSTRING, void *, void **));
    STORE_METHOD(SearchForAllUpdatesForUserAsync, (void *, HSTRING, HSTRING, void *, void **));
    STORE_METHOD(SearchForUpdatesAsync, (HSTRING, HSTRING, HSTRING, HSTRING, void *, void **));
    STORE_METHOD(SearchForUpdatesForUserAsync, (void *, HSTRING, HSTRING, HSTRING, HSTRING, void *, void **));
    STORE_METHOD(StartProductInstallAsync, (HSTRING, HSTRING, HSTRING, HSTRING, void *, void **));
    STORE_METHOD(StartProductInstallForUserAsync, (void *, HSTRING, HSTRING, HSTRING, HSTRING, void *, void **));
    STORE_METHOD(GetIsPackageIdentityAllowedToInstallAsync, (HSTRING, HSTRING, HSTRING, void **));
    STORE_METHOD(GetIsPackageIdentityAllowedToInstallForUserAsync, (void *, HSTRING, HSTRING, HSTRING, void **));
};
#undef STORE_METHOD
#endif
