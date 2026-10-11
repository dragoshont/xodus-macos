#define _WIN32_WINNT 0x0602
#include <windows.h>
#include <bcrypt.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpacked-not-aligned"
#include <dbghelp.h>
#pragma GCC diagnostic pop
#include <stdio.h>
#include <string.h>
#include <wchar.h>

struct thread_entry { DWORD id; HANDLE handle; };
struct capture
{
    PROCESS_INFORMATION process;
    struct thread_entry threads[128];
    unsigned thread_count, first, second, dumps;
    BOOL startup, failed, read_error, detached, quiet;
    unsigned reason;
    unsigned handles_closed, handles_failed;
    ULONGLONG deadline, limit, dump_bytes;
    WCHAR dump_prefix[4096];
#ifdef EXCEPTION_PACKAGE_CAPTURE
    struct { ULONG_PTR base; DWORD size; WCHAR name[260]; BOOL loaded; } modules[128];
    unsigned module_count, selected_events;
    BOOL saw_stub, first_wait;
    ULONGLONG started;
#endif
};

#ifdef EXCEPTION_PACKAGE_CAPTURE
static BOOL package_prepare(struct capture *, int, char **, WCHAR *, WCHAR *, WCHAR *);
static BOOL package_activate(struct capture *);
static BOOL package_windows(struct capture *);
#endif

static void close_owned(struct capture *capture, HANDLE handle)
{
    if (!handle || handle == INVALID_HANDLE_VALUE) return;
    if (CloseHandle(handle)) ++capture->handles_closed;
    else
    {
        fprintf(stderr, "EXCEPTION_CLOSE_ERROR error=%lu\n", (unsigned long)GetLastError());
        ++capture->handles_failed;
        capture->failed = TRUE;
        capture->reason = 5;
    }
}

static BOOL hash_file(HANDLE file, const char *expected)
{
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    BYTE buffer[16384], digest[32];
    DWORD size;
    LARGE_INTEGER zero, saved;
    unsigned i;
    char hex[65];
    BOOL ok = FALSE;
    zero.QuadPart = 0;
    if (!SetFilePointerEx(file, zero, &saved, FILE_CURRENT) ||
        !SetFilePointerEx(file, zero, NULL, FILE_BEGIN)) return FALSE;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) ||
        BCryptCreateHash(algorithm, &hash, NULL, 0, NULL, 0, 0)) goto done;
    for (;;)
    {
        if (!ReadFile(file, buffer, sizeof(buffer), &size, NULL)) goto done;
        if (!size) break;
        if (BCryptHashData(hash, buffer, size, 0)) goto done;
    }
    if (BCryptFinishHash(hash, digest, sizeof(digest), 0)) goto done;
    for (i = 0; i < sizeof(digest); ++i) sprintf(hex + i * 2, "%02x", digest[i]);
    ok = !strcmp(hex, expected);
done:
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!SetFilePointerEx(file, saved, NULL, FILE_BEGIN)) ok = FALSE;
    return ok;
}

static BOOL read_remote(struct capture *capture, ULONG_PTR address, void *out, SIZE_T size)
{
    SIZE_T actual = 0;
    if (address && ReadProcessMemory(capture->process.hProcess, (void *)address,
                                    out, size, &actual) && actual == size) return TRUE;
    capture->read_error = TRUE;
    fprintf(stderr, "EXCEPTION_READ_ERROR address=%llx size=%llu error=%lu\n",
            (unsigned long long)address, (unsigned long long)size, (unsigned long)GetLastError());
    return FALSE;
}

static BOOL range_ok(ULONG64 offset, ULONG64 length, ULONG64 total)
{
    return offset <= total && length <= total - offset;
}

static BOOL context_matches(const CONTEXT *saved, const CONTEXT *expected)
{
    DWORD flags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    return (saved->ContextFlags & flags) == flags &&
        saved->EFlags == expected->EFlags && saved->SegCs == expected->SegCs &&
        saved->SegSs == expected->SegSs &&
        !memcmp(&saved->Rax, &expected->Rax,
                offsetof(CONTEXT, Rip) + sizeof(saved->Rip) - offsetof(CONTEXT, Rax));
}

