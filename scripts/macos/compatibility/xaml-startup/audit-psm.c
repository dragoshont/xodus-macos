/* Parity probe for PsmQueryBackgroundActivationType (kernel.appcore / kernelbase).
 * Same binary on native Windows and Wine: x86_64-w64-mingw32-gcc -O1 -o audit-psm.exe audit-psm.c */
#include <windows.h>
#include <stdio.h>

typedef LONG (WINAPI *psm_fn)(HANDLE, ULONG *);

static void run(psm_fn fn, const char *label, HANDLE token)
{
    ULONG type = 0xdeadbeef;
    LONG status = fn(token, &type);
    printf("%s status=%08lx type=%08lx\n", label, (unsigned long)status, (unsigned long)type);
}

int main(void)
{
    const char *dlls[] = { "kernel.appcore.dll", "kernelbase.dll" };
    HANDLE token = NULL;
    int i;

    for (i = 0; i < 2; i++)
    {
        HMODULE mod = LoadLibraryA(dlls[i]);
        psm_fn fn = mod ? (psm_fn)GetProcAddress(mod, "PsmQueryBackgroundActivationType") : NULL;
        printf("[%s] loaded=%d export=%d\n", dlls[i], !!mod, !!fn);
        if (!fn) continue;
        run(fn, "  pseudo-process-token", (HANDLE)(LONG_PTR)-4);
        run(fn, "  pseudo-process-token-again", (HANDLE)(LONG_PTR)-4);
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        {
            run(fn, "  real-process-token", token);
            CloseHandle(token);
        }
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_DEFAULT, &token))
        {
            run(fn, "  token-without-query", token);
            CloseHandle(token);
        }
        run(fn, "  null-token", NULL);
        run(fn, "  invalid-handle", (HANDLE)(LONG_PTR)0x1234);
    }
    printf("EXIT=0\n");
    return 0;
}
