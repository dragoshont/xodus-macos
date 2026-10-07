/* Follow-up: Get flags after Set, dead/cross-process tickets, per-step SequenceNo. Measures RtlGet/SetThreadWorkOnBehalfTicket, the ThreadWorkOnBehalfTicket query class, ALPC WOB propagation
 * through a forwarding server thread, and NtAlpcQueryInformation(AlpcBasicInformation). Run on native and Wine. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    union { struct { USHORT DataLength, TotalLength; } s1; ULONG Length; } u1;
    union { struct { USHORT Type, DataInfoOffset; } s2; ULONG ZeroInit; } u2;
    union { CLIENT_ID ClientId; double DoNotUseThisField; };
    ULONG MessageId;
    union { SIZE_T ClientViewSize; ULONG CallbackId; };
} PORT_MSG;

typedef struct
{
    ULONG Flags;
    SECURITY_QUALITY_OF_SERVICE SecurityQos;
    SIZE_T MaxMessageLength, MemoryBandwidth, MaxPoolUsage, MaxSectionSize, MaxViewSize, MaxTotalSectionSize;
    ULONG DupObjectTypes;
#ifdef _WIN64
    ULONG Reserved;
#endif
} PORT_ATTR;

typedef struct { ULONG AllocatedAttributes, ValidAttributes; } MSG_ATTRS;
typedef struct { ULONG ThreadId, ThreadCreationTimeLow; } WOB_ATTR;
typedef struct { ULONG Flags, SequenceNo; void *PortContext; } BASIC_INFO;

#define ATTR_WOB      0x02000000
#define MSGFLG_SYNC   0x20000
#define MSGFLG_REPLY  0x10000

typedef NTSTATUS (NTAPI *fnCreatePort)(HANDLE *, OBJECT_ATTRIBUTES *, PORT_ATTR *);
typedef NTSTATUS (NTAPI *fnConnectPort)(HANDLE *, UNICODE_STRING *, OBJECT_ATTRIBUTES *, PORT_ATTR *, ULONG, PSID,
                                        PORT_MSG *, SIZE_T *, MSG_ATTRS *, MSG_ATTRS *, LARGE_INTEGER *);
typedef NTSTATUS (NTAPI *fnAccept)(HANDLE *, HANDLE, ULONG, OBJECT_ATTRIBUTES *, PORT_ATTR *, void *, PORT_MSG *,
                                   MSG_ATTRS *, BOOLEAN);
typedef NTSTATUS (NTAPI *fnSWR)(HANDLE, ULONG, PORT_MSG *, MSG_ATTRS *, PORT_MSG *, SIZE_T *, MSG_ATTRS *, LARGE_INTEGER *);
typedef NTSTATUS (NTAPI *fnInitAttr)(ULONG, MSG_ATTRS *, SIZE_T, SIZE_T *);
typedef void *(NTAPI *fnGetAttr)(MSG_ATTRS *, ULONG);
typedef NTSTATUS (NTAPI *fnGetWob)(void *, ULONG);
typedef NTSTATUS (NTAPI *fnSetWob)(void *);
typedef NTSTATUS (NTAPI *fnQueryAlpc)(HANDLE, ULONG, void *, ULONG, ULONG *);
typedef NTSTATUS (NTAPI *fnQIT)(HANDLE, ULONG, void *, ULONG, ULONG *);

static fnCreatePort pCreate;
static fnConnectPort pConnect;
static fnAccept pAccept;
static fnSWR pSWR;
static fnInitAttr pInit;
static fnGetAttr pGet;
static fnGetWob pGetWob;
static fnSetWob pSetWob;
static fnQueryAlpc pQueryAlpc;
static fnQIT pQIT;

#define DATA 16
typedef struct { PORT_MSG h; unsigned char data[0x200]; } MSGBUF;

static WCHAR name1[128], name2[128];
static HANDLE port2_client, port2_comm, port1_comm, port1_client;
static unsigned char client_ticket[16], client_fab_sent[8];
static volatile LONG t_ready;
static unsigned char last_t_ticket[8];
static volatile LONG t_count;

static ULONG own_low(void)
{
    FILETIME ct, et, kt, ut;
    GetThreadTimes(GetCurrentThread(), &ct, &et, &kt, &ut);
    return ct.dwLowDateTime;
}

static void hex(const char *tag, const unsigned char *b, int n)
{
    int i;
    printf("%s", tag);
    for (i = 0; i < n; i++) printf("%s%02x", (i % 4) ? "" : " ", b[i]);
    printf("\n");
}

static void describe(const char *tag, const unsigned char *b)
{
    ULONG t = ((ULONG *)b)[0], l = ((ULONG *)b)[1];
    printf("%s word0_is_tid=%d word1_is_ctime_low=%d word0_zero=%d word1_zero=%d\n", tag,
           t == GetCurrentThreadId(), l == own_low(), t == 0, l == 0);
}

static MSG_ATTRS *alloc_attrs(ULONG flags)
{
    SIZE_T need = 0;
    MSG_ATTRS *a;
    pInit(flags, NULL, 0, &need);
    a = calloc(1, need);
    pInit(flags, a, need, &need);
    return a;
}

static void get_variants(const char *who)
{
    static const ULONG flags[] = { 0, 1, 2, 3, 0x80000000 };
    unsigned char b[32];
    char tag[96];
    ULONG i;
    NTSTATUS st;

    for (i = 0; i < sizeof(flags) / sizeof(flags[0]); i++)
    {
        memset(b, 0xcc, sizeof(b));
        st = pGetWob(b, flags[i]);
        sprintf(tag, "%s GET flags=%08lx status=%08lx bytes", who, flags[i], st);
        hex(tag, b, 24);
        sprintf(tag, "%s GET flags=%08lx", who, flags[i]);
        describe(tag, b);
    }
}

static void qit_variants(const char *who, HANDLE thread)
{
    static const ULONG lens[] = { 0, 4, 8, 16, 32 };
    unsigned char b[32];
    char tag[96];
    ULONG i, ret;
    NTSTATUS st;

    for (i = 0; i < sizeof(lens) / sizeof(lens[0]); i++)
    {
        memset(b, 0xcc, sizeof(b));
        ret = 0xdead;
        st = pQIT(thread, 44, b, lens[i], &ret);
        sprintf(tag, "%s QIT44 len=%lu status=%08lx ret=%lx bytes", who, lens[i], st, ret);
        hex(tag, b, 24);
    }
}

static int simple_server_accept(HANDLE srv, HANDLE *comm, void *ctx, LARGE_INTEGER *to)
{
    MSGBUF rm;
    SIZE_T len = sizeof(rm);
    PORT_ATTR pa;
    NTSTATUS st;

    memset(&rm, 0, sizeof(rm));
    st = pSWR(srv, 0, NULL, NULL, &rm.h, &len, NULL, to);
    if (st || (rm.h.u2.s2.Type & 0xff) != 10) { printf("ACCEPT_RECV status=%08lx type=%u\n", st, rm.h.u2.s2.Type & 0xff); return 0; }
    memset(&pa, 0, sizeof(pa));
    pa.SecurityQos.Length = sizeof(pa.SecurityQos);
    pa.SecurityQos.ImpersonationLevel = SecurityImpersonation;
    pa.MaxMessageLength = 0x1000;
    st = pAccept(comm, srv, 0, NULL, &pa, ctx, &rm.h, NULL, TRUE);
    printf("ACCEPT status=%08lx\n", st);
    return !st;
}

static HANDLE connect_to(const WCHAR *name, LARGE_INTEGER *to)
{
    UNICODE_STRING us;
    PORT_ATTR pa;
    MSGBUF cm;
    SIZE_T len;
    HANDLE h = NULL;
    NTSTATUS st;

    RtlInitUnicodeString(&us, name);
    memset(&pa, 0, sizeof(pa));
    pa.SecurityQos.Length = sizeof(pa.SecurityQos);
    pa.SecurityQos.ImpersonationLevel = SecurityAnonymous;
    pa.MaxMessageLength = 0x1000;
    memset(&cm, 0, sizeof(cm));
    cm.h.u1.s1.TotalLength = sizeof(PORT_MSG);
    len = sizeof(cm);
    st = pConnect(&h, &us, NULL, &pa, MSGFLG_SYNC, NULL, &cm.h, &len, NULL, NULL, to);
    printf("CONNECT status=%08lx\n", st);
    return st ? NULL : h;
}

static HANDLE create_port(const WCHAR *name)
{
    UNICODE_STRING us;
    OBJECT_ATTRIBUTES oa;
    PORT_ATTR pa;
    HANDLE h = NULL;
    NTSTATUS st;

    RtlInitUnicodeString(&us, name);
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, NULL, NULL);
    memset(&pa, 0, sizeof(pa));
    pa.SecurityQos.Length = sizeof(pa.SecurityQos);
    pa.SecurityQos.ImpersonationLevel = SecurityImpersonation;
    pa.MaxMessageLength = 0x1000;
    st = pCreate(&h, &oa, &pa);
    printf("CREATE status=%08lx\n", st);
    return st ? NULL : h;
}

/* sync request carrying a WOB attribute; returns status */
static NTSTATUS send_wob(HANDLE port, unsigned char tag, ULONG fill_tid, ULONG fill_low, LARGE_INTEGER *to)
{
    MSGBUF sm, rm;
    SIZE_T len = sizeof(rm);
    MSG_ATTRS *sa = alloc_attrs(ATTR_WOB);
    WOB_ATTR *w = pGet(sa, ATTR_WOB);
    NTSTATUS st;

    w->ThreadId = fill_tid;
    w->ThreadCreationTimeLow = fill_low;
    sa->ValidAttributes = ATTR_WOB;
    memset(&sm, 0, sizeof(sm));
    memset(&rm, 0, sizeof(rm));
    sm.h.u1.s1.DataLength = DATA;
    sm.h.u1.s1.TotalLength = sizeof(PORT_MSG) + DATA;
    sm.data[0] = tag;
    st = pSWR(port, MSGFLG_SYNC, &sm.h, sa, &rm.h, &len, NULL, to);
    free(sa);
    return st;
}

