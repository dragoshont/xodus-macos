#ifndef XODUS_PACKAGE_MANAGER_ABI_H
#define XODUS_PACKAGE_MANAGER_ABI_H

#include <windows.h>
#include <inspectable.h>
#include <activation.h>
#include <winstring.h>

/*
 * ABI source: Microsoft windows-rs Windows/Management/Deployment/mod.rs.
 * The source SHA256 and retrieval date are recorded in package-manager-notes.md.
 * These interfaces derive separately from IInspectable, not from each other.
 * Object/collection/URI/async pointers are opaque because unsupported methods
 * never dereference them; enums retain their metadata-defined 32-bit width.
 */
static const GUID PM1_IID = {0x9a7d4b65,0x5e8f,0x4fc7,{0xa2,0xe5,0x7f,0x69,0x25,0xcb,0x8b,0x53}};
static const GUID PM2_IID = {0xf7aad08d,0x0840,0x46f2,{0xb5,0xd8,0xca,0xd4,0x76,0x93,0xa0,0x95}};
static const GUID PM3_IID = {0xdaad9948,0x36f1,0x41a7,{0x91,0x88,0xbc,0x26,0x3e,0x0d,0xcb,0x72}};
static const GUID PV1_IID = {0xcf2672c3,0x1a40,0x4450,{0x97,0x39,0x2a,0xce,0x2e,0x89,0x88,0x53}};
static const GUID PV2_IID = {0x46abcf2e,0x9dd4,0x47a2,{0xab,0x8c,0xc6,0x40,0x83,0x49,0xbc,0xd8}};