static BOOL validate_dump(HANDLE file, DWORD tid, const EXCEPTION_RECORD *record,
                          const CONTEXT *context, ULONGLONG limit)
{
    HANDLE mapping = NULL;
    BYTE *data = NULL;
    LARGE_INTEGER size;
    MINIDUMP_HEADER *header;
    MINIDUMP_DIRECTORY *directory;
    BOOL exception_ok = FALSE, thread_ok = FALSE, module_ok = FALSE, ok = FALSE;
    DWORD i, j;
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
        (ULONGLONG)size.QuadPart > limit) return FALSE;
    mapping = CreateFileMappingW(file, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!mapping || !(data = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0))) goto done;
    if (!range_ok(0, sizeof(*header), size.QuadPart)) goto done;
    header = (MINIDUMP_HEADER *)data;
    if (header->Signature != MINIDUMP_SIGNATURE ||
        !range_ok(header->StreamDirectoryRva,
                  (ULONG64)header->NumberOfStreams * sizeof(*directory), size.QuadPart)) goto done;
    directory = (MINIDUMP_DIRECTORY *)(data + header->StreamDirectoryRva);
    for (i = 0; i < header->NumberOfStreams; ++i)
    {
        MINIDUMP_LOCATION_DESCRIPTOR location = directory[i].Location;
        if (!range_ok(location.Rva, location.DataSize, size.QuadPart)) goto done;
        if (directory[i].StreamType == ExceptionStream)
        {
            MINIDUMP_EXCEPTION_STREAM *exception;
            CONTEXT saved;
            if (exception_ok || location.DataSize < sizeof(*exception)) goto done;
            exception = (MINIDUMP_EXCEPTION_STREAM *)(data + location.Rva);
            if (exception->ThreadId != tid ||
                exception->ExceptionRecord.ExceptionCode != record->ExceptionCode ||
                exception->ExceptionRecord.ExceptionFlags != record->ExceptionFlags ||
                exception->ExceptionRecord.ExceptionRecord != (ULONG64)(ULONG_PTR)record->ExceptionRecord ||
                exception->ExceptionRecord.ExceptionAddress != (ULONG64)(ULONG_PTR)record->ExceptionAddress ||
                exception->ExceptionRecord.NumberParameters != record->NumberParameters ||
                record->NumberParameters > EXCEPTION_MAXIMUM_PARAMETERS ||
                memcmp(exception->ExceptionRecord.ExceptionInformation, record->ExceptionInformation,
                       record->NumberParameters * sizeof(ULONG_PTR)) ||
                exception->ThreadContext.DataSize < sizeof(CONTEXT) ||
                !range_ok(exception->ThreadContext.Rva, exception->ThreadContext.DataSize, size.QuadPart)) goto done;
            memcpy(&saved, data + exception->ThreadContext.Rva, sizeof(saved));
            if (!context_matches(&saved, context)) goto done;
            exception_ok = TRUE;
        }
        else if (directory[i].StreamType == ThreadListStream)
        {
            MINIDUMP_THREAD_LIST *list = (MINIDUMP_THREAD_LIST *)(data + location.Rva);
            if (location.DataSize < sizeof(DWORD) ||
                (ULONG64)sizeof(DWORD) + (ULONG64)list->NumberOfThreads * sizeof(MINIDUMP_THREAD) >
                location.DataSize) goto done;
            for (j = 0; j < list->NumberOfThreads; ++j)
            {
                MINIDUMP_THREAD *thread = &list->Threads[j];
                if (!range_ok(thread->ThreadContext.Rva, thread->ThreadContext.DataSize, size.QuadPart) ||
                    !range_ok(thread->Stack.Memory.Rva, thread->Stack.Memory.DataSize, size.QuadPart)) goto done;
                if (thread->ThreadId == tid && thread->ThreadContext.DataSize >= sizeof(CONTEXT))
                {
                    CONTEXT saved;
                    memcpy(&saved, data + thread->ThreadContext.Rva, sizeof(saved));
                    thread_ok = context_matches(&saved, context);
                }
            }
        }
        else if (directory[i].StreamType == ModuleListStream)
        {
            MINIDUMP_MODULE_LIST *list = (MINIDUMP_MODULE_LIST *)(data + location.Rva);
            if (location.DataSize < sizeof(DWORD) ||
                (ULONG64)sizeof(DWORD) + (ULONG64)list->NumberOfModules * sizeof(MINIDUMP_MODULE) >
                location.DataSize) goto done;
            for (j = 0; j < list->NumberOfModules; ++j)
            {
                MINIDUMP_MODULE *module = &list->Modules[j];
                ULONG length;
                if (!range_ok(module->ModuleNameRva, sizeof(length), size.QuadPart)) goto done;
                memcpy(&length, data + module->ModuleNameRva, sizeof(length));
                if (length % 2 || !range_ok(module->ModuleNameRva + sizeof(length), length, size.QuadPart))
                    goto done;
                if (length && (ULONG64)(ULONG_PTR)record->ExceptionAddress >= module->BaseOfImage &&
                    (ULONG64)(ULONG_PTR)record->ExceptionAddress - module->BaseOfImage < module->SizeOfImage)
                    module_ok = TRUE;
            }
        }
    }
    ok = exception_ok && thread_ok && module_ok;
    printf("EXCEPTION_DUMP_VALIDATION exception=%u thread=%u fault_module=%u bytes=%llu\n",
           exception_ok, thread_ok, module_ok, (unsigned long long)size.QuadPart);
