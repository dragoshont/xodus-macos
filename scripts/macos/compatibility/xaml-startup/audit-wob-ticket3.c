/* Follow-up 3: NtSetInformationThread(44), connection port flags, seq after 2nd connect. Get flags after Set, dead/cross-process tickets, per-step SequenceNo. Measures RtlGet/SetThreadWorkOnBehalfTicket, the ThreadWorkOnBehalfTicket query class, ALPC WOB propagation
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

typedef NTSTATUS (NTAPI *fnSIT)(HANDLE, ULONG, void *, ULONG);
static fnSIT pSIT;

static ULONG seq_of(HANDLE port)
{
    BASIC_INFO bi;
    memset(&bi, 0xcc, sizeof(bi));
    if (!port || pQueryAlpc(port, 0, &bi, sizeof(bi), NULL)) return 0xffffffff;
    return bi.SequenceNo;
}

static void cur_ticket(const char *tag)
{
    unsigned char b[16];
    ULONG ret = 0;
    NTSTATUS st;
    char t2[96];

    memset(b, 0xcc, sizeof(b));
    st = pQIT(GetCurrentThread(), 44, b, 16, &ret);
    sprintf(t2, "%s QIT44 status=%08lx", tag, st);
    hex(t2, b, 16);
    memset(b, 0xcc, sizeof(b));
    st = pGetWob(b, 1);
    sprintf(t2, "%s GET1 status=%08lx", tag, st);
    hex(t2, b, 8);
}

static void sit_case(const char *tag, const unsigned char *src, ULONG len, HANDLE thread)
{
    unsigned char b[32];
    NTSTATUS st;
    char t2[128];

    memcpy(b, src, sizeof(b));
    st = pSIT(thread, 44, b, len);
    sprintf(t2, "SIT44 %s len=%lu status=%08lx", tag, len, st);
    printf("%s\n", t2);
    cur_ticket(t2);
}

static DWORD WINAPI sender_thread(void *arg)
{
    LARGE_INTEGER to;
    HANDLE cli;
    NTSTATUS st;

    to.QuadPart = -50000000;
    cli = connect_to(name1, &to);
    if (!cli) return 1;
    st = send_wob(cli, 1, 0, 0, &to);
    printf("C SEND status=%08lx\n", st);
    fflush(stdout);
    Sleep(2000);
    return 0;
}

static void conn_flags(ULONG flags)
{
    UNICODE_STRING us;
    OBJECT_ATTRIBUTES oa;
    PORT_ATTR pa;
    HANDLE h = NULL;
    BASIC_INFO bi;
    WCHAR n[128];
    NTSTATUS st;

    swprintf(n, 128, L"\\BaseNamedObjects\\XodusWob3F%lx_%lu", flags, GetCurrentProcessId());
    RtlInitUnicodeString(&us, n);
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, NULL, NULL);
    memset(&pa, 0, sizeof(pa));
    pa.Flags = flags;
    pa.SecurityQos.Length = sizeof(pa.SecurityQos);
    pa.SecurityQos.ImpersonationLevel = SecurityImpersonation;
    pa.MaxMessageLength = 0x1000;
    st = pCreate(&h, &oa, &pa);
    memset(&bi, 0xcc, sizeof(bi));
    if (!st) st = pQueryAlpc(h, 0, &bi, sizeof(bi), NULL);
    printf("CONNFLAGS attr=%08lx status=%08lx flags=%08lx seq=%lu ctx=%p\n", flags, st, bi.Flags, bi.SequenceNo,
           bi.PortContext);
    if (h) CloseHandle(h);
}

int main(void)
{
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    unsigned char r1[32], zero[32] = { 0 }, fab[32] = { 0x34, 0x12, 0, 0, 0x78, 0x56, 0, 0 }, ident[32];
    HANDLE srv1, comm_a = NULL, comm_b = NULL, th1, th2;
    LARGE_INTEGER to;
    MSGBUF rm1, rm2;
    NTSTATUS st;
    int valid;

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
    pSIT = (fnSIT)GetProcAddress(nt, "NtSetInformationThread");
    if (!pCreate || !pConnect || !pAccept || !pSWR || !pInit || !pGet || !pGetWob || !pSetWob || !pQueryAlpc)
    {
        printf("MISSING_EXPORT\n");
        return 1;
    }
    conn_flags(0);
    conn_flags(0x10000);
    conn_flags(0x70000);
    conn_flags(0x80000);

    to.QuadPart = -50000000;
    swprintf(name1, 128, L"\\BaseNamedObjects\\XodusWob3A%lu", GetCurrentProcessId());
    srv1 = create_port(name1);
    if (!srv1) return 1;
    printf("SEQ srv1_initial=%lu\n", seq_of(srv1));
    th1 = CreateThread(NULL, 0, sender_thread, NULL, 0, NULL);
    if (!simple_server_accept(srv1, &comm_a, (void *)0x5150, &to)) return 1;
    printf("SEQ after_accept_a srv1=%lu comm_a=%lu\n", seq_of(srv1), seq_of(comm_a));
    memset(r1, 0, sizeof(r1));
    st = recv_req(srv1, &rm1, r1, &valid, &to);
    printf("RECV_A status=%08lx srv1=%lu comm_a=%lu\n", st, seq_of(srv1), seq_of(comm_a));
    th2 = CreateThread(NULL, 0, sender_thread, NULL, 0, NULL);
    if (!simple_server_accept(srv1, &comm_b, (void *)0x6160, &to)) return 1;
    printf("SEQ after_accept_b srv1=%lu comm_a=%lu comm_b=%lu\n", seq_of(srv1), seq_of(comm_a), seq_of(comm_b));
    st = recv_req(srv1, &rm2, ident, &valid, &to);
    printf("RECV_B status=%08lx srv1=%lu comm_a=%lu comm_b=%lu\n", st, seq_of(srv1), seq_of(comm_a), seq_of(comm_b));
    memset(ident + 8, 0, 24);

    cur_ticket("INITIAL");
    sit_case("received_ticket8", r1, 8, GetCurrentThread());
    sit_case("zero8", zero, 8, GetCurrentThread());
    sit_case("received_ticket16_flags0", r1, 16, GetCurrentThread());
    sit_case("zero16", zero, 16, GetCurrentThread());
    r1[8] = 1;
    sit_case("received_ticket16_flags1", r1, 16, GetCurrentThread());
    sit_case("zero16b", zero, 16, GetCurrentThread());
    r1[8] = 0;
    sit_case("fab16", fab, 16, GetCurrentThread());
    sit_case("received_ticket32", r1, 32, GetCurrentThread());
    sit_case("received_ticket16_otherthread", r1, 16, th1);
    sit_case("zero16c", zero, 16, GetCurrentThread());
    st = do_reply(comm_a, &rm1.h);
    st = do_reply(comm_b, &rm2.h);
    WaitForSingleObject(th1, 5000);
    WaitForSingleObject(th2, 5000);
    printf("WOB3_DONE\n");
    fflush(stdout);
    return 0;
}