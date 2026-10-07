/* NtAlpcCreatePort on an existing name, with and without OBJ_OPENIF. Run on native and Wine. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

typedef NTSTATUS (NTAPI *pNtAlpcCreatePort)(HANDLE *, OBJECT_ATTRIBUTES *, void *);
#ifndef OBJ_OPENIF
#define OBJ_OPENIF 0x80
#endif

int main(void)
{
    pNtAlpcCreatePort create = (pNtAlpcCreatePort)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtAlpcCreatePort");
    WCHAR name[128];
    UNICODE_STRING us;
    OBJECT_ATTRIBUTES oa;
    HANDLE first = NULL, second = NULL, third = NULL;
    NTSTATUS st;
    if (!create) { printf("NO_NTALPCCREATEPORT\n"); return 1; }
    swprintf(name, 128, L"\\BaseNamedObjects\\XodusOpenIfProbe%lu", GetCurrentProcessId());
    RtlInitUnicodeString(&us, name);
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, NULL, NULL);
    st = create(&first, &oa, NULL);
    printf("OPENIF first status=%08lx handle_nonnull=%u\n", st, first != NULL);
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE | OBJ_OPENIF, NULL, NULL);
    st = create(&second, &oa, NULL);
    printf("OPENIF second_openif status=%08lx handle_nonnull=%u\n", st, second != NULL);
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, NULL, NULL);
    st = create(&third, &oa, NULL);
    printf("OPENIF third_plain status=%08lx handle_nonnull=%u\n", st, third != NULL);
    printf("OPENIF_DONE\n");
    return 0;
}
