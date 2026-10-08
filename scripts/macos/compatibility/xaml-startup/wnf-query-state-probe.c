/* Measures ntdll!NtQueryWnfStateData for the two state names MrmCoreR's
 * LanguageChangeNotifyThreadProc queries (NULL buffer, zero size, then with a buffer),
 * an unregistered name and NULL arguments. */
#include <windows.h>
#include <stdio.h>

typedef LONG (WINAPI *nqwsd_fn)(const ULONGLONG *, const GUID *, const void *, ULONG *, void *, ULONG *);
static nqwsd_fn nqwsd;

static void run(const char *label, ULONGLONG name, BOOL with_buffer)
{
    BYTE buf[512];
    ULONG stamp = 0xcccccccc, size = with_buffer ? sizeof(buf) : 0, i;
    LONG st;

    memset(buf, 0xcc, sizeof(buf));
    st = nqwsd(&name, NULL, NULL, &stamp, with_buffer ? buf : NULL, &size);
    printf("%s: name=%016llx st=0x%lx stamp=%lu size=%lu", label, name, st, stamp, size);
    if (with_buffer && !st)
    {
        printf(" data=");
        for (i = 0; i < size && i < 96; i++) printf("%02x", buf[i]);
        if (size >= 2) printf(" text=%.*ls", (int)(size / 2), (WCHAR *)buf);
    }
    printf("\n");
}

int main(void)
{
    ULONG stamp, size = 0;
    LONG st;

    setvbuf(stdout, NULL, _IONBF, 0);
    nqwsd = (nqwsd_fn)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryWnfStateData");
    printf("nqwsd=%p\n", nqwsd);
    if (!nqwsd) return 1;
    run("mrm1-size", 0x41950223a3bc1035ull, FALSE);
    run("mrm1-data", 0x41950223a3bc1035ull, TRUE);
    run("mrm2-size", 0x41950223a3bc2035ull, FALSE);
    run("mrm2-data", 0x41950223a3bc2035ull, TRUE);
    run("unknown", 0x41950223a3bcf035ull, FALSE);
    st = nqwsd(NULL, NULL, NULL, &stamp, NULL, &size);
    printf("nullname: st=0x%lx\n", st);
    return 0;
}