/* receives one request with WOB allocated and copies the ticket out */
static NTSTATUS recv_req(HANDLE srv, MSGBUF *rm, unsigned char *ticket, int *valid, LARGE_INTEGER *to)
{
    SIZE_T len = sizeof(*rm);
    MSG_ATTRS *ra = alloc_attrs(ATTR_WOB);
    NTSTATUS st;

    memset(rm, 0, sizeof(*rm));
    st = pSWR(srv, 0, NULL, NULL, &rm->h, &len, ra, to);
    if (st) { free(ra); return st; }
    *valid = !!(ra->ValidAttributes & ATTR_WOB);
    memcpy(ticket, pGet(ra, ATTR_WOB), 8);
    free(ra);
    return 0;
}

static NTSTATUS do_reply(HANDLE comm, PORT_MSG *req)
{
    MSGBUF reply;

    memset(&reply, 0, sizeof(reply));
    reply.h = *req;
    reply.h.u1.s1.DataLength = DATA;
    reply.h.u1.s1.TotalLength = sizeof(PORT_MSG) + DATA;
    reply.h.u2.ZeroInit = 0;
    return pSWR(comm, MSGFLG_REPLY, &reply.h, NULL, NULL, NULL, NULL, NULL);
}
static DWORD WINAPI port2_thread(void *arg)
{
    HANDLE srv = arg;
    LARGE_INTEGER to;
    MSGBUF rm;
    unsigned char t[8];
    int valid;
    NTSTATUS st;

    to.QuadPart = -50000000;
    InterlockedExchange(&t_ready, 1);
    if (!simple_server_accept(srv, &port2_comm, (void *)0x6160, &to)) return 1;
    for (;;)
    {
        st = recv_req(srv, &rm, t, &valid, &to);
        if (st) { printf("T recv status=%08lx\n", st); break; }
        printf("T_RECV tag=%u type=%u valid=%d", rm.data[0], rm.h.u2.s2.Type & 0xff, valid);
        hex(" ticket", t, 8);
        memcpy(last_t_ticket, t, 8);
        InterlockedIncrement(&t_count);
        st = do_reply(port2_comm, &rm.h);
        if (st) printf("T reply status=%08lx\n", st);
        fflush(stdout);
    }
    return 0;
}

