#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <stdio.h>

typedef HRESULT (WINAPI *GET_FACTORY)(HSTRING, void **);
typedef HRESULT (WINAPI *CREATE_STRING)(const WCHAR *, UINT32, HSTRING *);
typedef HRESULT (WINAPI *DELETE_STRING)(HSTRING);
typedef HRESULT (WINAPI *INITIALIZE)(RO_INIT_TYPE);
typedef void (WINAPI *UNINITIALIZE)(void);

struct inspectable
{
    const struct inspectable_vtable *vtable;
};
struct inspectable_vtable
{
    HRESULT (WINAPI *query)(struct inspectable *, const GUID *, void **);
    ULONG (WINAPI *addref)(struct inspectable *);
    ULONG (WINAPI *release)(struct inspectable *);
};

int main(int argc, char **argv)
{
    static const struct
    {
        const WCHAR *name;
        GUID iid;
    } classes[] = {
        {L"Windows.ApplicationModel.Core.CoreApplication",
         {0x1ada0e3e, 0xe4a2, 0x4123, {0xb4, 0x51, 0xdc, 0x96, 0xbf, 0x80, 0x04, 0x19}}},
        {L"Windows.UI.ViewManagement.ApplicationView",
         {0xa28d7594, 0x8c41, 0x4e13, {0x97, 0x19, 0x51, 0x64, 0x79, 0x6f, 0xe4, 0xc7}}},
    };
    HMODULE combase = LoadLibraryW(L"combase.dll"), provider;
    CREATE_STRING create;
    DELETE_STRING delete_string;
    INITIALIZE initialize;
    UNINITIALIZE uninitialize;
    GET_FACTORY factory;
    WCHAR path[MAX_PATH];
    UINT length, i;
    HRESULT initialized, result;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!combase) return 2;
    create = (CREATE_STRING)(ULONG_PTR)GetProcAddress(combase, "WindowsCreateString");
    delete_string = (DELETE_STRING)(ULONG_PTR)GetProcAddress(combase, "WindowsDeleteString");
    initialize = (INITIALIZE)(ULONG_PTR)GetProcAddress(combase, "RoInitialize");
    uninitialize = (UNINITIALIZE)(ULONG_PTR)GetProcAddress(combase, "RoUninitialize");
    if (!create || !delete_string || !initialize || !uninitialize) return 2;
    if (argc == 2) {
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argv[1], -1, path, MAX_PATH))
            return 2;
    } else if (argc == 1) {
        length = GetSystemDirectoryW(path, MAX_PATH);
        if (!length || length >= MAX_PATH - 21) return 2;
        wcscat(path, L"\\twinapi.appcore.dll");
    } else return 2;
    provider = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!provider) {
        printf("CORE_PROVIDER_LOAD_ERROR=%lu\n", GetLastError());
        return 3;
    }
    GetModuleFileNameW(provider, path, MAX_PATH);
    wprintf(L"CORE_PROVIDER_PATH=%ls\n", path);
    factory = (GET_FACTORY)(ULONG_PTR)GetProcAddress(provider, "DllGetActivationFactory");
    if (!factory) return 3;
    initialized = initialize(RO_INIT_MULTITHREADED);
    printf("CORE_RO_INITIALIZE=%08lx\n", (unsigned long)initialized);
    if (FAILED(initialized)) return 4;
    for (i = 0; i < sizeof(classes) / sizeof(classes[0]); i++) {
        struct inspectable *object = NULL, *requested = NULL;
        HSTRING name = NULL;
        result = create(classes[i].name, (UINT32)wcslen(classes[i].name), &name);
        if (FAILED(result)) { uninitialize(); return 4; }
        result = factory(name, (void **)&object);
        printf("CORE_FACTORY index=%u hr=%08lx object=%d\n", i, (unsigned long)result, object != NULL);
        if (SUCCEEDED(result) && object) {
            result = object->vtable->query(object, &classes[i].iid, (void **)&requested);
            printf("CORE_REACHED_IID index=%u hr=%08lx object=%d\n", i, (unsigned long)result, requested != NULL);
            if (requested) requested->vtable->release(requested);
            object->vtable->release(object);
        }
        delete_string(name);
    }
    uninitialize();
    FreeLibrary(provider);
    FreeLibrary(combase);
    return 0;
}
