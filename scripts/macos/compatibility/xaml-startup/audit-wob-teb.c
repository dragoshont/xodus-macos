/* Locates the TEB storage written by a successful RtlSetThreadWorkOnBehalfTicket. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <string.h>
typedef NTSTATUS (NTAPI *fnGetWob)(void *, ULONG);
typedef NTSTATUS (NTAPI *fnSetWob)(void *);
typedef NTSTATUS (NTAPI *fnQIT)(HANDLE, ULONG, void *, ULONG, ULONG *);
typedef NTSTATUS (NTAPI *fnSIT)(HANDLE, ULONG, void *, ULONG);
#define TEBN 0x1838
static unsigned char a[TEBN], b[TEBN];
static void diff(const char *tag)
{
    int i, n = 0;
    for (i = 0; i < TEBN; i += 8)
        if (memcmp(a + i, b + i, 8) && n++ < 16)
            printf("%s TEB+%04x %016llx -> %016llx\n", tag, i, *(unsigned long long *)(a + i), *(unsigned long long *)(b + i));
    printf("%s diffs=%d\n", tag, n);
}
int main(void)
{
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    fnGetWob pGet = (fnGetWob)GetProcAddress(nt, "RtlGetThreadWorkOnBehalfTicket");
    fnSetWob pSet = (fnSetWob)GetProcAddress(nt, "RtlSetThreadWorkOnBehalfTicket");
    fnQIT pQIT = (fnQIT)GetProcAddress(nt, "NtQueryInformationThread");
    fnSIT pSIT = (fnSIT)GetProcAddress(nt, "NtSetInformationThread");
    unsigned char ident[16], zero[16] = { 0 }, g[16];
    ULONG ret;
    NTSTATUS s1, s2;
    if (!pGet || !pSet) { printf("MISSING\n"); return 1; }
    s1 = pQIT(GetCurrentThread(), 44, ident, 16, &ret);
    printf("QIT44 status=%08lx ident=%08lx:%08lx flags=%lu teb=%p\n", s1, ((ULONG *)ident)[0], ((ULONG *)ident)[1], ((ULONG *)ident)[2], NtCurrentTeb());
    memcpy(a, NtCurrentTeb(), TEBN); s2 = pSet(ident); memcpy(b, NtCurrentTeb(), TEBN);
    printf("RTLSET_OWN_IDENT status=%08lx\n", s2); diff("SET_IDENT");
    memset(g, 0xcc, 16); s1 = pGet(g, 1);
    printf("GET1 status=%08lx %08lx:%08lx\n", s1, ((ULONG *)g)[0], ((ULONG *)g)[1]);
    memset(g, 0xcc, 16); s1 = pQIT(GetCurrentThread(), 44, g, 16, &ret);
    printf("QIT44_AFTER status=%08lx %08lx:%08lx flags=%lu\n", s1, ((ULONG *)g)[0], ((ULONG *)g)[1], ((ULONG *)g)[2]);
    memcpy(a, NtCurrentTeb(), TEBN); s2 = pSet(zero); memcpy(b, NtCurrentTeb(), TEBN);
    printf("RTLSET_ZERO status=%08lx\n", s2); diff("SET_ZERO");
    memcpy(a, NtCurrentTeb(), TEBN); s2 = pSIT(GetCurrentThread(), 44, ident, 8); memcpy(b, NtCurrentTeb(), TEBN);
    printf("SIT44_IDENT status=%08lx\n", s2); diff("SIT_IDENT");
    memset(g, 0xcc, 16); s1 = pGet(g, 0);
    printf("GET0_AFTER_SIT status=%08lx %08lx:%08lx\n", s1, ((ULONG *)g)[0], ((ULONG *)g)[1]);
    s2 = pSIT(GetCurrentThread(), 44, zero, 8);
    printf("SIT44_ZERO status=%08lx\n", s2);
    printf("TEBWOB_DONE\n");
    return 0;
}