static void query_basic(const char *who, HANDLE port)
{
    static const ULONG lens[] = { 16, 8, 32, 0 };
    BASIC_INFO bi[2];
    ULONG i, ret;
    NTSTATUS st;

    for (i = 0; i < sizeof(lens) / sizeof(lens[0]); i++)
    {
        memset(bi, 0xcc, sizeof(bi));
        ret = 0xdead;
        st = pQueryAlpc(port, 0, bi, lens[i], &ret);
        printf("%s QBASIC len=%lu status=%08lx ret=%lx flags=%08lx seq=%08lx ctx=%p\n", who, lens[i], st, ret,
               bi[0].Flags, bi[0].SequenceNo, bi[0].PortContext);
    }
    memset(bi, 0xcc, sizeof(bi));
    st = pQueryAlpc(port, 0, bi, sizeof(bi[0]), NULL);
    printf("%s QBASIC nullret status=%08lx flags=%08lx seq=%08lx ctx=%p\n", who, st, bi[0].Flags,
           bi[0].SequenceNo, bi[0].PortContext);
}

static ULONG seq_of(HANDLE port)
{
    BASIC_INFO bi;
    memset(&bi, 0xcc, sizeof(bi));
    if (!port || pQueryAlpc(port, 0, &bi, sizeof(bi), NULL)) return 0xffffffff;
    return bi.SequenceNo;
}

