#include <windows.h>
#include <stdio.h>

typedef HRESULT (WINAPI *CONNECT)(const void *, HANDLE *);
static unsigned checks, failures;

static void check(BOOL valid, const char *name)
{
    checks++;
    if (!valid) { failures++; printf("FAIL %s\n", name); }
}

int main(void)
{
    HMODULE module = LoadLibraryW(L"rmclient.dll");
    FARPROC named = module ? GetProcAddress(module, "HamConnectForExtendedExecution") : NULL;
    CONNECT connect = (CONNECT)(ULONG_PTR)named;
    HANDLE connection = (HANDLE)(ULONG_PTR)0xdeadbeef;
    static const unsigned char descriptor = 1;

    check(module != NULL, "provider loads");
    check(named != NULL, "reached named export resolves");
    if (!connect) goto done;
    check(GetProcAddress(module, (LPCSTR)32) == named, "genuine export ordinal matches named function");
    check(connect(&descriptor, &connection) == HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE) &&
          !connection, "synthetic unavailable backend fails and clears output");
    connection = (HANDLE)(ULONG_PTR)0xdeadbeef;
    check(connect(NULL, &connection) == HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE) &&
          !connection, "unavailable boundary does not interpret descriptor");
    check(connect(&descriptor, NULL) == E_POINTER, "defensive missing-output rejection");
    check(connect((const void *)(ULONG_PTR)1, &connection) == HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE) &&
          !connection, "unavailable boundary does not dereference descriptor");
done:
    printf("RESULT rmclient_unavailable_checks=%u failures=%u\n", checks, failures);
    if (module) FreeLibrary(module);
    return failures ? 1 : 0;
}