#define PM_METHOD(name, args) virtual HRESULT STDMETHODCALLTYPE name args = 0
struct PackageManager1 : IInspectable {
    PM_METHOD(AddPackageAsync, (void *, void *, UINT32, void **));
    PM_METHOD(UpdatePackageAsync, (void *, void *, UINT32, void **));
    PM_METHOD(RemovePackageAsync, (HSTRING, void **));
    PM_METHOD(StagePackageAsync, (void *, void *, void **));
    PM_METHOD(RegisterPackageAsync, (void *, void *, UINT32, void **));
    PM_METHOD(FindPackages, (void **));
    PM_METHOD(FindPackagesByUserSecurityId, (HSTRING, void **));
    PM_METHOD(FindPackagesByNamePublisher, (HSTRING, HSTRING, void **));
    PM_METHOD(FindPackagesByUserSecurityIdNamePublisher, (HSTRING, HSTRING, HSTRING, void **));
    PM_METHOD(FindUsers, (HSTRING, void **));
    PM_METHOD(SetPackageState, (HSTRING, INT32));
    PM_METHOD(FindPackageByPackageFullName, (HSTRING, void **));
    PM_METHOD(CleanupPackageForUserAsync, (HSTRING, HSTRING, void **));
    PM_METHOD(FindPackagesByPackageFamilyName, (HSTRING, void **));
    PM_METHOD(FindPackagesByUserSecurityIdPackageFamilyName, (HSTRING, HSTRING, void **));
    PM_METHOD(FindPackageByUserSecurityIdPackageFullName, (HSTRING, HSTRING, void **));
};
struct PackageManager2 : IInspectable {
    PM_METHOD(RemovePackageWithOptionsAsync, (HSTRING, UINT32, void **));
    PM_METHOD(StagePackageWithOptionsAsync, (void *, void *, UINT32, void **));
    PM_METHOD(RegisterPackageByFullNameAsync, (HSTRING, void *, UINT32, void **));
    PM_METHOD(FindPackagesWithPackageTypes, (UINT32, void **));
    PM_METHOD(FindPackagesByUserSecurityIdWithPackageTypes, (HSTRING, UINT32, void **));
    PM_METHOD(FindPackagesByNamePublisherWithPackageTypes, (HSTRING, HSTRING, UINT32, void **));
    PM_METHOD(FindPackagesByUserSecurityIdNamePublisherWithPackageTypes, (HSTRING, HSTRING, HSTRING, UINT32, void **));
    PM_METHOD(FindPackagesByPackageFamilyNameWithPackageTypes, (HSTRING, UINT32, void **));
    PM_METHOD(FindPackagesByUserSecurityIdPackageFamilyNameWithPackageTypes, (HSTRING, HSTRING, UINT32, void **));
    PM_METHOD(StageUserDataAsync, (HSTRING, void **));
};
struct PackageManager3 : IInspectable {
    PM_METHOD(AddPackageVolumeAsync, (HSTRING, void **));
    PM_METHOD(AddPackageToVolumeAsync, (void *, void *, UINT32, void *, void **));
    PM_METHOD(ClearPackageStatus, (HSTRING, UINT32));
    PM_METHOD(RegisterPackageWithAppDataVolumeAsync, (void *, void *, UINT32, void *, void **));
    PM_METHOD(FindPackageVolumeByName, (HSTRING, void **));
    PM_METHOD(FindPackageVolumes, (void **));
    PM_METHOD(GetDefaultPackageVolume, (void **));
    PM_METHOD(MovePackageToVolumeAsync, (HSTRING, UINT32, void *, void **));
    PM_METHOD(RemovePackageVolumeAsync, (void *, void **));
    PM_METHOD(SetDefaultPackageVolume, (void *));
    PM_METHOD(SetPackageStatus, (HSTRING, UINT32));
    PM_METHOD(SetPackageVolumeOfflineAsync, (void *, void **));
    PM_METHOD(SetPackageVolumeOnlineAsync, (void *, void **));
    PM_METHOD(StagePackageToVolumeAsync, (void *, void *, UINT32, void *, void **));
    PM_METHOD(StageUserDataWithOptionsAsync, (HSTRING, UINT32, void **));
};
struct PackageVolume1 : IInspectable {
    PM_METHOD(get_IsOffline, (BYTE *));
    PM_METHOD(get_IsSystemVolume, (BYTE *));
    PM_METHOD(get_MountPoint, (HSTRING *));
    PM_METHOD(get_Name, (HSTRING *));
    PM_METHOD(get_PackageStorePath, (HSTRING *));
    PM_METHOD(get_SupportsHardLinks, (BYTE *));
    PM_METHOD(FindPackages, (void **));
    PM_METHOD(FindPackagesByNamePublisher, (HSTRING, HSTRING, void **));
    PM_METHOD(FindPackagesByPackageFamilyName, (HSTRING, void **));
    PM_METHOD(FindPackagesWithPackageTypes, (UINT32, void **));
    PM_METHOD(FindPackagesByNamePublisherWithPackagesTypes, (UINT32, HSTRING, HSTRING, void **));
    PM_METHOD(FindPackagesByPackageFamilyNameWithPackageTypes, (UINT32, HSTRING, void **));
    PM_METHOD(FindPackageByPackageFullName, (HSTRING, void **));
    PM_METHOD(FindPackagesByUserSecurityId, (HSTRING, void **));
    PM_METHOD(FindPackagesByUserSecurityIdNamePublisher, (HSTRING, HSTRING, HSTRING, void **));
    PM_METHOD(FindPackagesByUserSecurityIdPackageFamilyName, (HSTRING, HSTRING, void **));
    PM_METHOD(FindPackagesByUserSecurityIdWithPackageTypes, (HSTRING, UINT32, void **));
    PM_METHOD(FindPackagesByUserSecurityIdNamePublisherWithPackageTypes, (HSTRING, UINT32, HSTRING, HSTRING, void **));
    PM_METHOD(FindPackagesByUserSecurityIdPackageFamilyNameWithPackagesTypes, (HSTRING, UINT32, HSTRING, void **));
    PM_METHOD(FindPackageByUserSecurityIdPackageFullName, (HSTRING, HSTRING, void **));
};
struct PackageVolume2 : IInspectable {
    PM_METHOD(get_IsFullTrustPackageSupported, (BYTE *));
    PM_METHOD(get_IsAppxInstallSupported, (BYTE *));
    PM_METHOD(GetAvailableSpaceAsync, (void **));
};
static const GUID SPACE_IID = {0x2a70d630,0x0767,0x5f0a,{0xa1,0xc2,0xde,0xb0,0x81,0x26,0xe2,0x6e}};
static const GUID ASYNC_INFO_IID = {0x36,0,0,{0xc0,0,0,0,0,0,0,0x46}};
struct SpaceCompletedHandler : IUnknown {
    PM_METHOD(Invoke, (void *, INT32));
};
struct SpaceOperation : IInspectable {
    PM_METHOD(put_Completed, (SpaceCompletedHandler *));
    PM_METHOD(get_Completed, (SpaceCompletedHandler **));
    PM_METHOD(GetResults, (UINT64 *));
};
struct SpaceAsyncInfo : IInspectable {
    PM_METHOD(get_Id, (UINT32 *));
    PM_METHOD(get_Status, (INT32 *));
    PM_METHOD(get_ErrorCode, (HRESULT *));
    PM_METHOD(Cancel, ());
    PM_METHOD(Close, ());
};
static const GUID PACKAGE_ITERABLE_IID = {0x69ad6aa7,0x0c49,0x5f27,{0xa5,0xeb,0xef,0x4d,0x59,0x46,0x7b,0x6d}};
static const GUID PACKAGE_ITERATOR_IID = {0x0217f069,0x025c,0x5ee6,{0xa8,0x7f,0xe7,0x82,0xe3,0xb6,0x23,0xae}};
struct PackageIterator : IInspectable {
    PM_METHOD(get_Current, (void **));
    PM_METHOD(get_HasCurrent, (BYTE *));
    PM_METHOD(MoveNext, (BYTE *));
    PM_METHOD(GetMany, (UINT32, void **, UINT32 *));
};
struct PackageIterable : IInspectable {
    PM_METHOD(First, (PackageIterator **));
};
#undef PM_METHOD
#endif
