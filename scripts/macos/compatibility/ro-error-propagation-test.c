#define COBJMACROS
#include <stdio.h>
#include "windows.h"
#include "roerrorapi.h"
#include "winstring.h"

static unsigned checks, failures;
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; printf("FAIL line %u: %s\n", __LINE__, #expr); } } while (0)

int main(void)
{
    HMODULE module = LoadLibraryW(L"combase.dll");
    WCHAR path[MAX_PATH], bounded[] = { 'x', 'y' };
    HSTRING message = NULL;
    IRestrictedErrorInfo *info = (void *)1;
    BOOL (WINAPI *enabled)(void);
    BOOL (WINAPI *transform)(HRESULT, HRESULT, HSTRING);
    BOOL (WINAPI *transform_w)(HRESULT, HRESULT, UINT, const WCHAR *);
    HRESULT (WINAPI *matching)(HRESULT, IRestrictedErrorInfo **);
    HRESULT (WINAPI *report)(IUnknown *, IRestrictedErrorInfo *);
    HRESULT (WINAPI *cancel)(DWORD, ULONG);

    CHECK(module != NULL);
    if (!module) goto done;
    CHECK(GetModuleFileNameW(module, path, MAX_PATH) != 0);
    printf("ERROR_PROVIDER=%ls\n", path);
    enabled = (void *)GetProcAddress(module, "IsErrorPropagationEnabled");
    transform = (void *)GetProcAddress(module, "RoTransformError");
    transform_w = (void *)GetProcAddress(module, "RoTransformErrorW");
    matching = (void *)GetProcAddress(module, "RoGetMatchingRestrictedErrorInfo");
    report = (void *)GetProcAddress(module, "RoReportFailedDelegate");
    cancel = (void *)GetProcAddress(module, "CoCancelCall");
    CHECK(enabled && transform && transform_w && matching && report && cancel);
    if (!enabled || !transform || !transform_w || !matching || !report || !cancel) goto cleanup;
    CHECK(!enabled());
    CHECK(WindowsCreateString(L"test", 4, &message) == S_OK);
    CHECK(!transform(E_FAIL, E_INVALIDARG, message));
    CHECK(!transform(S_OK, S_OK, NULL));
    CHECK(!transform_w(E_FAIL, E_INVALIDARG, 2, bounded));
    CHECK(!transform_w(S_OK, S_OK, 0, NULL));
    CHECK(matching(E_FAIL, &info) == E_NOTIMPL && !info);
    CHECK(matching(E_FAIL, NULL) == E_POINTER);
    CHECK(report(NULL, NULL) == E_NOTIMPL);
    CHECK(cancel(GetCurrentThreadId(), 0) == CO_E_CANCEL_DISABLED);
    CHECK(!RoOriginateErrorW(E_ILLEGAL_METHOD_CALL, 2, bounded));
cleanup:
    WindowsDeleteString(message);
    FreeLibrary(module);
done:
    printf("error_propagation_checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