done:
    if (data) UnmapViewOfFile(data);
    if (mapping) CloseHandle(mapping);
    return ok;
}

struct dump_bound { HANDLE file; ULONGLONG deadline, limit; BOOL exceeded; };

static BOOL CALLBACK dump_callback(void *parameter, const PMINIDUMP_CALLBACK_INPUT input,
                                  MINIDUMP_CALLBACK_OUTPUT *output)
{
    struct dump_bound *bound = parameter;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(bound->file, &size) ||
        (ULONGLONG)size.QuadPart > bound->limit || GetTickCount64() > bound->deadline)
        bound->exceeded = TRUE;
    if (input->CallbackType == CancelCallback)
    {
        output->CheckCancel = TRUE;
        output->Cancel = bound->exceeded;
    }
    if (input->CallbackType == ThreadCallback)
        output->ThreadWriteFlags = bound->exceeded ? 0 : ThreadWriteThread | ThreadWriteContext;
    if (input->CallbackType == ModuleCallback)
        output->ModuleWriteFlags = bound->exceeded ? 0 : ModuleWriteModule;
    return TRUE;
}

static BOOL save_exception(struct capture *capture, DEBUG_EVENT *event, BOOL inject_read_error)
{
    struct thread_entry *thread = NULL;
    CONTEXT context;
    EXCEPTION_RECORD record = event->u.Exception.ExceptionRecord;
    EXCEPTION_POINTERS pointers;
    MINIDUMP_EXCEPTION_INFORMATION exception;
    MINIDUMP_CALLBACK_INFORMATION callback;
    struct dump_bound bound;
    WCHAR path[4096];
    HANDLE file;
    unsigned i;
    BOOL written, valid;
    LARGE_INTEGER size;
    for (i = 0; i < capture->thread_count; ++i)
        if (capture->threads[i].id == event->dwThreadId) thread = &capture->threads[i];
    if (!thread || !thread->handle || record.NumberParameters > EXCEPTION_MAXIMUM_PARAMETERS) return FALSE;
    memset(&context, 0, sizeof(context)); context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    if (!GetThreadContext(thread->handle, &context))
    {
        fprintf(stderr, "EXCEPTION_CONTEXT_ERROR thread=%lu error=%lu\n",
                (unsigned long)thread->id, (unsigned long)GetLastError());
        return FALSE;
    }
    printf("EXCEPTION_CONTEXT thread=%lu rip=%llx rsp=%llx rcx=%llx rdx=%llx r8=%llx r9=%llx\n",
           (unsigned long)thread->id, (unsigned long long)context.Rip, (unsigned long long)context.Rsp,
           (unsigned long long)context.Rcx, (unsigned long long)context.Rdx,
           (unsigned long long)context.R8, (unsigned long long)context.R9);
    {
        BYTE instruction;
        if (!read_remote(capture, inject_read_error ? 1 : (ULONG_PTR)record.ExceptionAddress,
                         &instruction, sizeof(instruction))) return FALSE;
    }
    if (_snwprintf(path, ARRAY_SIZE(path), L"%ls-%u.dmp", capture->dump_prefix, capture->dumps) < 0)
        return FALSE;
    file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
#ifdef EXCEPTION_PACKAGE_CAPTURE
    printf("EXCEPTION_DUMP_BIND event=%u thread=%lu code=%08lx chance=%u path=%ls\n",
           capture->selected_events, (unsigned long)thread->id, (unsigned long)record.ExceptionCode,
           event->u.Exception.dwFirstChance ? 1 : 2, path);
#endif
    pointers.ExceptionRecord = &record; pointers.ContextRecord = &context;
    exception.ThreadId = thread->id; exception.ExceptionPointers = &pointers;
    exception.ClientPointers = FALSE;
    bound.file = file; bound.deadline = capture->deadline;
    bound.limit = capture->dump_bytes < capture->limit ? capture->limit - capture->dump_bytes : 0;
#ifdef EXCEPTION_PACKAGE_CAPTURE
    if (bound.limit > 2000000) bound.limit = 2000000;
#endif
    bound.exceeded = FALSE;
    callback.CallbackRoutine = dump_callback; callback.CallbackParam = &bound;
    written = MiniDumpWriteDump(capture->process.hProcess, capture->process.dwProcessId, file,
                               MiniDumpNormal, &exception, NULL, &callback);
    valid = written && !bound.exceeded && validate_dump(file, thread->id, &record, &context, capture->limit);
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0) valid = FALSE;
    else
    {
        capture->dump_bytes += size.QuadPart;
#ifdef EXCEPTION_PACKAGE_CAPTURE
        if ((ULONGLONG)size.QuadPart > bound.limit) valid = FALSE;
#endif
    }
    if (!valid) fprintf(stderr, "EXCEPTION_DUMP_ERROR written=%u bound=%u error=%lu\n",
                        written, bound.exceeded, (unsigned long)GetLastError());
    close_owned(capture, file);
    if (valid) ++capture->dumps;
    return valid;
}

