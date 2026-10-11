#define _WIN32_WINNT 0x0602
#include <windows.h>
#include <stdio.h>
#include <string.h>

static LONG CALLBACK handle_exception(EXCEPTION_POINTERS *pointers)
{
    if (pointers->ExceptionRecord->ExceptionCode != 0xe0425844 &&
        pointers->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT) return EXCEPTION_CONTINUE_SEARCH;
    if (pointers->ExceptionRecord->NumberParameters != 2 ||
        pointers->ExceptionRecord->ExceptionInformation[0] != 0x11223344 ||
        pointers->ExceptionRecord->ExceptionInformation[1] != 0x55667788) return EXCEPTION_CONTINUE_SEARCH;
    puts("EXCEPTION_FIXTURE_APPLICATION_HANDLED");
    return EXCEPTION_CONTINUE_EXECUTION;
}

int main(int argc, char **argv)
{
    void *handler;
    HANDLE release = NULL;
    WCHAR name[128];
    ULONG_PTR parameters[2] = {0x11223344, 0x55667788};
    unsigned i;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "deadline"))
    { puts("EXCEPTION_FIXTURE_DEADLINE_ENTERED"); Sleep(30000); return 0; }
    if (!strcmp(argv[1], "aggregate-size"))
    {
        puts("EXCEPTION_FIXTURE_PRESSURE_ENTERED");
        for (i = 0; i < 100000; ++i) OutputDebugStringA("EXCEPTION_CAPTURE_PRESSURE");
        Sleep(30000); return 0;
    }
    if (!strcmp(argv[1], "unhandled"))
    { RaiseException(0xe0425845, 0, 2, parameters); return 3; }
    if (strcmp(argv[1], "handled") && strcmp(argv[1], "breakpoint") &&
        strcmp(argv[1], "read-error") && strcmp(argv[1], "detach")) return 2;
    if (!strcmp(argv[1], "detach"))
    {
        _snwprintf(name, ARRAY_SIZE(name), L"Local\\Xodus.ExceptionFixture.%lu",
                   (unsigned long)GetCurrentProcessId());
        release = CreateEventW(NULL, TRUE, FALSE, name);
        if (!release || GetLastError() == ERROR_ALREADY_EXISTS) return 4;
    }
    handler = AddVectoredExceptionHandler(1, handle_exception);
    if (!handler) return 5;
    RaiseException(!strcmp(argv[1], "breakpoint") ? EXCEPTION_BREAKPOINT : 0xe0425844, 0, 2, parameters);
    if (!RemoveVectoredExceptionHandler(handler)) return 6;
    if (release)
    {
        DWORD result = WaitForSingleObject(release, 10000);
        CloseHandle(release);
        if (result != WAIT_OBJECT_0) return 7;
    }
    puts("EXCEPTION_FIXTURE_EXIT=0");
    return 0;
}
