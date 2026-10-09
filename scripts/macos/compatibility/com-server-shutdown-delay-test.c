#include <windows.h>
#include <stdio.h>

typedef HRESULT (WINAPI *REGISTER_DELAY)(HANDLE, DWORD);

int main(void)
{
    HMODULE module = LoadLibraryA("combase-server-shutdown-delay.dll");
    REGISTER_DELAY register_delay = module
        ? (REGISTER_DELAY)GetProcAddress(module, MAKEINTRESOURCEA(69)) : NULL;
    HANDLE event;
    HRESULT results[4];
    unsigned failures = 0;
    if (!register_delay) return 2;
    event = CreateEventW(NULL, TRUE, TRUE, NULL);
    if (!event) return 3;
    results[0] = register_delay(NULL, 0);
    results[1] = register_delay(NULL, 100);
    results[2] = register_delay(event, 0);
    results[3] = register_delay(event, 100);
    if (results[0] != S_OK) ++failures;
    if (results[1] != E_INVALIDARG) ++failures;
    if (results[2] != E_INVALIDARG) ++failures;
    if (results[3] != E_NOTIMPL) ++failures;
    CloseHandle(event);
    FreeLibrary(module);
    printf("SHUTDOWN_DELAY_BOUNDARY_TEST failures=%u\n", failures);
    return failures ? 1 : 0;
}