static HANDLE srv1_global;

static void seqs(const char *tag)
{
    printf("SEQ %s srv1=%lu comm1=%lu cli1=%lu cli2=%lu comm2=%lu\n", tag, seq_of(srv1_global), seq_of(port1_comm),
           seq_of(port1_client), seq_of(port2_client), seq_of(port2_comm));
}

static void get_all(const char *tag)
{
    unsigned char b[16];
    ULONG f, ret;
    NTSTATUS st;
    char t2[96];

    for (f = 0; f < 3; f++)
    {
        memset(b, 0xcc, sizeof(b));
        st = pGetWob(b, f);
        sprintf(t2, "%s GET%lu status=%08lx", tag, f, st);
        hex(t2, b, 12);
    }
    memset(b, 0xcc, sizeof(b));
    st = pQIT(GetCurrentThread(), 44, b, 16, &ret);
    sprintf(t2, "%s QIT44 status=%08lx", tag, st);
    hex(t2, b, 16);
}

static DWORD WINAPI client2_thread(void *arg)
{
    LARGE_INTEGER to;
    HANDLE cli;
    NTSTATUS st;
    int i;

    to.QuadPart = -50000000;
    cli = connect_to(name1, &to);
    if (!cli) return 1;
    port1_client = cli;
    for (i = 1; i <= 2; i++)
    {
        st = send_wob(cli, (unsigned char)i, 0, 0, &to);
        printf("C SEND%d status=%08lx\n", i, st);
        seqs("C_AFTER_SEND");
        fflush(stdout);
    }
    return 0;
}

static int child_main(const WCHAR *name)
{
    LARGE_INTEGER to;
    HANDLE cli;
    NTSTATUS st;

    to.QuadPart = -50000000;
    cli = connect_to(name, &to);
    if (!cli) return 1;
    st = send_wob(cli, 7, 0, 0, &to);
    printf("CHILD SEND status=%08lx\n", st);
    return 0;
}

