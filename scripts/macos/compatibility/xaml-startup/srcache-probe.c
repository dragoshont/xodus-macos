/* Measures the StateRepository cache entry points reached by MrmCoreR
 * StateRepositoryHelper::ArePackageMrtResourcesRedirected. */
#include <windows.h>
#include <stdio.h>

typedef HRESULT (WINAPI *open_fn)(UINT32, void **);
typedef void (WINAPI *close_fn)(void *);

int main(int argc, char **argv)
{
    static const char *names[] = {
        "ext-ms-onecore-appmodel-staterepository-cache-l1-1-0.dll",
        "windows.staterepositorycore.dll",
        "windows.staterepositoryclient.dll",
    };
    unsigned int i;
    HKEY key;
    LONG r;

    r = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AppModel\\StateRepository\\Cache", 0, KEY_READ, &key);
    printf("HKLM cache key: %ld\n", r);
    if (!r) RegCloseKey(key);

    for (i = 0; i < ARRAYSIZE(names); i++)
    {
        HMODULE mod = LoadLibraryA(names[i]), owner = NULL;
        char path[MAX_PATH] = "";
        open_fn open_p;
        close_fn close_p;
        void *mgr = (void *)0xdeadbeef;
        HRESULT hr;
        UINT32 flags;

        printf("%s: mod=%p err=%lu\n", names[i], mod, mod ? 0 : GetLastError());
        if (!mod) continue;
        open_p = (open_fn)GetProcAddress(mod, "SRCacheManager_Open");
        close_p = (close_fn)GetProcAddress(mod, "SRCacheManager_Close");
        if (open_p && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (const char *)open_p, &owner))
            GetModuleFileNameA(owner, path, sizeof(path));
        printf("  open=%p close=%p owner=%s\n", open_p, close_p, path);
        if (!open_p) continue;
        for (flags = 0; flags < (argc > 1 ? 4u : 1u); flags++)
        {
            mgr = (void *)0xdeadbeef;
            hr = open_p(flags, &mgr);
            printf("  Open(%u) hr=%#lx mgr=%p\n", flags, hr, mgr);
            if (SUCCEEDED(hr) && mgr && close_p) close_p(mgr);
        }
    }
    return 0;
}
