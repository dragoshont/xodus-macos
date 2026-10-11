#include <windows.h>
#include <stdio.h>
#include <string.h>

static volatile LONG touched;

__declspec(dllexport) __declspec(noinline) void debug_site0(EXCEPTION_RECORD *record, ULONG_PTR frame,
                                                         CONTEXT *context, DISPATCHER_CONTEXT *dispatch)
{
    if (record->ExceptionCode == 0xe0425844 && frame == 7 &&
        context->Rip == 0x1234 && dispatch->ControlPc == 0x5678) InterlockedIncrement(&touched);
}
__declspec(dllexport) __declspec(noinline) void debug_site1(ULONG_PTR control_pc, EXCEPTION_RECORD *record,
                                                         CONTEXT *context)
{
    if (control_pc == 0x5678 && record->ExceptionCode == 0xe0425844 &&
        context->Rip == 0x1234) InterlockedIncrement(&touched);
}
__declspec(dllexport) __declspec(noinline) void debug_site2(ULONG_PTR control_pc, EXCEPTION_RECORD *record,
                                                         CONTEXT *context)
{
    if (control_pc == 0x5678 && record->ExceptionCode == 0xe0425844 &&
        context->Rip == 0x1234) InterlockedIncrement(&touched);
}
__declspec(dllexport) __declspec(noinline) void debug_site3(void) { InterlockedIncrement(&touched); }
__declspec(dllexport) __declspec(noinline) void debug_site4(void) { InterlockedIncrement(&touched); }

static LONG CALLBACK handled(EXCEPTION_POINTERS *exception)
{
    if (exception->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP)
    {
        puts("FIXTURE_UNRELATED_STEP_HANDLER");
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (exception->ExceptionRecord->ExceptionCode != 0xe0425844) return EXCEPTION_CONTINUE_SEARCH;
    puts("FIXTURE_APPLICATION_HANDLER");
    return EXCEPTION_CONTINUE_EXECUTION;
}

static DWORD WINAPI worker(void *argument)
{
    EXCEPTION_RECORD record;
    CONTEXT context;
    DISPATCHER_CONTEXT dispatch;
    (void)argument;
    memset(&record, 0, sizeof(record)); record.ExceptionCode = 0xe0425844;
    record.ExceptionAddress = (void *)(ULONG_PTR)0x1234;
    memset(&context, 0, sizeof(context)); context.Rip = 0x1234;
    memset(&dispatch, 0, sizeof(dispatch)); dispatch.ControlPc = 0x5678;
    debug_site0(argument ? (EXCEPTION_RECORD *)(ULONG_PTR)1 : &record, 7, &context, &dispatch);
    debug_site1(0x5678, &record, &context); debug_site2(0x5678, &record, &context);
    debug_site3(); debug_site4();
    return 0;
}

int main(int argc, char **argv)
{
    HANDLE thread;
    DWORD code;
    void *handler;
    HANDLE detached = NULL;
    WCHAR event_name[128];
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "timeout")) { Sleep(30000); return 0; }
    if (!strcmp(argv[1], "log-limit"))
    {
        unsigned i;
        for (i = 0; i < 10000; ++i) OutputDebugStringA("FIXTURE_CAPTURE_BOUND_0123456789");
        Sleep(30000);
        return 0;
    }
    if (!strcmp(argv[1], "unhandled")) { RaiseException(0xe0425845, 0, 0, NULL); return 3; }
    if (!strcmp(argv[1], "occupied")) { Sleep(30000); return 0; }
    if (strcmp(argv[1], "positive") && strcmp(argv[1], "detach") && strcmp(argv[1], "read-error") &&
        strcmp(argv[1], "foreign-step")) return 2;
    if (!strcmp(argv[1], "detach"))
    {
        _snwprintf(event_name, ARRAY_SIZE(event_name), L"Local\\Xodus.DebugFixture.Detach.%lu",
                   (unsigned long)GetCurrentProcessId());
        detached = CreateEventW(NULL, TRUE, FALSE, event_name);
        if (!detached || GetLastError() == ERROR_ALREADY_EXISTS) return 9;
    }
    handler = AddVectoredExceptionHandler(1, handled);
    if (!handler) return 4;
    RaiseException(!strcmp(argv[1], "foreign-step") ? EXCEPTION_SINGLE_STEP : 0xe0425844, 0, 0, NULL);
    if (!RemoveVectoredExceptionHandler(handler)) return 5;
    if (!strcmp(argv[1], "foreign-step")) return 0;
    thread = CreateThread(NULL, 0, worker, !strcmp(argv[1], "read-error") ? (void *)1 : NULL, 0, NULL);
    if (!thread) return 6;
    if (WaitForSingleObject(thread, 10000) != WAIT_OBJECT_0 ||
        !GetExitCodeThread(thread, &code)) { CloseHandle(thread); return 7; }
    CloseHandle(thread);
    if (detached)
    {
        DWORD waited = WaitForSingleObject(detached, 10000);
        CloseHandle(detached);
        if (waited != WAIT_OBJECT_0) return 10;
    }
    printf("FIXTURE_RESULT touched=%ld thread_exit=%lu\n", touched, (unsigned long)code);
    return touched == 5 && !code ? 0 : 8;
}
