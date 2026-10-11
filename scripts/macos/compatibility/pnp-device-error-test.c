#include <stdio.h>
#include "initguid.h"
#include "private.h"
#include "devquery.h"
#undef INITGUID
#include "roapi.h"

static HRESULT backend_error;
static unsigned backend_calls;
HRESULT WINAPI test_DevGetObjects(DEV_OBJECT_TYPE type, ULONG flags, ULONG count, const DEVPROPCOMPKEY *keys,
    ULONG filters_count, const DEVPROP_FILTER_EXPRESSION *filters, ULONG *length, const DEV_OBJECT **objects);
#define DevGetObjects test_DevGetObjects
#include "pnp.c"
#undef DevGetObjects

static LONG checks, failures;
#define CHECK(expr) do { InterlockedIncrement(&checks); if (!(expr)) { InterlockedIncrement(&failures); printf("FAIL %u: %s\n", __LINE__, #expr); } } while (0)

HRESULT WINAPI test_DevGetObjects(DEV_OBJECT_TYPE type, ULONG flags, ULONG count, const DEVPROPCOMPKEY *keys,
    ULONG filters_count, const DEVPROP_FILTER_EXPRESSION *filters, ULONG *length, const DEV_OBJECT **objects)
{
    CHECK(type == DevObjectTypeDevice && !flags && !count && !keys && !filters_count && !filters);
    ++backend_calls;
    *length = 0;
    *objects = NULL;
    return backend_error;
}

static void test_async_error(HRESULT expected)
{
    struct pnp_request *request = calloc(1, sizeof(*request));
    IAsyncOperation_PnpObjectCollection *operation = NULL;
    IVectorView_PnpObject *view = NULL;
    IAsyncInfo *info = NULL;
    AsyncStatus status = Started;
    HRESULT error = S_OK, hr;
    unsigned i;

    CHECK(request != NULL);
    if (!request) return;
    request->IUnknown_iface.lpVtbl = &request_vtbl;
    request->ref = 1;
    backend_error = expected;
    hr = async_operation_inspectable_create(&IID_IAsyncOperation_PnpObjectCollection,
        (IUnknown *)pnp_object_factory, &request->IUnknown_iface, find_all_async,
        (IAsyncOperation_IInspectable **)&operation);
    IUnknown_Release(&request->IUnknown_iface);
    CHECK(hr == S_OK && operation);
    if (FAILED(hr)) return;
    CHECK(IAsyncOperation_PnpObjectCollection_QueryInterface(operation, &IID_IAsyncInfo, (void **)&info) == S_OK);
    if (info)
    {
        for (i = 0; i < 500; ++i)
        {
            CHECK(IAsyncInfo_get_Status(info, &status) == S_OK);
            if (status != Started) break;
            Sleep(10);
        }
        CHECK(status == Error);
        CHECK(IAsyncInfo_get_ErrorCode(info, &error) == S_OK && error == expected);
        CHECK(IAsyncOperation_PnpObjectCollection_GetResults(operation, &view) == expected && !view);
        CHECK(IAsyncInfo_Close(info) == S_OK);
        IAsyncInfo_Release(info);
    }
    IAsyncOperation_PnpObjectCollection_Release(operation);
    for (i = 0; i < 500 && InterlockedCompareExchange(&factory.ref, 1, 1) != 1; ++i) Sleep(10);
    CHECK(factory.ref == 1);
}

int main(void)
{
    CHECK(RoInitialize(RO_INIT_MULTITHREADED) == S_OK);
    test_async_error(E_ACCESSDENIED);
    test_async_error(HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));
    CHECK(backend_calls == 2);
    RoUninitialize();
    printf("Pnp backend-error async test: %ld checks, %ld failures\n", checks, failures);
    return failures ? 1 : 0;
}
