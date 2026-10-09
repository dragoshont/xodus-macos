#define COBJMACROS
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef HRESULT (WINAPI *FACTORY)(HSTRING, void **);
typedef HRESULT (WINAPI *REGISTER)(HSTRING *, FACTORY *, UINT32, void **);
typedef void (WINAPI *REVOKE)(void *);
static FACTORY genuine_factory;
static unsigned int callback_count;

static LONG WINAPI report_exception(EXCEPTION_POINTERS *exception)
{
    printf("CALLBACK_EXCEPTION code=%08lx address=%p\n",
           (unsigned long)exception->ExceptionRecord->ExceptionCode,
           exception->ExceptionRecord->ExceptionAddress);
    fflush(stdout);
    ExitProcess(4);
    return EXCEPTION_EXECUTE_HANDLER;
}

static HRESULT WINAPI delegate_factory(HSTRING name, void **factory)
{
    HRESULT status;
    ++callback_count;
    status = genuine_factory(name, factory);
    printf("ACTUAL_CALLBACK class=%ls hr=%08lx factory=%p\n",
           WindowsGetStringRawBuffer(name, NULL), (unsigned long)status, *factory);
    return status;
}

int main(int argc, char **argv)
{
    HMODULE combase = LoadLibraryW(L"combase.dll"), twin;
    REGISTER reg = (REGISTER)(ULONG_PTR)GetProcAddress(combase, "RoRegisterActivationFactories");
    REVOKE revoke = (REVOKE)(ULONG_PTR)GetProcAddress(combase, "RoRevokeActivationFactories");
    HSTRING names[4];
    FACTORY callbacks[4] = {delegate_factory, delegate_factory, delegate_factory, delegate_factory};
    WCHAR *data, *cursor;
    FILE *file;
    long bytes;
    void *cookie;
    HRESULT status;
    unsigned int i;
    SetErrorMode(SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(report_exception);
    if ((argc != 2 && argc != 3) || !reg || !revoke || !(file = fopen(argv[1], "rb"))) return 2;
    fseek(file, 0, SEEK_END); bytes = ftell(file); rewind(file);
    if (bytes < 4 || bytes > 128 * 1024 || bytes % 2 || !(data = malloc(bytes))) return 2;
    if (fread(data, 1, bytes, file) != (size_t)bytes) return 2;
    fclose(file);
    if (data[bytes / 2 - 1] || data[bytes / 2 - 2]) return 2;
    cursor = data;
    for (i = 0; i < 4; ++i)
    {
        if (!*cursor || WindowsCreateString(cursor, wcslen(cursor), &names[i]) != S_OK) return 2;
        cursor += wcslen(cursor) + 1;
    }
    if (argc == 3 && !strcmp(argv[2], "--client"))
    {
        static const IID factory_iid = {0x00000035, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
        if (FAILED(RoInitialize(RO_INIT_MULTITHREADED))) return 2;
        for (i = 0; i < 4; ++i)
        {
            void *factory = NULL;
            status = RoGetActivationFactory(names[i], &factory_iid, &factory);
            printf("PAIRED_CLIENT_REQUEST class=%ls hr=%08lx factory=%p\n",
                   WindowsGetStringRawBuffer(names[i], NULL), (unsigned long)status, factory);
            if (factory) IUnknown_Release((IUnknown *)factory);
            WindowsDeleteString(names[i]);
        }
        RoUninitialize();
        free(data); FreeLibrary(combase);
        return 0;
    }
    twin = LoadLibraryExW(L"twinapi.appcore.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    genuine_factory = twin ? (FACTORY)(ULONG_PTR)GetProcAddress(twin, "DllGetActivationFactory") : NULL;
    if (!genuine_factory) return 2;
    cookie = (void *)(ULONG_PTR)0x1234;
    status = reg(names, callbacks, 4, &cookie);
    printf("WITHOUT_APARTMENT hr=%08lx cookie=%p callbacks=%u\n",
           (unsigned long)status, cookie, callback_count);
    if (SUCCEEDED(status)) { revoke(cookie); cookie = NULL; }
    if (FAILED(RoInitialize(RO_INIT_MULTITHREADED))) return 2;
    if (argc == 3 && !strcmp(argv[2], "--app-callback"))
    {
        static const unsigned char signature[] = {0x48, 0x89, 0x5c, 0x24, 0x18, 0x55, 0x56, 0x57};
        FACTORY app_callback = (FACTORY)((BYTE *)twin + 0x2fc90);
        void *factory = NULL;
        if (memcmp((const void *)app_callback, signature, sizeof(signature))) return 2;
        printf("VERIFIED_ORIGINAL_CALLBACK_RVA=2fc90 class=%ls\n", WindowsGetStringRawBuffer(names[0], NULL));
        fflush(stdout);
        status = app_callback(names[0], &factory);
        printf("ORIGINAL_CALLBACK_RESULT=%08lx factory=%p\n", (unsigned long)status, factory);
        if (factory) IUnknown_Release((IUnknown *)factory);
        return 0;
    }
    cookie = (void *)(ULONG_PTR)0x1234;
    status = reg(names, callbacks, 4, &cookie);
    printf("UNPACKAGED_MTA_REGISTER hr=%08lx cookie=%p callbacks=%u\n",
           (unsigned long)status, cookie, callback_count);
    fflush(stdout);
    if (getenv("XODUS_PAIR_HOLD")) Sleep(3000);
    if (SUCCEEDED(status)) { revoke(cookie); printf("OWNED_COOKIE_REVOKED\n"); }
    for (i = 0; i < 4; ++i)
    {
        void *factory = NULL;
        status = genuine_factory(names[i], &factory);
        printf("GENUINE_TWIN_FACTORY class=%ls hr=%08lx factory=%p\n",
               WindowsGetStringRawBuffer(names[i], NULL), (unsigned long)status, factory);
        if (factory) IUnknown_Release((IUnknown *)factory);
        WindowsDeleteString(names[i]);
    }
    revoke(NULL);
    RoUninitialize();
    FreeLibrary(twin); FreeLibrary(combase); free(data);
    return 0;
}