static BOOL module_event(struct capture *capture, HANDLE file, void *base,
                         const WCHAR *fixture_name, const char *fixture_pin, ULONG_PTR *startup_address)
{
    static const BYTE startup_bytes[] = {0xcc,0xc3,0x49,0xff,0x80,0xf8,0x00,0x00,0x00,0x31,0xc0,0xc3};
    WCHAR path[4096], *name;
    DWORD length;
    BYTE actual[sizeof(startup_bytes)];
    if (!file || file == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "EXCEPTION_MODULE_FILE_MISSING base=%p\n", base);
        return FALSE;
    }
    length = GetFinalPathNameByHandleW(file, path, ARRAY_SIZE(path), FILE_NAME_NORMALIZED);
    if (!length || length >= ARRAY_SIZE(path)) return FALSE;
    name = wcsrchr(path, '\\'); name = name ? name + 1 : path;
    printf("EXCEPTION_MODULE name=%ls base=%p\n", name, base);
#ifdef EXCEPTION_PACKAGE_CAPTURE
    {
        IMAGE_DOS_HEADER dos;
        IMAGE_NT_HEADERS64 nt;
        unsigned index = capture->module_count;
        if (index == ARRAY_SIZE(capture->modules) || wcslen(name) >= 260 ||
            !read_remote(capture, (ULONG_PTR)base, &dos, sizeof(dos)) ||
            dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew > 0x100000 ||
            !read_remote(capture, (ULONG_PTR)base + dos.e_lfanew, &nt, sizeof(nt)) ||
            nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || !nt.OptionalHeader.SizeOfImage)
            return FALSE;
        capture->modules[index].base = (ULONG_PTR)base;
        capture->modules[index].size = nt.OptionalHeader.SizeOfImage;
        wcscpy(capture->modules[index].name, name);
        capture->modules[index].loaded = TRUE;
        ++capture->module_count;
    }
    if ((!_wcsicmp(name, L"twinapi.appcore.dll") &&
         !hash_file(file, "0d3e56c5068633f975bcab528267789f520f82a6d6a50f1330104e7597289b43")) ||
        (!_wcsicmp(name, L"mrt100_app.dll") &&
         !hash_file(file, "6eb3c9c626c2514b39301969f318d1b7c839d3efe1a86a90783ec6f78f19e3ec")) ||
        (!_wcsicmp(name, L"xodus-vector-oracle.dll") &&
         !hash_file(file, "5a94b019c7190d9ec47f98e0bc28dced70036f1f97e9a84ef9d75340e364158a")))
        return FALSE;
#endif
    if (!_wcsicmp(name, fixture_name) && !hash_file(file, fixture_pin)) return FALSE;
    if (!_wcsicmp(name, L"dbghelp.dll") &&
        !hash_file(file, "e111a5b162e52207d04fe82d704c1b169297c22c2fdff64348322a98a15abbfb")) return FALSE;
    if (!_wcsicmp(name, L"ntdll.dll"))
    {
        if (!hash_file(file, "f8bd19c7606e0f7c506380d1c5c1d8bea477cbfea935f8c6f115de2ae8a2cb42") ||
            !read_remote(capture, (ULONG_PTR)base + 0x110a8, actual, sizeof(actual)) ||
            memcmp(actual, startup_bytes, sizeof(actual))) return FALSE;
        *startup_address = (ULONG_PTR)base + 0x110a8;
        puts("EXCEPTION_STARTUP_PIN_VERIFIED");
    }
    return TRUE;
}

