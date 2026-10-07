#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdlib.h>

typedef NTSTATUS (NTAPI *fn_path)(HANDLE, PSID, BOOLEAN, UNICODE_STRING *);
typedef HRESULT (WINAPI *fn_derive)(const WCHAR *, PSID *);

static fn_path p;

static void show(const char *tag, HANDLE token, PSID sid, BOOLEAN rel)
{
    UNICODE_STRING s;
    NTSTATUS st;
    memset(&s, 0xcc, sizeof(s));
    SetLastError(0xdead);
    st = p(token, sid, rel, &s);
    printf("%s rel=%d: st=%08lx gle=%lx", tag, rel, st, GetLastError());
    if (!st)
    {
        printf(" len=%u max=%u heap=%d str=%.*ls", s.Length, s.MaximumLength,
               HeapValidate(GetProcessHeap(), 0, s.Buffer), s.Length / 2, s.Buffer);
    }
    else printf(" len=%04x max=%04x buf=%p", s.Length, s.MaximumLength, s.Buffer);
    printf("\n");
}

int main(int argc, char **argv)
{
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    int mode = argc > 1 ? atoi(argv[1]) : 0;
    DWORD sess = 0;
    HANDLE tok;
    PSID ac = NULL;
    fn_derive derive = (fn_derive)GetProcAddress(LoadLibraryA("userenv.dll"), "DeriveAppContainerSidFromAppContainerName");

    setvbuf(stdout, NULL, _IONBF, 0);
    p = (fn_path)GetProcAddress(nt, "RtlGetAppContainerNamedObjectPath");
    ProcessIdToSessionId(GetCurrentProcessId(), &sess);
    printf("export=%d session=%lu\n", p != NULL, sess);
    if (!p) return 1;
    if (derive) derive(L"xodus.audit.container", &ac);
    switch (mode)
    {
    case 0:
        show("null/null", NULL, NULL, FALSE);
        show("null/null", NULL, NULL, TRUE);
        OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok);
        show("proctok/null", tok, NULL, FALSE);
        show("proctok/null", tok, NULL, TRUE);
        show("pseudo-4/null", (HANDLE)~(ULONG_PTR)3, NULL, FALSE);
        show("null/ac", NULL, ac, FALSE);
        show("null/ac", NULL, ac, TRUE);
        show("proctok/ac", tok, ac, FALSE);
        OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &tok);
        CloseHandle(tok);
        show("closedtok/null", tok, NULL, FALSE);
        show("badtok/null", (HANDLE)0x1234, NULL, FALSE);
        {
            HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
            show("eventhandle/null", ev, NULL, FALSE);
        }
        {
            OpenProcessToken(GetCurrentProcess(), 0, &tok) || OpenProcessToken(GetCurrentProcess(), READ_CONTROL, &tok);
            show("noquery-tok/null", tok, NULL, FALSE);
        }
        {
            SID world = { SID_REVISION, 1, SECURITY_WORLD_SID_AUTHORITY, { SECURITY_WORLD_RID } };
            show("null/world", NULL, &world, FALSE);
        }
        break;
    case 1:
        printf("nullout st=%08lx\n", p(NULL, NULL, FALSE, NULL));
        break;
    case 2:
    {
        static const struct { BYTE n; DWORD auth; DWORD sub[12]; } sids[] = {
            { 1, 15, { 2 } },
            { 2, 15, { 2, 1 } },
            { 7, 15, { 2, 1, 2, 3, 4, 5, 6 } },
            { 8, 15, { 2, 1, 2, 3, 4, 5, 6, 7 } },
            { 9, 15, { 2, 1, 2, 3, 4, 5, 6, 7, 8 } },
            { 12, 15, { 2, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
            { 8, 15, { 3, 1, 2, 3, 4, 5, 6, 7 } },
            { 8, 5, { 2, 1, 2, 3, 4, 5, 6, 7 } },
        };
        UNICODE_STRING s;
        unsigned i, j;
        for (i = 0; i < ARRAYSIZE(sids); i++)
        {
            SID_IDENTIFIER_AUTHORITY a = { { 0, 0, 0, 0, 0, (BYTE)sids[i].auth } };
            BYTE buf[SECURITY_MAX_SID_SIZE];
            PSID sid = buf;
            InitializeSid(sid, &a, sids[i].n);
            for (j = 0; j < sids[i].n; j++) *GetSidSubAuthority(sid, j) = sids[i].sub[j];
            for (j = 0; j < 2; j++)
            {
                NTSTATUS st;
                memset(&s, 0xcc, sizeof(s));
                st = p(NULL, sid, (BOOLEAN)j, &s);
                printf("sid%u n=%u rel=%u st=%08lx len=%04x max=%04x buf=%s %.*ls\n", i, sids[i].n, j, st, s.Length,
                       s.MaximumLength, s.Buffer ? "set" : "null", st ? 0 : s.Length / 2, st ? L"" : s.Buffer);
            }
        }
        memset(&s, 0xcc, sizeof(s));
        printf("empty st=%08lx len=%u max=%u buf=%p\n", p(NULL, NULL, FALSE, &s), s.Length, s.MaximumLength, s.Buffer);
        break;
    }
    case 3:
    {
        UNICODE_STRING s;
        BYTE buf[SECURITY_MAX_SID_SIZE];
        SID_IDENTIFIER_AUTHORITY a = { { 0, 0, 0, 0, 0, 15 } };
        int k;
        InitializeSid(buf, &a, 1);
        *GetSidSubAuthority(buf, 0) = 2;
        for (k = 0; k < 6; k++)
        {
            NTSTATUS st;
            memset(&s, 0xcc, sizeof(s));
            if (k == 3) { st = p(NULL, buf, FALSE, &s); printf("badsid st=%08lx\n", st); memset(&s, 0xcc, sizeof(s)); }
            if (k == 4) { st = p(NULL, NULL, TRUE, &s); printf("rel st=%08lx len=%04x buf=%p\n", st, s.Length, s.Buffer); memset(&s, 0xcc, sizeof(s)); }
            st = p(NULL, NULL, FALSE, &s);
            printf("empty%d st=%08lx len=%04x max=%04x buf=%p\n", k, st, s.Length, s.MaximumLength, s.Buffer);
        }
        break;
    }
    }
    return 0;
}
