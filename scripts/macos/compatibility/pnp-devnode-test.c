#include <stdio.h>
#include "windows.h"
#include "cfgmgr32.h"
#include "setupapi.h"

CONFIGRET WINAPI test_list_size(ULONG *, const WCHAR *, ULONG);
CONFIGRET WINAPI test_list(const WCHAR *, WCHAR *, ULONG, ULONG);
CONFIGRET WINAPI test_locate(DEVINST *, WCHAR *, ULONG);
CONFIGRET WINAPI test_property(DEVINST, const DEVPROPKEY *, DEVPROPTYPE *, void *, ULONG *, ULONG);
#define CM_Get_Device_ID_List_SizeW test_list_size
#define CM_Get_Device_ID_ListW test_list
#define CM_Locate_DevNodeW test_locate
#define CM_Get_DevNode_PropertyW test_property
#include "cfgmgr32/main.c"
#undef CM_Get_Device_ID_List_SizeW
#undef CM_Get_Device_ID_ListW
#undef CM_Locate_DevNodeW
#undef CM_Get_DevNode_PropertyW

static unsigned checks, failures, list_calls, property_calls, mode;
static const WCHAR fixture_id[] = L"ROOT\\XODUS_SYNTHETIC\\0000";
static const GUID fixture_guid = {0x817f497c, 0xa771, 0x4140, {0x86, 0x0b, 0xd7, 0x67, 0x5d, 0x82, 0x9a, 0x57}};
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; printf("FAIL %u: %s\n", __LINE__, #expr); } } while (0)

CONFIGRET WINAPI test_list_size(ULONG *size, const WCHAR *filter, ULONG flags)
{
    CHECK(!filter && flags == CM_GETIDLIST_FILTER_NONE);
    *size = ARRAY_SIZE(fixture_id) + 1;
    return mode == 1 ? CR_ACCESS_DENIED : CR_SUCCESS;
}

CONFIGRET WINAPI test_list(const WCHAR *filter, WCHAR *ids, ULONG size, ULONG flags)
{
    CHECK(!filter && flags == CM_GETIDLIST_FILTER_NONE && size == ARRAY_SIZE(fixture_id) + 1);
    if (++list_calls == 1) return CR_BUFFER_SMALL;
    memcpy(ids, fixture_id, sizeof(fixture_id));
    ids[ARRAY_SIZE(fixture_id)] = 0;
    return CR_SUCCESS;
}

CONFIGRET WINAPI test_locate(DEVINST *node, WCHAR *id, ULONG flags)
{
    CHECK(!flags && !wcscmp(id, fixture_id));
    *node = 42;
    return CR_SUCCESS;
}

CONFIGRET WINAPI test_property(DEVINST node, const DEVPROPKEY *key, DEVPROPTYPE *type,
                               void *buffer, ULONG *size, ULONG flags)
{
    CHECK(node == 42 && !flags);
    if (mode == 2) return CR_ACCESS_DENIED;
    if (!IsEqualDevPropKey(*key, DEVPKEY_Device_ClassGuid))
    {
        *size = 0;
        return CR_NO_SUCH_VALUE;
    }
    ++property_calls;
    *type = DEVPROP_TYPE_GUID;
    if (!buffer)
    {
        *size = 4;
        return CR_BUFFER_SMALL;
    }
    if (mode == 3 || *size < sizeof(fixture_guid))
    {
        *size = sizeof(fixture_guid);
        return CR_BUFFER_SMALL;
    }
    memcpy(buffer, &fixture_guid, sizeof(fixture_guid));
    *size = sizeof(fixture_guid);
    return CR_SUCCESS;
}

int main(void)
{
    DEVPROPCOMPKEY keys[] = {{DEVPKEY_Device_ClassGuid, DEVPROP_STORE_SYSTEM, NULL},
                            {DEVPKEY_Device_DriverVersion, DEVPROP_STORE_SYSTEM, NULL}};
    const DEV_OBJECT *objects = NULL;
    ULONG count = 0;
    HRESULT hr;

    hr = DevGetObjects(DevObjectTypeDevice, 0, ARRAY_SIZE(keys), keys, 0, NULL, &count, &objects);
    CHECK(hr == S_OK && count == 1 && objects);
    CHECK(list_calls == 2 && property_calls == 3);
    if (objects && count)
    {
        CHECK(!wcscmp(objects[0].pszObjectId, fixture_id));
        CHECK(objects[0].cPropertyCount == 2);
        CHECK(objects[0].pProperties[0].Type == DEVPROP_TYPE_GUID &&
              objects[0].pProperties[0].BufferSize == sizeof(GUID) &&
              IsEqualGUID(objects[0].pProperties[0].Buffer, &fixture_guid));
        CHECK(objects[0].pProperties[1].Type == DEVPROP_TYPE_EMPTY &&
              !objects[0].pProperties[1].Buffer && !objects[0].pProperties[1].BufferSize);
        DevFreeObjects(count, objects);
    }
    for (mode = 1; mode <= 3; ++mode)
    {
        HRESULT expected = HRESULT_FROM_WIN32(mode == 3 ? ERROR_INSUFFICIENT_BUFFER : ERROR_ACCESS_DENIED);
        list_calls = property_calls = 0;
        hr = DevGetObjects(DevObjectTypeDevice, 0, ARRAY_SIZE(keys), keys, 0, NULL, &count, &objects);
        CHECK(hr == expected && !count && !objects);
        if (mode == 3) CHECK(property_calls == 4);
    }
    printf("Device devnode retry/error test: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