static void cleanup_owned(struct capture *capture, BOOL pending, DEBUG_EVENT *event)
{
    BOOL terminated = TerminateProcess(capture->process.hProcess, 95);
    BOOL continued = !pending || ContinueDebugEvent(event->dwProcessId, event->dwThreadId,
        event->dwDebugEventCode == EXCEPTION_DEBUG_EVENT ? DBG_EXCEPTION_NOT_HANDLED : DBG_CONTINUE);
    if (!capture->detached)
        capture->detached = DebugActiveProcessStop(capture->process.dwProcessId);
    capture->quiet = WaitForSingleObject(capture->process.hProcess, 5000) == WAIT_OBJECT_0;
    printf("EXCEPTION_CLEANUP terminated=%u continued=%u detached=%u quiet=%u\n",
           terminated, continued, capture->detached, capture->quiet);
    if (!terminated || !continued || !capture->detached || !capture->quiet) capture->reason = 5;
}

int main(int argc, char **argv)
{
    struct capture capture;
    STARTUPINFOW startup;
    DEBUG_EVENT event;
    WCHAR image[4096], command[8192], *name;
#ifdef EXCEPTION_PACKAGE_CAPTURE
    WCHAR cwd[4096];
#endif
    ULONG_PTR startup_address = 0;
    DWORD disposition, exit_code = STILL_ACTIVE;
    BOOL pending = FALSE, exited = FALSE;
    unsigned i;
    setvbuf(stdout, NULL, _IONBF, 0);
    memset(&capture, 0, sizeof(capture)); memset(&startup, 0, sizeof(startup)); startup.cb = sizeof(startup);
#ifdef EXCEPTION_PACKAGE_CAPTURE
    if (!package_prepare(&capture, argc, argv, image, command, cwd))
    {
        fprintf(stderr, "EXCEPTION_PACKAGE_PREPARE_ERROR error=%lu\n", (unsigned long)GetLastError());
        return 2;
    }
    name = wcsrchr(image, '\\'); name = name ? name + 1 : image;
#else
    if (argc != 5 || strlen(argv[2]) != 64 ||
        (strcmp(argv[3], "handled") && strcmp(argv[3], "breakpoint") && strcmp(argv[3], "unhandled") &&
         strcmp(argv[3], "read-error") && strcmp(argv[3], "deadline") &&
         strcmp(argv[3], "aggregate-size") && strcmp(argv[3], "detach"))) return 2;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argv[1], -1, image, ARRAY_SIZE(image)) ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argv[4], -1,
                             capture.dump_prefix, ARRAY_SIZE(capture.dump_prefix))) return 2;
    name = wcsrchr(image, '\\'); name = name ? name + 1 : image;
    if (_wcsicmp(name, L"exception-event-fixture.exe")) return 2;
#endif
    {
        HMODULE provider = GetModuleHandleW(L"dbghelp.dll");
        WCHAR path[4096];
        HANDLE file;
        DWORD length = provider ? GetModuleFileNameW(provider, path, ARRAY_SIZE(path)) : 0;
        if (!length || length >= ARRAY_SIZE(path)) return 3;
        file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, 0, NULL);
        if (file == INVALID_HANDLE_VALUE) return 3;
        if (!hash_file(file, "e111a5b162e52207d04fe82d704c1b169297c22c2fdff64348322a98a15abbfb"))
        { CloseHandle(file); return 3; }
        CloseHandle(file);
        printf("EXCEPTION_DUMP_PROVIDER=%ls pin=e111a5b162e52207d04fe82d704c1b169297c22c2fdff64348322a98a15abbfb\n", path);
    }
#ifdef EXCEPTION_PACKAGE_CAPTURE
    puts("EXCEPTION_PACKAGE_FILTER stub=80000100 then=all-first-and-second-chance failfast=00001007 max_events=8 max_dump_bytes=2000000 total_controller_bytes=10000000 deadline_ms=60000");
    capture.limit = 10000000;
    capture.started = GetTickCount64();
    capture.deadline = capture.started + 60000;