int main(void)
{
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    unsigned char r1[8], rx[8], zero[16] = { 0 }, tail[16];
    HANDLE srv1, srv2, th_c, comm_x = NULL;
    LARGE_INTEGER to;
    MSGBUF rm;
    NTSTATUS st;
    int valid, argc;
    WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    WCHAR cmd[512];

    pCreate = (fnCreatePort)GetProcAddress(nt, "NtAlpcCreatePort");
    pConnect = (fnConnectPort)GetProcAddress(nt, "NtAlpcConnectPort");
    pAccept = (fnAccept)GetProcAddress(nt, "NtAlpcAcceptConnectPort");
    pSWR = (fnSWR)GetProcAddress(nt, "NtAlpcSendWaitReceivePort");
    pInit = (fnInitAttr)GetProcAddress(nt, "AlpcInitializeMessageAttribute");
    pGet = (fnGetAttr)GetProcAddress(nt, "AlpcGetMessageAttribute");
    pGetWob = (fnGetWob)GetProcAddress(nt, "RtlGetThreadWorkOnBehalfTicket");
    pSetWob = (fnSetWob)GetProcAddress(nt, "RtlSetThreadWorkOnBehalfTicket");
    pQueryAlpc = (fnQueryAlpc)GetProcAddress(nt, "NtAlpcQueryInformation");
    pQIT = (fnQIT)GetProcAddress(nt, "NtQueryInformationThread");
    if (!pCreate || !pConnect || !pAccept || !pSWR || !pInit || !pGet || !pGetWob || !pSetWob || !pQueryAlpc)
    {
        printf("MISSING_EXPORT\n");
        return 1;
    }
    if (argc >= 3 && !wcscmp(argv[1], L"child")) return child_main(argv[2]);

    to.QuadPart = -50000000;
    swprintf(name1, 128, L"\\BaseNamedObjects\\XodusWob2A%lu", GetCurrentProcessId());
    swprintf(name2, 128, L"\\BaseNamedObjects\\XodusWob2B%lu", GetCurrentProcessId());
    srv1 = create_port(name1);
    srv2 = create_port(name2);
    if (!srv1 || !srv2) return 1;
    srv1_global = srv1;
    CloseHandle(CreateThread(NULL, 0, port2_thread, srv2, 0, NULL));
    while (!t_ready) Sleep(10);
    port2_client = connect_to(name2, &to);
    seqs("AFTER_CONNECT2");
    th_c = CreateThread(NULL, 0, client2_thread, NULL, 0, NULL);
    if (!simple_server_accept(srv1, &port1_comm, (void *)0x5150, &to)) return 1;
    Sleep(100);
    seqs("AFTER_ACCEPT1");
    get_all("S_INITIAL");

    st = recv_req(srv1, &rm, r1, &valid, &to);
    printf("S RECV1 status=%08lx valid=%d\n", st, valid);
    seqs("AFTER_RECV1");
    st = pSetWob(r1);
    printf("S SET_R1 status=%08lx\n", st);
    get_all("S_AFTER_SET_R1");
    memcpy(tail, r1, 8);
    memset(tail + 8, 0xcc, 8);
    st = pSetWob(zero);
    printf("S SET_ZERO status=%08lx\n", st);
    get_all("S_AFTER_SET_ZERO");
    st = pSetWob(tail);
    printf("S SET_R1_GARBAGE_TAIL status=%08lx\n", st);
    get_all("S_AFTER_SET_TAIL");
    pSetWob(zero);
    st = send_wob(port2_client, 30, 0, 0, &to);
    seqs("AFTER_FWD_SEND");
    st = do_reply(port1_comm, &rm.h);
    printf("S REPLY1 status=%08lx\n", st);
    Sleep(100);
    seqs("AFTER_REPLY1");
    st = recv_req(srv1, &rm, rx, &valid, &to);
    printf("S RECV2 status=%08lx\n", st);
    seqs("AFTER_RECV2");
    st = do_reply(port1_comm, &rm.h);
    printf("S REPLY2 status=%08lx\n", st);
    WaitForSingleObject(th_c, 10000);
    Sleep(200);
    seqs("AFTER_CLIENT_EXIT");
    st = pSetWob(r1);
    printf("S SET_DEAD_THREAD_TICKET status=%08lx\n", st);
    get_all("S_AFTER_SET_DEAD");
    pSetWob(zero);

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    swprintf(cmd, 512, L"\"%s\" child %s", argv[0], name1);
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) { printf("CREATEPROCESS failed %lu\n", GetLastError()); return 1; }
    if (!simple_server_accept(srv1, &comm_x, (void *)0x7170, &to)) return 1;
    st = recv_req(srv1, &rm, rx, &valid, &to);
    printf("S RECVX status=%08lx valid=%d eq_r1=%d\n", st, valid, !memcmp(rx, r1, 8));
    st = pSetWob(rx);
    printf("S SET_CROSSPROC_LIVE status=%08lx\n", st);
    get_all("S_AFTER_SET_XLIVE");
    pSetWob(zero);
    st = do_reply(comm_x, &rm.h);
    printf("S REPLYX status=%08lx\n", st);
    WaitForSingleObject(pi.hProcess, 10000);
    Sleep(200);
    st = pSetWob(rx);
    printf("S SET_CROSSPROC_DEAD status=%08lx\n", st);
    get_all("S_AFTER_SET_XDEAD");
    printf("WOB2_DONE\n");
    fflush(stdout);
    return 0;
}
