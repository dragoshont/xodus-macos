#ifndef XODUS_DEBUG_EVENT_CAPTURE_H
#define XODUS_DEBUG_EVENT_CAPTURE_H

#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define CAPTURE_THREADS 128
#define CAPTURE_SITES 5

struct capture_site
{
    const WCHAR *module;
    const char *sha256;
    DWORD rva;
    unsigned kind;
};

struct capture_thread
{
    DWORD id;
    HANDLE handle;
    CONTEXT original;
    DWORD64 address[4];
    BOOL installed;
};

struct debug_capture
{
    HANDLE process;
    DWORD pid;
    struct capture_thread threads[CAPTURE_THREADS];
    struct capture_site sites[CAPTURE_SITES];
    DWORD64 bases[CAPTURE_SITES];
    DWORD64 initial_break;
    unsigned hits[CAPTURE_SITES], exceptions[2], thread_count;
    unsigned bound_reason, debug_strings;
    BOOL initial_seen, rotated, exited, failed, bound_hit, cleanup_verified, occupied_error, read_error;
    BOOL request_detach, successful_detach;
    DWORD exit_code;
    ULONGLONG deadline, bytes, byte_limit;
};

static void capture_abort_initial(PROCESS_INFORMATION *process)
{
    BOOL terminated = TerminateProcess(process->hProcess, 95);
    BOOL detached = DebugActiveProcessStop(process->dwProcessId);
    DWORD waited = WaitForSingleObject(process->hProcess, 5000);
    fprintf(stderr, "DEBUG_INITIAL_CLEANUP terminated=%u detached=%u wait=%lu error=%lu\n",
            terminated, detached, (unsigned long)waited, (unsigned long)GetLastError());
    CloseHandle(process->hThread); CloseHandle(process->hProcess);
}

static BOOL capture_hash(HANDLE file, const char *expected)
{
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    BYTE buffer[16384], digest[32];
    DWORD count;
    LARGE_INTEGER saved, zero;
    char hex[65];
    BOOL ok = FALSE;
    unsigned i;
    zero.QuadPart = 0;
    if (!file || file == INVALID_HANDLE_VALUE ||
        !SetFilePointerEx(file, zero, &saved, FILE_CURRENT)) return FALSE;
    if (!SetFilePointerEx(file, zero, NULL, FILE_BEGIN)) return FALSE;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) ||
        BCryptCreateHash(algorithm, &hash, NULL, 0, NULL, 0, 0)) goto done;
    for (;;)
    {
        if (!ReadFile(file, buffer, sizeof(buffer), &count, NULL)) goto done;
        if (!count) break;
        if (BCryptHashData(hash, buffer, count, 0)) goto done;
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

static BOOL capture_read(struct debug_capture *capture, DWORD64 address, void *value, SIZE_T size)
{
    SIZE_T read;
    if (address && ReadProcessMemory(capture->process, (void *)(ULONG_PTR)address,
                                    value, size, &read) && read == size) return TRUE;
    capture->read_error = TRUE;
    fprintf(stderr, "DEBUG_READ_ERROR address=%llx bytes=%llu error=%lu\n",
            (unsigned long long)address, (unsigned long long)size, (unsigned long)GetLastError());
    return FALSE;
}

static struct capture_thread *capture_find_thread(struct debug_capture *capture, DWORD id)
{
    unsigned i;
    for (i = 0; i < capture->thread_count; ++i)
        if (capture->threads[i].id == id) return &capture->threads[i];
    return NULL;
}

static void capture_addresses(struct debug_capture *capture, DWORD64 addresses[4])
{
    unsigned i;
    for (i = 0; i < 3; ++i)
        addresses[i] = capture->bases[i] ? capture->bases[i] + capture->sites[i].rva : 0;
    i = capture->rotated ? 4 : 3;
    addresses[3] = capture->bases[i] ? capture->bases[i] + capture->sites[i].rva : 0;
}

static BOOL capture_install(struct debug_capture *capture, struct capture_thread *thread)
{
    CONTEXT context;
    DWORD64 addresses[4];
    memset(&context, 0, sizeof(context));
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext(thread->handle, &context)) return FALSE;
    if (!thread->installed)
    {
        if (context.Dr0 || context.Dr1 || context.Dr2 || context.Dr3 ||
            (context.Dr7 & 0xff))
        {
            capture->occupied_error = TRUE;
            fprintf(stderr, "DEBUG_OCCUPIED_REGISTERS thread=%lu\n", (unsigned long)thread->id);
            return FALSE;
        }
        thread->original = context;
    }
    capture_addresses(capture, addresses);
    context.Dr0 = addresses[0]; context.Dr1 = addresses[1];
    context.Dr2 = addresses[2]; context.Dr3 = addresses[3];
    context.Dr7 = thread->original.Dr7 & ~((DWORD64)0xffff00ff);
    if (addresses[0]) context.Dr7 |= 1;
    if (addresses[1]) context.Dr7 |= 4;
    if (addresses[2]) context.Dr7 |= 16;
    if (addresses[3]) context.Dr7 |= 64;
    context.Dr6 = thread->original.Dr6;
    thread->installed = TRUE;
    if (!SetThreadContext(thread->handle, &context)) return FALSE;
    memset(&context, 0, sizeof(context));
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext(thread->handle, &context) ||
        context.Dr0 != addresses[0] || context.Dr1 != addresses[1] ||
        context.Dr2 != addresses[2] || context.Dr3 != addresses[3] ||
        (context.Dr7 & 0xffff00ff) !=
        ((addresses[0] ? 1 : 0) | (addresses[1] ? 4 : 0) |
         (addresses[2] ? 16 : 0) | (addresses[3] ? 64 : 0))) return FALSE;
    memcpy(thread->address, addresses, sizeof(addresses));
    return TRUE;
}