#else
    capture.limit = !strcmp(argv[3], "aggregate-size") ? 65536 : 10000000;
    capture.deadline = GetTickCount64() + (!strcmp(argv[3], "deadline") ? 2000 : 12000);
    if (_snwprintf(command, ARRAY_SIZE(command), L"\"%ls\" %hs", image, argv[3]) < 0) return 2;
#endif
    if (!CreateProcessW(image, command, NULL, NULL, FALSE, CREATE_SUSPENDED | DEBUG_ONLY_THIS_PROCESS,
                        NULL,
#ifdef EXCEPTION_PACKAGE_CAPTURE
                        cwd,
#else
                        NULL,
#endif
                        &startup, &capture.process))
    {
        fprintf(stderr, "EXCEPTION_CREATE_PROCESS_ERROR error=%lu\n", (unsigned long)GetLastError());
        return 3;
    }
    printf("EXCEPTION_OWNED_PID=%lu\n", (unsigned long)capture.process.dwProcessId);
#ifdef EXCEPTION_PACKAGE_CAPTURE
    if (!package_activate(&capture))
    { capture.failed = TRUE; capture.reason = 4; goto done; }
#endif
    if (ResumeThread(capture.process.hThread) == (DWORD)-1)
    { capture.failed = TRUE; capture.reason = 4; goto done; }
    close_owned(&capture, capture.process.hThread); capture.process.hThread = NULL;
    if (capture.failed) goto done;
    while (!exited)
    {
#ifdef EXCEPTION_PACKAGE_CAPTURE
        if (!capture.first_wait && GetTickCount64() - capture.started >= 11000)
        {
            capture.first_wait = TRUE;
            printf("ORIGINAL_APP_FIRST_WAIT=%lu ELAPSED_MS=%llu\n",
                   (unsigned long)WaitForSingleObject(capture.process.hProcess, 0),
                   (unsigned long long)(GetTickCount64() - capture.started));
            if (!package_windows(&capture)) { capture.failed = TRUE; capture.reason = 4; break; }
        }
#endif
        __int64 size = _ftelli64(stdout);
        if (size < 0) { capture.failed = TRUE; capture.reason = 4; break; }
        if ((ULONGLONG)size + capture.dump_bytes > capture.limit)
        { capture.failed = TRUE; capture.reason = 2; break; }
        if (GetTickCount64() > capture.deadline) { capture.failed = TRUE; capture.reason = 1; break; }
        if (!WaitForDebugEvent(&event, 100))
        {
            if (GetLastError() == ERROR_SEM_TIMEOUT) continue;
            capture.failed = TRUE; capture.reason = 4; break;
        }
        pending = TRUE; disposition = DBG_CONTINUE;
        if (event.dwProcessId != capture.process.dwProcessId)
        { capture.failed = TRUE; capture.reason = 4; break; }
        switch (event.dwDebugEventCode)
        {
        case CREATE_PROCESS_DEBUG_EVENT:
        case CREATE_THREAD_DEBUG_EVENT:
            if (capture.thread_count == ARRAY_SIZE(capture.threads))
            {
                if (event.dwDebugEventCode == CREATE_THREAD_DEBUG_EVENT)
                    close_owned(&capture, event.u.CreateThread.hThread);
                else
                {
                    close_owned(&capture, event.u.CreateProcessInfo.hThread);
                    close_owned(&capture, event.u.CreateProcessInfo.hFile);
                    if (event.u.CreateProcessInfo.hProcess != capture.process.hProcess)
                        close_owned(&capture, event.u.CreateProcessInfo.hProcess);
                }
                capture.failed = TRUE;
                break;
            }
            capture.threads[capture.thread_count].id = event.dwThreadId;
            capture.threads[capture.thread_count++].handle =
                event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT ?
                event.u.CreateProcessInfo.hThread : event.u.CreateThread.hThread;
            if (event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT)
            {
                if (!module_event(&capture, event.u.CreateProcessInfo.hFile,
                                  event.u.CreateProcessInfo.lpBaseOfImage, name, argv[2], &startup_address))
                    capture.failed = TRUE;
                close_owned(&capture, event.u.CreateProcessInfo.hFile);
                if (event.u.CreateProcessInfo.hProcess != capture.process.hProcess)
                    close_owned(&capture, event.u.CreateProcessInfo.hProcess);
            }
            break;
        case LOAD_DLL_DEBUG_EVENT:
            if (!module_event(&capture, event.u.LoadDll.hFile, event.u.LoadDll.lpBaseOfDll,
                              name, argv[2], &startup_address)) capture.failed = TRUE;
            close_owned(&capture, event.u.LoadDll.hFile);
            break;
        case EXIT_THREAD_DEBUG_EVENT:
            for (i = 0; i < capture.thread_count; ++i)
                if (capture.threads[i].id == event.dwThreadId && capture.threads[i].handle)
                { close_owned(&capture, capture.threads[i].handle); capture.threads[i].handle = NULL; }
            break;
#ifdef EXCEPTION_PACKAGE_CAPTURE
        case UNLOAD_DLL_DEBUG_EVENT:
            for (i = 0; i < capture.module_count; ++i)
                if (capture.modules[i].base == (ULONG_PTR)event.u.UnloadDll.lpBaseOfDll)
                    capture.modules[i].loaded = FALSE;
            break;
#endif
        case EXCEPTION_DEBUG_EVENT:
            if (!capture.startup && event.u.Exception.dwFirstChance &&
                event.dwThreadId == capture.process.dwThreadId &&
                event.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT &&
                startup_address && (ULONG_PTR)event.u.Exception.ExceptionRecord.ExceptionAddress == startup_address)
            { capture.startup = TRUE; puts("EXCEPTION_OWNED_STARTUP_TRAP"); }
            else
            {
                EXCEPTION_RECORD *record = &event.u.Exception.ExceptionRecord;
                disposition = DBG_EXCEPTION_NOT_HANDLED;
                if (event.u.Exception.dwFirstChance) ++capture.first; else ++capture.second;
                printf("EXCEPTION_EVENT chance=%u thread=%lu code=%08lx flags=%lx address=%p parameters=%lu\n",
                       event.u.Exception.dwFirstChance ? 1 : 2, (unsigned long)event.dwThreadId,
                       (unsigned long)record->ExceptionCode, (unsigned long)record->ExceptionFlags,
                       record->ExceptionAddress, (unsigned long)record->NumberParameters);
                if (record->NumberParameters > EXCEPTION_MAXIMUM_PARAMETERS) capture.failed = TRUE;
                else for (i = 0; i < record->NumberParameters; ++i)
                    printf("EXCEPTION_PARAMETER index=%u value=%llx\n", i,
                           (unsigned long long)record->ExceptionInformation[i]);
#ifdef EXCEPTION_PACKAGE_CAPTURE
                if (record->ExceptionCode == 0x80000100) capture.saw_stub = TRUE;
                if (!capture.failed && (capture.saw_stub || record->ExceptionCode == 0x1007))
                {
                    unsigned module;
                    if (++capture.selected_events > 8)
                    { capture.failed = TRUE; capture.reason = 6; break; }
                    printf("EXCEPTION_SELECTED index=%u chance=%u code=%08lx\n",
                           capture.selected_events, event.u.Exception.dwFirstChance ? 1 : 2,
                           (unsigned long)record->ExceptionCode);
                    for (module = 0; module < capture.module_count; ++module)
                        if (capture.modules[module].loaded)
                            printf("EXCEPTION_INVENTORY event=%u name=%ls base=%llx size=%lx\n",
                                   capture.selected_events, capture.modules[module].name,
                                   (unsigned long long)capture.modules[module].base,
                                   (unsigned long)capture.modules[module].size);
                    printf("EXCEPTION_RECORD_OWNER event=%u address=%p\n",
                           capture.selected_events, record->ExceptionAddress);
                    if (!save_exception(&capture, &event, FALSE)) capture.failed = TRUE;
                }
#else
                if (!capture.failed && event.u.Exception.dwFirstChance &&
                    !save_exception(&capture, &event, !strcmp(argv[3], "read-error"))) capture.failed = TRUE;
#endif
            }
            break;
        case OUTPUT_DEBUG_STRING_EVENT:
            printf("EXCEPTION_DEBUG_STRING length=%u unicode=%u\n",
                   event.u.DebugString.nDebugStringLength, event.u.DebugString.fUnicode);
            break;
        case EXIT_PROCESS_DEBUG_EVENT:
            exited = TRUE; exit_code = event.u.ExitProcess.dwExitCode; break;
        }
        if (capture.failed)
        {
            if (!capture.reason) capture.reason = capture.read_error ? 3 : 4;
            fprintf(stderr, "EXCEPTION_GATE_ERROR event=%lu reason=%u error=%lu\n",
                    (unsigned long)event.dwDebugEventCode, capture.reason, (unsigned long)GetLastError());
            break;
        }
        printf("EXCEPTION_CONTINUE event=%lu status=%08lx\n",
               (unsigned long)event.dwDebugEventCode, (unsigned long)disposition);
        if (!ContinueDebugEvent(event.dwProcessId, event.dwThreadId, disposition))
        { capture.failed = TRUE; capture.reason = 4; break; }
        pending = FALSE;
#ifndef EXCEPTION_PACKAGE_CAPTURE
        if (!strcmp(argv[3], "detach") && capture.dumps)
        {
            WCHAR event_name[128];
            HANDLE released;
            capture.detached = DebugActiveProcessStop(capture.process.dwProcessId);
            _snwprintf(event_name, ARRAY_SIZE(event_name), L"Local\\Xodus.ExceptionFixture.%lu",
                       (unsigned long)capture.process.dwProcessId);
            released = OpenEventW(EVENT_MODIFY_STATE, FALSE, event_name);
            if (!capture.detached || !released || !SetEvent(released)) capture.failed = TRUE;
            close_owned(&capture, released);
            capture.quiet = WaitForSingleObject(capture.process.hProcess, 5000) == WAIT_OBJECT_0;
            if (!capture.quiet || !GetExitCodeProcess(capture.process.hProcess, &exit_code)) capture.failed = TRUE;
            if (capture.failed) capture.reason = 5;
            else exited = TRUE;
            printf("EXCEPTION_LIVE_DETACH detached=%u quiet=%u exit=%lu\n",
                   capture.detached, capture.quiet, (unsigned long)exit_code);
            break;
        }
#endif
    }
