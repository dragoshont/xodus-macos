#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <string.h>

typedef HRESULT (WINAPI *REGISTER_DELAY)(HANDLE, DWORD);
static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line=%d %s error=%lu\n", __LINE__, #c, GetLastError()); ++failures; } } while (0)

int main(int argc, char **argv)
{
    int native = argc > 1 && !strcmp(argv[1], "--native");
    HMODULE module = LoadLibraryW(L"combase.dll");
    REGISTER_DELAY reg = (REGISTER_DELAY)(ULONG_PTR)GetProcAddress(module,
        native ? MAKEINTRESOURCEA(69) : "CoRegisterServerShutdownDelay");
    HANDLE first, second, duplicated, invalid;
    DWORD before, after;
    HRESULT status;
    if (!reg || FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) return 2;
    first = CreateEventW(NULL, TRUE, FALSE, NULL);
    second = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(first && second);
    status = reg(first, 150);
    printf("INITIAL_REGISTER=%08lx WAIT=%lu\n", (unsigned long)status, WaitForSingleObject(first, 500));
    CHECK(status == S_OK && WaitForSingleObject(first, 0) == WAIT_TIMEOUT);
    CHECK(reg(NULL, 0) == S_OK);
    CHECK(CoAddRefServerProcess() == 1);
    CHECK(reg(first, 150) == S_OK);
    CHECK(WaitForSingleObject(first, 500) == WAIT_TIMEOUT);
    CHECK(CoReleaseServerProcess() == 0);
    CHECK(reg(NULL, 0) == S_OK);
    CHECK(WaitForSingleObject(first, 500) == WAIT_TIMEOUT);
    CHECK(CoAddRefServerProcess() == 1);
    CHECK(reg(first, 300) == S_OK);
    CHECK(CoReleaseServerProcess() == 0);
    Sleep(40);
    CHECK(CoAddRefServerProcess() == 1);
    CHECK(WaitForSingleObject(first, 450) == WAIT_TIMEOUT);
    CHECK(CoReleaseServerProcess() == 0);
    CHECK(reg(NULL, 0) == S_OK);
    CHECK(WaitForSingleObject(first, 500) == WAIT_TIMEOUT);
    if (!native) {
        invalid = CreateSemaphoreW(NULL, 0, 1, NULL);
        CHECK(FAILED(reg(invalid, 150)));
        CloseHandle(invalid);
        CHECK(FAILED(reg((HANDLE)(ULONG_PTR)0x1234, 150)));
        CHECK(reg(NULL, 150) == E_INVALIDARG);
        CHECK(reg(second, 0) == E_INVALIDARG);
        CHECK(GetProcessHandleCount(GetCurrentProcess(), &before));
        for (int i = 0; i < 100; ++i) {
            duplicated = CreateEventW(NULL, TRUE, FALSE, NULL);
            CHECK(CoAddRefServerProcess() == 1);
            CHECK(reg(duplicated, 100) == S_OK);
            CloseHandle(duplicated); /* registration owns its independent handle */
            CHECK(CoReleaseServerProcess() == 0);
            CHECK(reg(NULL, 0) == S_OK);
        }
        CHECK(GetProcessHandleCount(GetCurrentProcess(), &after));
        CHECK(after <= before + 2);
        printf("OWNERSHIP_CLEANUP_HANDLES=%lu/%lu\n", before, after);
    }
    CHECK(CoAddRefServerProcess() == 1);
    CHECK(reg(first, 150) == S_OK);
    status = reg(second, 150);
    printf("REPLACE_REGISTER=%08lx\n", (unsigned long)status);
    CHECK(status == S_OK);
    CHECK(CoReleaseServerProcess() == 0);
    CHECK(WaitForSingleObject(second, 1000) == WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(first, 0) == WAIT_TIMEOUT);
    CHECK(reg(NULL, 0) == S_OK);
    CHECK(CoAddRefServerProcess() == 1);
    CHECK(reg(first, 150) == CO_E_SERVER_STOPPING);
    CHECK(CoReleaseServerProcess() == 0);
    CHECK(WaitForSingleObject(first, 500) == WAIT_TIMEOUT);
    CHECK(reg(NULL, 0) == S_OK);
    CloseHandle(first); CloseHandle(second);
    CoUninitialize(); FreeLibrary(module);
    printf("SHUTDOWN_DELAY_TEST_%s failures=%d native=%d\n", failures ? "FAIL" : "PASS", failures, native);
    return failures ? 1 : 0;
}
