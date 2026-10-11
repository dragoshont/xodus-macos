#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windows.h"
#include "winternl.h"
#include <stdio.h>

typedef NTSTATUS (WINAPI *CONNECT)(const void *, HANDLE *);
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
    WCHAR path[MAX_PATH];

    check(module != NULL, "provider loads");
    check(named != NULL, "reached named export resolves");
    if (!connect) goto done;
    check(GetModuleFileNameW(module, path, MAX_PATH) != 0, "provider path resolves");
    printf("RMCLIENT_PROVIDER=%ls\n", path);
    check(GetProcAddress(module, (LPCSTR)32) == named, "genuine export ordinal matches named function");
    check(connect(&descriptor, &connection) == STATUS_NOT_SUPPORTED &&
          connection == (HANDLE)(ULONG_PTR)0xdeadbeef, "unavailable backend fails without touching output");
    connection = (HANDLE)(ULONG_PTR)0xdeadbeef;
    check(connect(NULL, &connection) == STATUS_INVALID_PARAMETER_1 &&
          connection == (HANDLE)(ULONG_PTR)0xdeadbeef, "NULL descriptor matches genuine failure and preserves output");
    check(connect(&descriptor, NULL) == STATUS_INVALID_PARAMETER_2, "defensive missing-output rejection");
    check(connect(NULL, NULL) == STATUS_INVALID_PARAMETER_1, "NULL descriptor has precedence");
    check(connect((const void *)(ULONG_PTR)1, &connection) == STATUS_NOT_SUPPORTED &&
          connection == (HANDLE)(ULONG_PTR)0xdeadbeef, "unavailable boundary does not dereference descriptor");
    connection = NULL;
    check(connect(&descriptor, &connection) == STATUS_NOT_SUPPORTED && !connection,
          "initially NULL output remains untouched");
done:
    printf("RESULT rmclient_unavailable_checks=%u failures=%u\n", checks, failures);
    if (module) FreeLibrary(module);
    return failures ? 1 : 0;
}