static BOOL capture_restore_thread(struct capture_thread *thread)
{
    CONTEXT context;
    if (!thread->installed) return TRUE;
    if (!SetThreadContext(thread->handle, &thread->original)) return FALSE;
    memset(&context, 0, sizeof(context)); context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext(thread->handle, &context) ||
        context.Dr0 != thread->original.Dr0 || context.Dr1 != thread->original.Dr1 ||
        context.Dr2 != thread->original.Dr2 || context.Dr3 != thread->original.Dr3 ||
        context.Dr6 != thread->original.Dr6 || context.Dr7 != thread->original.Dr7) return FALSE;
    thread->installed = FALSE;
    return TRUE;
}

static BOOL capture_refresh(struct debug_capture *capture)
{
    unsigned i;
    for (i = 0; i < capture->thread_count; ++i)
        if (capture->threads[i].handle && !capture_install(capture, &capture->threads[i])) return FALSE;
    return TRUE;
}

static BOOL capture_module(struct debug_capture *capture, HANDLE file, DWORD64 base)
{
    WCHAR path[4096], *name;
    DWORD length;
    unsigned i;
    length = GetFinalPathNameByHandleW(file, path, ARRAY_SIZE(path), FILE_NAME_NORMALIZED);
    if (!length || length >= ARRAY_SIZE(path)) return FALSE;
    name = wcsrchr(path, '\\'); name = name ? name + 1 : path;
    printf("DEBUG_MODULE name=%ls base=%llx\n", name, (unsigned long long)base);
    for (i = 0; i < CAPTURE_SITES; ++i)
    {
        IMAGE_DOS_HEADER dos;
        IMAGE_NT_HEADERS64 nt;
        MEMORY_BASIC_INFORMATION memory;
        if (_wcsicmp(name, capture->sites[i].module)) continue;
        if (capture->bases[i] || !capture_hash(file, capture->sites[i].sha256)) return FALSE;
        if (!capture_read(capture, base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
            dos.e_lfanew > 0x100000 ||
            !capture_read(capture, base + dos.e_lfanew, &nt, sizeof(nt)) ||
            nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            capture->sites[i].rva >= nt.OptionalHeader.SizeOfImage ||
            !VirtualQueryEx(capture->process, (void *)(ULONG_PTR)(base + capture->sites[i].rva),
                            &memory, sizeof(memory)) ||
            memory.State != MEM_COMMIT || memory.Type != MEM_IMAGE ||
            (memory.Protect & PAGE_GUARD) ||
            !(memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                               PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return FALSE;
        capture->bases[i] = base;
        printf("DEBUG_PIN site=%u base=%llx rva=%lx\n", i,
               (unsigned long long)base, (unsigned long)capture->sites[i].rva);
    }
    if (!_wcsicmp(name, L"ntdll.dll"))
    {
        HMODULE local = GetModuleHandleW(L"ntdll.dll");
        WCHAR local_path[4096];
        HANDLE local_file;
        BYTE remote[16], local_bytes[16];
        void *symbol = (void *)(ULONG_PTR)GetProcAddress(local, "DbgBreakPoint");
        if (!symbol || !GetModuleFileNameW(local, local_path, ARRAY_SIZE(local_path))) return FALSE;
        local_file = CreateFileW(local_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                                 NULL, OPEN_EXISTING, 0, NULL);
        if (local_file == INVALID_HANDLE_VALUE) return FALSE;
        CloseHandle(local_file);
        capture->initial_break = base + (DWORD64)((BYTE *)symbol - (BYTE *)local);
        memcpy(local_bytes, symbol, sizeof(local_bytes));
        if (!capture_read(capture, capture->initial_break, remote, sizeof(remote)) ||
            memcmp(local_bytes, remote, sizeof(remote))) return FALSE;
    }
    return capture_refresh(capture);
}

static BOOL capture_dump_record(struct debug_capture *capture, DWORD64 address)
{
    EXCEPTION_RECORD record;
    unsigned depth, i;
    for (depth = 0; address && depth < 4; ++depth)
    {
        if (!capture_read(capture, address, &record, sizeof(record)) ||
            record.NumberParameters > EXCEPTION_MAXIMUM_PARAMETERS) return FALSE;
        printf("DEBUG_RECORD depth=%u pointer=%llx code=%08lx flags=%lx address=%p parameters=%lu\n",
               depth, (unsigned long long)address, (unsigned long)record.ExceptionCode,
               (unsigned long)record.ExceptionFlags, record.ExceptionAddress,
               (unsigned long)record.NumberParameters);
        for (i = 0; i < record.NumberParameters; ++i)
            printf("DEBUG_PARAMETER index=%u value=%llx\n", i,
                   (unsigned long long)record.ExceptionInformation[i]);
        address = (DWORD64)(ULONG_PTR)record.ExceptionRecord;
    }
    return !address;
}

static BOOL capture_hit(struct debug_capture *capture, DEBUG_EVENT *event, DWORD *disposition)
{
    struct capture_thread *thread = capture_find_thread(capture, event->dwThreadId);
    CONTEXT context;
    unsigned slot, index;
    DWORD64 triggered;
    *disposition = DBG_EXCEPTION_NOT_HANDLED;
    if (!thread || !thread->installed || !event->u.Exception.dwFirstChance ||
        event->u.Exception.ExceptionRecord.ExceptionCode != EXCEPTION_SINGLE_STEP) return TRUE;
    memset(&context, 0, sizeof(context));
    context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext(thread->handle, &context)) return FALSE;
    triggered = context.Dr6 & 15;
    if (!triggered || (triggered & (triggered - 1)) || (context.Dr6 & 0xe000)) return TRUE;
    for (slot = 0; slot < 4 && !(triggered & ((DWORD64)1 << slot)); ++slot) {}
    if (slot == 4 || !thread->address[slot] || context.Rip != thread->address[slot] ||
        (DWORD64)(ULONG_PTR)event->u.Exception.ExceptionRecord.ExceptionAddress != context.Rip) return TRUE;
    index = slot == 3 && capture->rotated ? 4 : slot;
    ++capture->hits[index];
    printf("DEBUG_HIT site=%u thread=%lu rip=%llx rcx=%llx rdx=%llx r8=%llx r9=%llx\n",
           index, (unsigned long)event->dwThreadId, (unsigned long long)context.Rip,
           (unsigned long long)context.Rcx, (unsigned long long)context.Rdx,
           (unsigned long long)context.R8, (unsigned long long)context.R9);
    {
        DWORD64 stack[16];
        unsigned i;
        if (!capture_read(capture, context.Rsp, stack, sizeof(stack))) return FALSE;
        printf("DEBUG_RAW_STACK rsp=%llx", (unsigned long long)context.Rsp);
        for (i = 0; i < ARRAY_SIZE(stack); ++i) printf(" %llx", (unsigned long long)stack[i]);
        puts("");
    }
    if (capture->sites[index].kind == 1)
    {
        CONTEXT original;
        DISPATCHER_CONTEXT dispatch;
        if (!capture_dump_record(capture, context.Rcx) ||
            !capture_read(capture, context.R8, &original, sizeof(original)) ||
            !capture_read(capture, context.R9, &dispatch, sizeof(dispatch))) return FALSE;
        printf("DEBUG_GUARD original_rip=%llx original_rsp=%llx control_pc=%llx image=%llx handler=%p\n",
               (unsigned long long)original.Rip, (unsigned long long)original.Rsp,
               (unsigned long long)dispatch.ControlPc, (unsigned long long)dispatch.ImageBase,
               dispatch.LanguageHandler);
    }
    else if (capture->sites[index].kind == 2)
    {
        CONTEXT original;
        if (!capture_dump_record(capture, context.Rdx) ||
            !capture_read(capture, context.R8, &original, sizeof(original))) return FALSE;
        printf("DEBUG_PINVOKE control_pc=%llx original_rip=%llx original_rsp=%llx\n",
               (unsigned long long)context.Rcx, (unsigned long long)original.Rip,
               (unsigned long long)original.Rsp);
    }
    else if (capture->sites[index].kind == 3)
    {
        DWORD64 output;
        if (!capture_read(capture, context.Rsp + 0x28, &output, sizeof(output))) return FALSE;
        printf("DEBUG_VECTOR hr=%08lx output=%llx\n", (unsigned long)(DWORD)context.Rax,
               (unsigned long long)output);
    }
    if (index == 3)
    {
        capture->rotated = TRUE;
        if (!capture_refresh(capture)) return FALSE;
    }
    /* RF suppresses only the matching execution trap for this instruction. */
    context.ContextFlags = CONTEXT_CONTROL | CONTEXT_DEBUG_REGISTERS;
    context.EFlags |= 0x10000;
    context.Dr6 = thread->original.Dr6;
    context.Dr0 = thread->address[0]; context.Dr1 = thread->address[1];
    context.Dr2 = thread->address[2]; context.Dr3 = thread->address[3];
    context.Dr7 = thread->original.Dr7 & ~((DWORD64)0xffff00ff);
    if (thread->address[0]) context.Dr7 |= 1;
    if (thread->address[1]) context.Dr7 |= 4;
    if (thread->address[2]) context.Dr7 |= 16;
    if (thread->address[3]) context.Dr7 |= 64;
    if (!SetThreadContext(thread->handle, &context)) return FALSE;
    *disposition = DBG_CONTINUE;
    return TRUE;
}

static BOOL capture_loop(struct debug_capture *capture, DWORD timeout_ms, ULONGLONG byte_limit)
{
    DEBUG_EVENT event;
    DWORD disposition, code;
    BOOL pending = FALSE;
    unsigned i;
    capture->deadline = GetTickCount64() + timeout_ms;
    capture->byte_limit = byte_limit;
    while (!capture->exited)
    {
        __int64 output = _ftelli64(stdout);
        if (output < 0 || (ULONGLONG)output > byte_limit || GetTickCount64() > capture->deadline)
        {
            capture->failed = TRUE;
            capture->bound_hit = output >= 0;
            capture->bound_reason = output < 0 ? 0 : (ULONGLONG)output > byte_limit ? 2 : 1;
            break;
        }
        if (!WaitForDebugEvent(&event, 100))
        {
            if (GetLastError() == ERROR_SEM_TIMEOUT) continue;
            capture->failed = TRUE; break;
        }
        pending = TRUE; disposition = DBG_CONTINUE;
        if (event.dwProcessId != capture->pid) { capture->failed = TRUE; break; }
        switch (event.dwDebugEventCode)
        {
        case CREATE_PROCESS_DEBUG_EVENT:
        case CREATE_THREAD_DEBUG_EVENT:
        {
            HANDLE handle = event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT ?
                event.u.CreateProcessInfo.hThread : event.u.CreateThread.hThread;
            struct capture_thread *thread;
            if (capture->thread_count == CAPTURE_THREADS) { capture->failed = TRUE; CloseHandle(handle); break; }
            thread = &capture->threads[capture->thread_count++];
            thread->id = event.dwThreadId; thread->handle = handle;
            if (event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT)
            {
                if (!capture_module(capture, event.u.CreateProcessInfo.hFile,
                                    (DWORD64)(ULONG_PTR)event.u.CreateProcessInfo.lpBaseOfImage)) capture->failed = TRUE;
                if (event.u.CreateProcessInfo.hFile) CloseHandle(event.u.CreateProcessInfo.hFile);
                if (event.u.CreateProcessInfo.hProcess != capture->process)
                    CloseHandle(event.u.CreateProcessInfo.hProcess);
            }
            else if (!capture_install(capture, thread)) capture->failed = TRUE;
            break;
        }
        case LOAD_DLL_DEBUG_EVENT:
            if (!event.u.LoadDll.hFile || !capture_module(capture, event.u.LoadDll.hFile,
                 (DWORD64)(ULONG_PTR)event.u.LoadDll.lpBaseOfDll)) capture->failed = TRUE;
            if (event.u.LoadDll.hFile) CloseHandle(event.u.LoadDll.hFile);
            break;
        case UNLOAD_DLL_DEBUG_EVENT:
            for (i = 0; i < CAPTURE_SITES; ++i)
                if (capture->bases[i] == (DWORD64)(ULONG_PTR)event.u.UnloadDll.lpBaseOfDll)
                    capture->failed = TRUE;
            break;
        case EXIT_THREAD_DEBUG_EVENT:
        {
            struct capture_thread *thread = capture_find_thread(capture, event.dwThreadId);
            if (!thread) capture->failed = TRUE;
            else { CloseHandle(thread->handle); thread->handle = NULL; }
            break;
        }
        case EXCEPTION_DEBUG_EVENT:
            code = event.u.Exception.ExceptionRecord.ExceptionCode;
            if (!capture->initial_seen && event.u.Exception.dwFirstChance &&
                code == EXCEPTION_BREAKPOINT && capture->initial_break &&
                (DWORD64)(ULONG_PTR)event.u.Exception.ExceptionRecord.ExceptionAddress == capture->initial_break)
                capture->initial_seen = TRUE;
            else
            {
                ++capture->exceptions[event.u.Exception.dwFirstChance ? 0 : 1];
                printf("DEBUG_EXCEPTION chance=%u thread=%lu code=%08lx address=%p\n",
                       event.u.Exception.dwFirstChance ? 1 : 2, (unsigned long)event.dwThreadId,
                       (unsigned long)code, event.u.Exception.ExceptionRecord.ExceptionAddress);
                if (!capture_hit(capture, &event, &disposition)) capture->failed = TRUE;
            }
            break;
        case EXIT_PROCESS_DEBUG_EVENT:
            capture->exited = TRUE;
            capture->exit_code = event.u.ExitProcess.dwExitCode;
            break;
        case OUTPUT_DEBUG_STRING_EVENT:
        {
            BYTE string[4096];
            SIZE_T size = event.u.DebugString.nDebugStringLength *
                (event.u.DebugString.fUnicode ? sizeof(WCHAR) : 1);
            if (size > sizeof(string) || !capture_read(capture,
                (DWORD64)(ULONG_PTR)event.u.DebugString.lpDebugStringData, string, size))
                capture->failed = TRUE;
            else
            {
                SIZE_T j;
                ++capture->debug_strings;
                printf("DEBUG_STRING bytes=%llu data=", (unsigned long long)size);
                for (j = 0; j < size; ++j) printf("%02x", string[j]);
                puts("");
            }
            break;
        }
        }
        if (capture->failed) break;
        if (capture->request_detach && capture->hits[4])
        {
            BOOL restored = TRUE;
            for (i = 0; i < capture->thread_count; ++i)
                if (capture->threads[i].handle && !capture_restore_thread(&capture->threads[i])) restored = FALSE;
            if (!restored) { capture->failed = TRUE; break; }
            if (!ContinueDebugEvent(event.dwProcessId, event.dwThreadId, disposition))
            { capture->failed = TRUE; break; }
            pending = FALSE;
            capture->successful_detach = DebugActiveProcessStop(capture->pid);
            if (!capture->successful_detach) capture->failed = TRUE;
            printf("DEBUG_LIVE_DETACH restored=%u detached=%u\n", restored, capture->successful_detach);
            break;
        }
        if (event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT)
            printf("DEBUG_DISPOSITION chance=%u code=%08lx status=%08lx\n",
                   event.u.Exception.dwFirstChance ? 1 : 2,
                   (unsigned long)event.u.Exception.ExceptionRecord.ExceptionCode,
                   (unsigned long)disposition);
        if (!ContinueDebugEvent(event.dwProcessId, event.dwThreadId, disposition))
        { capture->failed = TRUE; break; }
        pending = FALSE;
    }
    if (capture->failed && !capture->exited)
    {
        BOOL restored = TRUE, terminated, detached, quiet;
        /* Only the exact process handle created by this controller is terminated. */
        if (!pending)
        {
            for (i = 0; i < capture->thread_count; ++i)
                if (capture->threads[i].handle &&
                    SuspendThread(capture->threads[i].handle) == (DWORD)-1) restored = FALSE;
        }
        for (i = 0; i < capture->thread_count; ++i)
            if (capture->threads[i].handle && !capture_restore_thread(&capture->threads[i]))
            {
                restored = FALSE;
                fprintf(stderr, "DEBUG_RESTORE_ERROR thread=%lu error=%lu\n",
                        (unsigned long)capture->threads[i].id, (unsigned long)GetLastError());
            }
        terminated = TerminateProcess(capture->process, 95);
        if (!terminated)
            fprintf(stderr, "DEBUG_TERMINATE_ERROR error=%lu\n", (unsigned long)GetLastError());
        if (pending)
        {
            if (!ContinueDebugEvent(event.dwProcessId, event.dwThreadId, DBG_EXCEPTION_NOT_HANDLED))
            {
                restored = FALSE;
                fprintf(stderr, "DEBUG_CONTINUE_CLEANUP_ERROR error=%lu\n", (unsigned long)GetLastError());
            }
            pending = FALSE;
        }
        detached = DebugActiveProcessStop(capture->pid);
        if (!detached)
            fprintf(stderr, "DEBUG_DETACH_ERROR error=%lu\n", (unsigned long)GetLastError());
        quiet = WaitForSingleObject(capture->process, 5000) == WAIT_OBJECT_0;
        if (!quiet)
            fprintf(stderr, "DEBUG_OWNED_PROCESS_NOT_QUIESCENT\n");
        capture->cleanup_verified = restored && terminated && detached && quiet;
        printf("DEBUG_CLEANUP restored=%u terminated=%u detached=%u quiet=%u\n",
               restored, terminated, detached, quiet);
    }
    for (i = 0; i < capture->thread_count; ++i)
        if (capture->threads[i].handle) CloseHandle(capture->threads[i].handle);
    printf("DEBUG_RESULT failed=%u exited=%u exit=%lu initial=%u rotated=%u hits=%u,%u,%u,%u,%u first=%u second=%u\n",
           capture->failed, capture->exited, (unsigned long)capture->exit_code,
           capture->initial_seen, capture->rotated, capture->hits[0], capture->hits[1],
           capture->hits[2], capture->hits[3], capture->hits[4],
           capture->exceptions[0], capture->exceptions[1]);
    {
        __int64 size = _ftelli64(stdout);
        if (size < 0 || (ULONGLONG)size > byte_limit)
        {
            capture->failed = TRUE;
            fprintf(stderr, "DEBUG_FINAL_CAPTURE_BOUND_OR_ACCOUNTING_ERROR\n");
        }
    }
    return !capture->failed && (capture->exited || capture->successful_detach);
}

#endif