done:
#ifdef EXCEPTION_PACKAGE_CAPTURE
    if (!package_windows(&capture))
    { capture.failed = TRUE; if (!capture.reason) capture.reason = 4; }
    if (exited) printf("ORIGINAL_APP_EXIT=%lu\n", (unsigned long)exit_code);
    else puts("ORIGINAL_APP_EXIT_UNOBSERVED=1");
#endif
    if (capture.failed && !exited) cleanup_owned(&capture, pending, &event);
    for (i = 0; i < capture.thread_count; ++i)
        close_owned(&capture, capture.threads[i].handle);
    close_owned(&capture, capture.process.hThread);
    if (exited)
    {
        capture.quiet = WaitForSingleObject(capture.process.hProcess, 5000) == WAIT_OBJECT_0;
        if (!capture.quiet) { capture.failed = TRUE; capture.reason = 5; }
    }
    close_owned(&capture, capture.process.hProcess);
    printf("EXCEPTION_HANDLES_CLOSED count=%u failures=%u\n", capture.handles_closed, capture.handles_failed);
    {
        printf("EXCEPTION_RESULT failed=%u reason=%u startup=%u first=%u second=%u dumps=%u exited=%u exit=%lu quiet=%u\n",
               capture.failed, capture.reason, capture.startup, capture.first, capture.second,
               capture.dumps, exited, (unsigned long)exit_code, capture.quiet);
        __int64 size = _ftelli64(stdout);
        if (size < 0 || (ULONGLONG)size + capture.dump_bytes > capture.limit)
        {
            fprintf(stderr, "EXCEPTION_FINAL_AGGREGATE_BOUND stdout=%lld dumps=%llu\n",
                    (long long)size, (unsigned long long)capture.dump_bytes);
            capture.failed = TRUE;
            if (capture.reason != 5) capture.reason = size < 0 ? 4 : 2;
        }
    }
#ifdef EXCEPTION_PACKAGE_CAPTURE
    return !capture.failed && capture.startup && exited && capture.quiet ? 0 : 7;
#else
    if (!strcmp(argv[3], "read-error"))
        return capture.failed && capture.reason == 3 && capture.detached && capture.quiet && capture.first ? 0 : 7;
    if (!strcmp(argv[3], "deadline") || !strcmp(argv[3], "aggregate-size"))
        return capture.failed && capture.reason == (!strcmp(argv[3], "deadline") ? 1u : 2u) &&
               capture.startup && capture.detached && capture.quiet ? 0 : 7;
    if (!strcmp(argv[3], "unhandled"))
        return !capture.failed && capture.startup && capture.dumps == 1 && capture.first == 1 &&
               capture.second == 1 && exited && exit_code == 0xe0425845 ? 0 : 7;
    return !capture.failed && capture.startup && capture.dumps == 1 && capture.first == 1 &&
           !capture.second && exited && !exit_code ? 0 : 7;
#endif
}
