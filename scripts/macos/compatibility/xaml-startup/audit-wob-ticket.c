/* Measures RtlGet/SetThreadWorkOnBehalfTicket, the ThreadWorkOnBehalfTicket query class, ALPC WOB propagation
 * through a forwarding server thread, and NtAlpcQueryInformation(AlpcBasicInformation). Run on native and Wine. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
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

static DWORD WINAPI client_thread(void *arg)
{
    LARGE_INTEGER to;
    unsigned char fab[16] = { 0x34, 0x12, 0, 0, 0x78, 0x56, 0, 0 };
    HANDLE cli;
    NTSTATUS st;

    to.QuadPart = -50000000;
    memset(client_ticket, 0xcc, sizeof(client_ticket));
    st = pGetWob(client_ticket, 1);
    printf("C GET1 status=%08lx", st);
    hex(" bytes", client_ticket, 16);
    describe("C GET1", client_ticket);
    cli = connect_to(name1, &to);
    if (!cli) return 1;
    port1_client = cli;
    st = send_wob(cli, 1, 0, 0, &to);
    printf("C SEND1 status=%08lx\n", st);
    st = pSetWob(fab);
    printf("C SET_FAB status=%08lx\n", st);
    memcpy(client_fab_sent, fab, 8);
    st = send_wob(cli, 2, 0x34, 0x56, &to);
    printf("C SEND2 status=%08lx\n", st);
    st = pSetWob(client_ticket);
    printf("C SET_RESTORE status=%08lx\n", st);
    st = send_wob(cli, 3, 0, 0, &to);
    printf("C SEND3 status=%08lx\n", st);
    fflush(stdout);
    Sleep(500);
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

static void teb_diff(const unsigned char *a, const unsigned char *b, int n)
{
    int i, shown = 0;
    for (i = 0; i < n; i += 8)
        if (memcmp(a + i, b + i, 8) && shown++ < 16)
            printf("  TEB+%04x %016llx -> %016llx\n", i, *(unsigned long long *)(a + i),
                   *(unsigned long long *)(b + i));
    printf("  TEB diffs=%d\n", shown);
}
#define TEBN 0x1800
static unsigned char teb_a[TEBN], teb_b[TEBN];

int main(void)
{
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    unsigned char orig[16], fab[16] = { 0x34, 0x12, 0, 0, 0x78, 0x56, 0, 0 }, cur[16], r[8], saved[16];
    HANDLE srv1, srv2, th_c;
    LARGE_INTEGER to;
    MSGBUF rm;
    NTSTATUS st;
    int i, valid;
    LONG before;

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
    printf("EXPORTS get=%d set=%d qalpc=%d\n", !!pGetWob, !!pSetWob, !!pQueryAlpc);
    if (!pCreate || !pConnect || !pAccept || !pSWR || !pInit || !pGet || !pGetWob || !pSetWob || !pQueryAlpc)
        return 1;

    /* A: local semantics */
    get_variants("MAIN");
    qit_variants("MAIN", GetCurrentThread());
    th_c = CreateThread(NULL, 0, client_thread, NULL, CREATE_SUSPENDED, NULL);
    qit_variants("MAIN_OTHERTHREAD", th_c);

    memset(orig, 0xcc, sizeof(orig));
    st = pGetWob(orig, 1);
    memcpy(teb_a, NtCurrentTeb(), TEBN);
    st = pSetWob(fab);
    memcpy(teb_b, NtCurrentTeb(), TEBN);
    printf("MAIN SET_FAB status=%08lx\n", st);
    teb_diff(teb_a, teb_b, TEBN);
    get_variants("MAIN_AFTER_FAB");
    qit_variants("MAIN_AFTER_FAB", GetCurrentThread());
    memcpy(teb_a, NtCurrentTeb(), TEBN);
    st = pSetWob(orig);
    memcpy(teb_b, NtCurrentTeb(), TEBN);
    printf("MAIN SET_RESTORE status=%08lx\n", st);
    teb_diff(teb_a, teb_b, TEBN);
    memset(cur, 0xcc, sizeof(cur));
    pGetWob(cur, 1);
    printf("MAIN after_restore_equals_orig16=%d equals_orig8=%d\n", !memcmp(cur, orig, 16), !memcmp(cur, orig, 8));
    fab[8] = 1;
    st = pSetWob(fab);
    printf("MAIN SET_FAB_FLAG1 status=%08lx\n", st);
    get_variants("MAIN_AFTER_FAB_FLAG1");
    st = pSetWob(orig);
    printf("MAIN SET_RESTORE2 status=%08lx\n", st);
    memset(cur, 0xcc, sizeof(cur));
    pGetWob(cur, 1);
    printf("MAIN after_restore2_equals_orig16=%d\n", !memcmp(cur, orig, 16));
    fflush(stdout);

    /* B: propagation */
    to.QuadPart = -50000000;
    swprintf(name1, 128, L"\\BaseNamedObjects\\XodusWobProbeA%lu", GetCurrentProcessId());
    swprintf(name2, 128, L"\\BaseNamedObjects\\XodusWobProbeB%lu", GetCurrentProcessId());
    srv1 = create_port(name1);
    srv2 = create_port(name2);
    if (!srv1 || !srv2) return 1;
    CloseHandle(CreateThread(NULL, 0, port2_thread, srv2, 0, NULL));
    while (!t_ready) Sleep(10);
    port2_client = connect_to(name2, &to);
    if (!port2_client) return 1;
    ResumeThread(th_c);
    if (!simple_server_accept(srv1, &port1_comm, (void *)0x5150, &to)) return 1;
    query_basic("SRV1_BEFORE", srv1);
    query_basic("COMM1_BEFORE", port1_comm);
    query_basic("CLI2_BEFORE", port2_client);
    for (i = 0; i < 3; i++)
    {
        st = recv_req(srv1, &rm, r, &valid, &to);
        if (st) { printf("S recv status=%08lx\n", st); break; }
        printf("S_RECV tag=%u valid=%d eq_client_get1=%d eq_fab=%d", rm.data[0], valid, !memcmp(r, client_ticket, 8),
               !memcmp(r, client_fab_sent, 8));
        hex(" ticket", r, 8);
        memset(saved, 0xcc, sizeof(saved));
        pGetWob(saved, 1);
        st = pSetWob(r);
        memset(cur, 0xcc, sizeof(cur));
        pGetWob(cur, 1);
        printf("S SET_RECEIVED status=%08lx get1_eq_received=%d", st, !memcmp(cur, r, 8));
        hex(" get1", cur, 16);
        before = t_count;
        st = send_wob(port2_client, (unsigned char)(10 + i), 0, 0, &to);
        if (!st) while (t_count == before) Sleep(5);
        printf("S FORWARD status=%08lx t_eq_received=%d t_eq_s_own=%d\n", st, !memcmp(last_t_ticket, r, 8),
               !memcmp(last_t_ticket, saved, 8));
        st = pSetWob(saved);
        memset(cur, 0xcc, sizeof(cur));
        pGetWob(cur, 1);
        printf("S SET_RESTORE status=%08lx get1_eq_saved16=%d\n", st, !memcmp(cur, saved, 16));
        before = t_count;
        st = send_wob(port2_client, (unsigned char)(20 + i), 0, 0, &to);
        if (!st) while (t_count == before) Sleep(5);
        printf("S FORWARD_RESTORED status=%08lx t_eq_received=%d t_eq_s_own=%d\n", st,
               !memcmp(last_t_ticket, r, 8), !memcmp(last_t_ticket, saved, 8));
        st = do_reply(port1_comm, &rm.h);
        printf("S reply status=%08lx\n", st);
        fflush(stdout);
    }
    WaitForSingleObject(th_c, 10000);
    query_basic("SRV1_AFTER", srv1);
    query_basic("COMM1_AFTER", port1_comm);
    query_basic("CLI2_AFTER", port2_client);
    query_basic("INVALID", (HANDLE)0x1234);
    printf("WOB_DONE\n");
    fflush(stdout);
    return 0;
}