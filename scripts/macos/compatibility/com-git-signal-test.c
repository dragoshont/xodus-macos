#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>

typedef void (WINAPI *SIGNAL)(void);
static unsigned checks, failures;

static void check(BOOL valid, const char *name)
{
    checks++;
    if (!valid) { failures++; printf("FAIL %s\n", name); }
}

int main(void)
{
    HMODULE module = LoadLibraryW(L"combase.dll");
    SIGNAL signal = module ? (SIGNAL)(ULONG_PTR)GetProcAddress(module, (LPCSTR)95) : NULL;
    unsigned state;

    check(signal != NULL, "exact private void export present");
    if (!signal) goto done;
    signal();
    check(TRUE, "no-apartment empty-state signal returns");
    for (state = 0; state < 2; state++)
    {
        IGlobalInterfaceTable *git = NULL;
        IStream *stream = NULL;
        IUnknown *resolved = NULL;
        DWORD cookie = 0;
        HRESULT hr = CoInitializeEx(NULL, state ? COINIT_MULTITHREADED : COINIT_APARTMENTTHREADED);

        check(SUCCEEDED(hr), "real apartment initialized");
        if (FAILED(hr)) continue;
        signal();
        check(TRUE, "initialized empty-state signal returns");
        hr = CoCreateInstance(&CLSID_StdGlobalInterfaceTable, NULL, CLSCTX_INPROC_SERVER,
                              &IID_IGlobalInterfaceTable, (void **)&git);
        check(hr == S_OK && git, "real Wine global interface table created");
        if (FAILED(hr) || !git) goto cleanup;
        hr = CreateStreamOnHGlobal(NULL, TRUE, &stream);
        check(hr == S_OK && stream, "real marshalable object created");
        if (FAILED(hr) || !stream) goto cleanup;
        hr = IGlobalInterfaceTable_RegisterInterfaceInGlobal(git, (IUnknown *)stream, &IID_IUnknown, &cookie);
        check(hr == S_OK && cookie, "synchronous registration returns real cookie");
        if (FAILED(hr) || !cookie) goto cleanup;
        signal();
        hr = IGlobalInterfaceTable_GetInterfaceFromGlobal(git, cookie, &IID_IUnknown, (void **)&resolved);
        check(hr == S_OK && resolved, "signal preserves completed registration");
        if (resolved) IUnknown_Release(resolved);
        hr = IGlobalInterfaceTable_RevokeInterfaceFromGlobal(git, cookie);
        check(hr == S_OK, "real registration revokes");
        cookie = 0;
        signal();
        check(TRUE, "post-revoke empty-state signal returns");
cleanup:
        if (cookie) IGlobalInterfaceTable_RevokeInterfaceFromGlobal(git, cookie);
        if (stream) IStream_Release(stream);
        if (git) IGlobalInterfaceTable_Release(git);
        CoUninitialize();
    }
done:
    printf("RESULT git_signal_checks=%u failures=%u\n", checks, failures);
    if (module) FreeLibrary(module);
    return failures ? 1 : 0;
}
