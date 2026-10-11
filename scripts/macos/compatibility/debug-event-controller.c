#define _WIN32_WINNT 0x0602
#include "debug-event-capture.h"
#include <stdlib.h>

/* Synthetic-only entry point: it cannot start a package or accept game mode. */
int main(int argc, char **argv)
{
    struct debug_capture capture;
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    WCHAR image[4096], command[8192], *name;
    DWORD length, timeout = 10000;
    ULONGLONG limit = 1000000;
    unsigned i;
    char *end;
    memset(&capture, 0, sizeof(capture));
    memset(&startup, 0, sizeof(startup)); startup.cb = sizeof(startup);
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc != 9 || strlen(argv[2]) != 64 ||
        (strcmp(argv[3], "positive") && strcmp(argv[3], "unhandled") &&
         strcmp(argv[3], "timeout") && strcmp(argv[3], "log-limit") &&
         strcmp(argv[3], "read-error") && strcmp(argv[3], "foreign-step") &&
         strcmp(argv[3], "occupied") && strcmp(argv[3], "detach"))) return 2;
    length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argv[1], -1, image, ARRAY_SIZE(image));
    if (!length) return 2;
    name = wcsrchr(image, '\\'); name = name ? name + 1 : image;
    if (_wcsicmp(name, L"debug-event-fixture.exe")) return 2;
    capture.request_detach = !strcmp(argv[3], "detach");
    for (i = 0; i < CAPTURE_SITES; ++i)
    {
        unsigned long rva = strtoul(argv[4 + i], &end, 16);
        if (!rva || *end || rva > 0xffffffff) return 2;
        capture.sites[i].module = name; capture.sites[i].sha256 = argv[2];
        capture.sites[i].rva = (DWORD)rva;
        capture.sites[i].kind = i == 0 ? 1 : i < 3 ? 2 : 0;
    }
    if (_snwprintf(command, ARRAY_SIZE(command), L"\"%ls\" %hs", image, argv[3]) < 0) return 2;
    if (!strcmp(argv[3], "timeout")) timeout = 1500;
    if (!strcmp(argv[3], "log-limit")) limit = 32768;
    if (!CreateProcessW(image, command, NULL, NULL, FALSE,
                        CREATE_SUSPENDED | DEBUG_ONLY_THIS_PROCESS, NULL, NULL, &startup, &process))
    {
        fprintf(stderr, "DEBUG_CREATE_ERROR=%lu\n", (unsigned long)GetLastError());
        return 3;
    }
    capture.process = process.hProcess; capture.pid = process.dwProcessId;
    printf("DEBUG_OWNED_PROCESS pid=%lu\n", (unsigned long)capture.pid);
    if (!strcmp(argv[3], "occupied"))
    {
        CONTEXT context;
        memset(&context, 0, sizeof(context)); context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (!GetThreadContext(process.hThread, &context))
        { capture_abort_initial(&process); return 9; }
        context.Dr0 = 0x10000; context.Dr7 |= 1;
        if (!SetThreadContext(process.hThread, &context))
        { capture_abort_initial(&process); return 9; }
    }
    if (ResumeThread(process.hThread) == (DWORD)-1)
    {
        fprintf(stderr, "DEBUG_RESUME_ERROR=%lu\n", (unsigned long)GetLastError());
        capture_abort_initial(&process);
        return 4;
    }
    CloseHandle(process.hThread);
    capture_loop(&capture, timeout, limit);
    if (!strcmp(argv[3], "detach") && capture.successful_detach)
    {
        WCHAR event_name[128];
        DWORD exit_code = STILL_ACTIVE;
        HANDLE event;
        BOOL ok;
        _snwprintf(event_name, ARRAY_SIZE(event_name), L"Local\\Xodus.DebugFixture.Detach.%lu",
                   (unsigned long)capture.pid);
        event = OpenEventW(EVENT_MODIFY_STATE, FALSE, event_name);
        ok = event && SetEvent(event);
        if (event) CloseHandle(event);
        if (ok) ok = WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0 &&
                     GetExitCodeProcess(process.hProcess, &exit_code) && !exit_code;
        if (!ok)
        {
            fprintf(stderr, "DEBUG_DETACHED_EXIT_ERROR error=%lu\n", (unsigned long)GetLastError());
            if (!TerminateProcess(process.hProcess, 95) ||
                WaitForSingleObject(process.hProcess, 5000) != WAIT_OBJECT_0)
                fprintf(stderr, "DEBUG_DETACHED_CLEANUP_FAILED\n");
            capture.failed = TRUE;
        }
        else { capture.exited = TRUE; capture.exit_code = exit_code; }
        printf("DEBUG_DETACHED_PROCESS quiet=%u exit=%lu\n", ok, (unsigned long)exit_code);
    }
    CloseHandle(process.hProcess);
    if (!strcmp(argv[3], "timeout") || !strcmp(argv[3], "log-limit"))
        return capture.failed && capture.bound_hit && capture.cleanup_verified && !capture.exited &&
               capture.initial_seen && capture.bound_reason == (!strcmp(argv[3], "timeout") ? 1u : 2u) &&
               (strcmp(argv[3], "log-limit") || capture.debug_strings) ? 0 : 5;
    if (!strcmp(argv[3], "unhandled"))
        return !capture.failed && capture.exited && capture.exit_code == 0xe0425845 &&
               capture.exceptions[1] ? 0 : 6;
    if (!strcmp(argv[3], "foreign-step"))
        return !capture.failed && capture.exited && !capture.exit_code &&
               !capture.hits[0] && capture.exceptions[0] && !capture.exceptions[1] ? 0 : 6;
    if (!strcmp(argv[3], "read-error"))
        return capture.failed && capture.read_error && capture.hits[0] == 1 &&
               capture.cleanup_verified ? 0 : 6;
    if (!strcmp(argv[3], "occupied"))
        return capture.failed && capture.occupied_error && capture.cleanup_verified ? 0 : 6;
    if (!strcmp(argv[3], "detach") && !capture.successful_detach) return 6;
    for (i = 0; i < CAPTURE_SITES; ++i)
        if (capture.hits[i] != 1) return 7;
    return !capture.failed && capture.exited && !capture.exit_code &&
           capture.initial_seen && capture.rotated ? 0 : 8;
}
