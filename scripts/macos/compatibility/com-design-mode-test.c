#include <windows.h>
#include <objbase.h>
#include <stdio.h>

typedef HRESULT (WINAPI *QUERY_MODE)(BOOL *);
static unsigned checks, failures;

static void check(BOOL valid, const char *name)
{
    checks++;
    if (!valid) { failures++; printf("FAIL %s\n", name); }
}

static void probe(QUERY_MODE query)
{
    BOOL mode = 0x5a5a5a5a;
    check(query(&mode) == S_OK && mode == FALSE, "normal host writes FALSE");
    mode = TRUE;
    check(query(&mode) == S_OK && mode == FALSE, "repeated call overwrites sentinel");
    check(query(NULL) == E_INVALIDARG, "native NULL-output rejection");
}

int main(void)
{
    HMODULE module = LoadLibraryW(L"combase.dll");
    QUERY_MODE queries[2];
    unsigned i, state;
    HRESULT hr;

    queries[0] = module ? (QUERY_MODE)(ULONG_PTR)GetProcAddress(module, (LPCSTR)90) : NULL;
    queries[1] = module ? (QUERY_MODE)(ULONG_PTR)GetProcAddress(module, (LPCSTR)157) : NULL;
    check(queries[0] && queries[1], "both exact private ordinal exports present");
    if (!queries[0] || !queries[1]) goto done;
    for (state = 0; state < 3; state++)
    {
        if (state)
        {
            hr = CoInitializeEx(NULL, state == 1 ? COINIT_APARTMENTTHREADED : COINIT_MULTITHREADED);
            check(SUCCEEDED(hr), "real apartment initialized");
            if (FAILED(hr)) continue;
        }
        for (i = 0; i < 2; i++) probe(queries[i]);
        if (state) CoUninitialize();
    }
done:
    printf("RESULT design_mode_checks=%u failures=%u\n", checks, failures);
    if (module) FreeLibrary(module);
    return failures ? 1 : 0;
}
