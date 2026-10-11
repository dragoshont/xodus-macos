#define _WIN32_WINNT 0x0602
#include <windows.h>
#include <objbase.h>
#include <stdio.h>

typedef HRESULT (WINAPI *QUERY_ID)(UINT64 *);
static QUERY_ID query;
static unsigned checks, failures;

static void check(int valid, const char *name)
{
    checks++;
    if (!valid) { failures++; printf("FAIL %s\n", name); }
}

struct worker
{
    HANDLE ready, release;
    UINT64 id, repeated;
    HRESULT initialized, result, repeat_result;
    BOOL sta;
};

static DWORD WINAPI apartment_worker(void *argument)
{
    struct worker *worker = argument;
    worker->initialized = worker->sta ? CoInitializeEx(NULL, COINIT_APARTMENTTHREADED) : S_FALSE;
    worker->result = query(&worker->id);
    worker->repeat_result = query(&worker->repeated);
    SetEvent(worker->ready);
    WaitForSingleObject(worker->release, 10000);
    if (worker->sta && SUCCEEDED(worker->initialized)) CoUninitialize();
    return 0;
}

int main(void)
{
    HMODULE module = LoadLibraryW(L"combase.dll");
    FARPROC ordinal = module ? GetProcAddress(module, (LPCSTR)122) : NULL;
    UINT64 id = ~(UINT64)0, repeated = 0, old;
    HRESULT result;
    unsigned i, j;
    struct worker workers[3] = {{0}};
    HANDLE threads[3] = {0};

    query = (QUERY_ID)(ULONG_PTR)ordinal;
    check(query != NULL, "private ordinal 122 present");
    if (!query) goto done;
    check(ordinal != GetProcAddress(module, "CoGetSystemSecurityPermissions"),
          "private apartment ordinal is not named security policy API");
    check(query(NULL) == E_POINTER, "documented defensive NULL-output divergence");
    check(query(&id) == CO_E_NOTINITIALIZED && !id, "no apartment clears identity and fails");
    result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    check(SUCCEEDED(result), "real STA initialized");
    if (FAILED(result)) goto done;
    check(query(&id) == S_OK && id, "STA has real instance identifier");
    check(query(&repeated) == S_OK && repeated == id, "STA identifier stable");
    old = id;
    CoUninitialize();
    id = ~(UINT64)0;
    check(query(&id) == CO_E_NOTINITIALIZED && !id, "destroyed STA leaves no identity");
    result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    check(SUCCEEDED(result), "same thread reinitializes STA");
    if (FAILED(result)) goto done;
    check(query(&id) == S_OK && id && id != old, "same-TID new STA has new identity");
    CoUninitialize();
    result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    check(SUCCEEDED(result), "real process MTA initialized");
    if (FAILED(result)) goto done;
    check(query(&id) == S_OK && id, "MTA has real instance identifier");
    check(query(&repeated) == S_OK && repeated == id, "MTA identifier stable");
    for (i = 0; i < 3; i++)
    {
        workers[i].sta = i != 0;
        workers[i].ready = CreateEventW(NULL, TRUE, FALSE, NULL);
        workers[i].release = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (workers[i].ready && workers[i].release)
            threads[i] = CreateThread(NULL, 0, apartment_worker, &workers[i], 0, NULL);
        check(threads[i] != NULL, "identity worker created");
        if (!threads[i]) continue;
        check(WaitForSingleObject(workers[i].ready, 10000) == WAIT_OBJECT_0 &&
              SUCCEEDED(workers[i].initialized) && workers[i].result == S_OK &&
              workers[i].id && workers[i].repeat_result == S_OK &&
              workers[i].repeated == workers[i].id, "worker has stable apartment instance");
    }
    check(workers[0].id == id, "uninitialized thread uses live implicit MTA identity");
    for (i = 1; i < 3; i++)
    {
        check(workers[i].id && workers[i].id != id, "STA worker differs from process MTA");
        for (j = 1; j < i; j++)
            check(workers[i].id != workers[j].id, "simultaneous STAs have distinct identities");
    }
    for (i = 0; i < 3; i++)
    {
        if (workers[i].release) SetEvent(workers[i].release);
        if (threads[i])
        {
            check(WaitForSingleObject(threads[i], 10000) == WAIT_OBJECT_0, "worker exits normally");
            CloseHandle(threads[i]);
        }
        if (workers[i].ready) CloseHandle(workers[i].ready);
        if (workers[i].release) CloseHandle(workers[i].release);
    }
    CoUninitialize();
    id = ~(UINT64)0;
    check(query(&id) == CO_E_NOTINITIALIZED && !id, "last apartment teardown clears identity");
done:
    printf("RESULT apartment_identifier_checks=%u failures=%u\n", checks, failures);
    if (module) FreeLibrary(module);
    return failures ? 1 : 0;
}
