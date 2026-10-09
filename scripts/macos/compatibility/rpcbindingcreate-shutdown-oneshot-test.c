#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <string.h>

typedef HRESULT (WINAPI *REGISTER_DELAY)(HANDLE, DWORD);
int main(int argc, char **argv)
{
    int native = argc > 2 && !strcmp(argv[2], "--native"), failures = 0;
    HMODULE module = LoadLibraryW(L"combase.dll");
    REGISTER_DELAY reg = (REGISTER_DELAY)(ULONG_PTR)GetProcAddress(module,
        native ? MAKEINTRESOURCEA(69) : "CoRegisterServerShutdownDelay");
    HANDLE event;
    DWORD waited, elapsed, start;
    if (argc < 2 || !reg || FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) return 2;
    event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!event) return 2;
    if (CoAddRefServerProcess() != 1 || reg(event, 150) != S_OK) return 3;
    if (WaitForSingleObject(event, 300) != WAIT_TIMEOUT) ++failures;
    if (CoReleaseServerProcess() != 0) ++failures;
    start = GetTickCount();
    if (!strcmp(argv[1], "deliver")) {
        waited = WaitForSingleObject(event, 1000);
        elapsed = GetTickCount() - start;
        printf("RELEASED_WAIT=%lu ELAPSED=%lu\n", waited, elapsed);
        if (waited != WAIT_OBJECT_0 || elapsed < 100 || elapsed > 900) ++failures;
    } else if (!strcmp(argv[1], "cancel")) {
        if (reg(NULL, 0) != S_OK) ++failures;
        waited = WaitForSingleObject(event, 500);
        printf("CANCEL_WAIT=%lu\n", waited);
        if (waited != WAIT_TIMEOUT) ++failures;
    } else if (!strcmp(argv[1], "reacquire")) {
        waited = WaitForSingleObject(event, 20);
        printf("EARLY_WAIT=%lu\n", waited);
        if (waited != WAIT_TIMEOUT || CoAddRefServerProcess() != 1) ++failures;
        waited = WaitForSingleObject(event, 500);
        printf("REACQUIRED_WAIT=%lu\n", waited);
        if (waited != WAIT_TIMEOUT || CoReleaseServerProcess() != 0) ++failures;
        waited = WaitForSingleObject(event, 1000);
        printf("SECOND_RELEASE_WAIT=%lu\n", waited);
        if (waited != WAIT_OBJECT_0) ++failures;
    } else return 2;
    if (reg(NULL, 0) != S_OK) ++failures;
    if (strcmp(argv[1], "cancel")) {
        HRESULT status;
        ResetEvent(event);
        if (CoAddRefServerProcess() != 1) ++failures;
        status = reg(event, 150);
        printf("TERMINAL_REGISTER=%08lx\n", (unsigned long)status);
        if (status != CO_E_SERVER_STOPPING) ++failures;
        if (CoReleaseServerProcess() != 0) ++failures;
        waited = WaitForSingleObject(event, 500);
        printf("TERMINAL_WAIT=%lu\n", waited);
        if (waited != WAIT_TIMEOUT) ++failures;
        if (reg(NULL, 0) != S_OK) ++failures;
    }
    CloseHandle(event); CoUninitialize(); FreeLibrary(module);
    printf("SHUTDOWN_ONESHOT_%s failures=%d native=%d\n", failures ? "FAIL" : "PASS", failures, native);
    return failures ? 1 : 0;
}
