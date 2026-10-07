/* ALPC sync request with valid CONTEXT / WORK_ON_BEHALF send attributes; receiver allocates 0xe2000000.
 * Prints statuses and the attributes the receiver and the client observe. Run on native and Wine. */
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
typedef struct { void *PortContext, *MessageContext; ULONG Sequence, MessageId, CallbackId; } CTX_ATTR;
typedef struct { ULONG ThreadId, ThreadCreationTimeLow; } WOB_ATTR;

#define ATTR_SECURITY 0x80000000
#define ATTR_CONTEXT  0x20000000
#define ATTR_WOB      0x02000000
#define MSGFLG_SYNC   0x20000
#define MSGFLG_REPLY  0x1

typedef NTSTATUS (NTAPI *fnCreatePort)(HANDLE *, OBJECT_ATTRIBUTES *, PORT_ATTR *);
typedef NTSTATUS (NTAPI *fnConnectPort)(HANDLE *, UNICODE_STRING *, OBJECT_ATTRIBUTES *, PORT_ATTR *, ULONG, PSID,
                                        PORT_MSG *, SIZE_T *, MSG_ATTRS *, MSG_ATTRS *, LARGE_INTEGER *);
typedef NTSTATUS (NTAPI *fnAccept)(HANDLE *, HANDLE, ULONG, OBJECT_ATTRIBUTES *, PORT_ATTR *, void *, PORT_MSG *,
                                   MSG_ATTRS *, BOOLEAN);
typedef NTSTATUS (NTAPI *fnSWR)(HANDLE, ULONG, PORT_MSG *, MSG_ATTRS *, PORT_MSG *, SIZE_T *, MSG_ATTRS *, LARGE_INTEGER *);
typedef NTSTATUS (NTAPI *fnInitAttr)(ULONG, MSG_ATTRS *, SIZE_T, SIZE_T *);
typedef void *(NTAPI *fnGetAttr)(MSG_ATTRS *, ULONG);

static fnCreatePort pCreate;
static fnConnectPort pConnect;
static fnAccept pAccept;
static fnSWR pSWR;
static fnInitAttr pInit;
static fnGetAttr pGet;
static WCHAR port_name[128];
static volatile ULONG client_tid, client_ctime_low, server_tid;
static HANDLE client_port_handle;

#define DATA 16
typedef struct { PORT_MSG h; unsigned char data[0x200]; } MSGBUF;

static const struct { ULONG valid; ULONG tid, tlow; } cases[] =
{
    { ATTR_CONTEXT | ATTR_WOB, 0x77, 0x88 },
    { ATTR_CONTEXT, 0, 0 },
    { ATTR_WOB, 0, 0 },
    { ATTR_WOB, 0x77, 0x88 },
    { 0, 0, 0 },
    { 0, 0, 0 },
    { 0, 0, 0 },
};
/* server reply flags per case: 0x10000 delivers an LPC_REPLY (type 2) natively; 0x1 is also accepted natively */
static const ULONG reply_flags[] = { 0x10000, 0x10000, 0x10000, 0x10000, 0x10000, 0x10000, 0x1 };

static MSG_ATTRS *alloc_attrs(ULONG flags)
{
    SIZE_T need = 0;
    MSG_ATTRS *a;
    pInit(flags, NULL, 0, &need);
    a = calloc(1, need);
    pInit(flags, a, need, &need);
    return a;
}

static void dump_attrs(const char *who, int idx, MSG_ATTRS *a)
{
    printf("%s case=%d valid=%08lx", who, idx, a->ValidAttributes);
    if (a->ValidAttributes & ATTR_CONTEXT)
    {
        CTX_ATTR *c = pGet(a, ATTR_CONTEXT);
        printf(" ctx_port_is_client_handle=%d", c->PortContext == client_port_handle);
        printf(" ctx_port=%p ctx_msg=%p seq_nonzero=%u msgid_nonzero=%u cb=%lu", c->PortContext, c->MessageContext,
               c->Sequence != 0, c->MessageId != 0, c->CallbackId);
    }
    if (a->ValidAttributes & ATTR_WOB)
    {
        WOB_ATTR *w = pGet(a, ATTR_WOB);
        printf(" wob_tid=%s wob_low=%s", w->ThreadId == 0 ? "zero" : w->ThreadId == 0x77 ? "0x77" : "other",
               w->ThreadCreationTimeLow == 0 ? "zero" : w->ThreadCreationTimeLow == 0x88 ? "0x88" : "other");
        printf(" wob_tid_is_client=%d wob_tid_is_server=%d wob_low_is_client_ctime=%d",
               w->ThreadId == client_tid, w->ThreadId == server_tid, w->ThreadCreationTimeLow == client_ctime_low);
        printf(" raw_tid=%08lx raw_low=%08lx client_tid=%08lx client_ctime_low=%08lx server_tid=%08lx",
               w->ThreadId, w->ThreadCreationTimeLow, client_tid, client_ctime_low, server_tid);
    }
    printf("\n");
}

static DWORD WINAPI client_thread(void *arg)
{
    UNICODE_STRING us;
    PORT_ATTR pa;
    MSGBUF cm, sm, rm;
    SIZE_T len;
    HANDLE cli = NULL;
    LARGE_INTEGER to;
    NTSTATUS st;
    int i;

    {
        FILETIME ct, et, kt, ut;
        GetThreadTimes(GetCurrentThread(), &ct, &et, &kt, &ut);
        client_ctime_low = ct.dwLowDateTime;
        client_tid = GetCurrentThreadId();
    }
    RtlInitUnicodeString(&us, port_name);
    memset(&pa, 0, sizeof(pa));
    pa.SecurityQos.Length = sizeof(pa.SecurityQos);
    pa.SecurityQos.ImpersonationLevel = SecurityAnonymous;
    pa.MaxMessageLength = 0x1000;
    memset(&cm, 0, sizeof(cm));
    cm.h.u1.s1.TotalLength = sizeof(PORT_MSG);
    len = sizeof(cm);
    to.QuadPart = -50000000;
    st = pConnect(&cli, &us, NULL, &pa, MSGFLG_SYNC, NULL, &cm.h, &len, NULL, NULL, &to);
    client_port_handle = cli;
    printf("CLIENT connect status=%08lx\n", st);
    fflush(stdout);
    if (st) return 1;
    for (i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++)
    {
        MSG_ATTRS *sa = alloc_attrs(0xe2000000), *ra = alloc_attrs(0xe2000000);
        CTX_ATTR *c = pGet(sa, ATTR_CONTEXT);
        WOB_ATTR *w = pGet(sa, ATTR_WOB);
        c->PortContext = (void *)0x1111;
        c->MessageContext = (void *)0x2222;
        c->Sequence = 3;
        c->MessageId = 4;
        c->CallbackId = 5;
        w->ThreadId = cases[i].tid;
        w->ThreadCreationTimeLow = cases[i].tlow;
        sa->ValidAttributes = cases[i].valid;
        memset(&sm, 0, sizeof(sm));
        memset(&rm, 0, sizeof(rm));
        sm.h.u1.s1.DataLength = DATA;
        sm.h.u1.s1.TotalLength = sizeof(PORT_MSG) + DATA;
        sm.data[0] = (unsigned char)i;
        len = sizeof(rm);
        st = pSWR(cli, MSGFLG_SYNC, &sm.h, sa, &rm.h, &len, ra, &to);
        printf("CLIENT case=%d send_valid=%08lx status=%08lx reply_type=%u reply_len=%u\n", i, cases[i].valid, st,
               rm.h.u2.s2.Type & 0xff, rm.h.u1.s1.DataLength);
        if (!st) dump_attrs("CLIENT_REPLY", i, ra);
        fflush(stdout);
        free(sa);
        free(ra);
    }
    CloseHandle(cli);
    return 0;
}

int main(void)
{
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    UNICODE_STRING us;
    OBJECT_ATTRIBUTES oa;
    PORT_ATTR pa;
    MSGBUF rm, reply;
    SIZE_T len;
    HANDLE srv = NULL, comm = NULL, th;
    LARGE_INTEGER to;
    MSG_ATTRS *ra;
    NTSTATUS st;
    int idle = 0, done = 0;

    pCreate = (fnCreatePort)GetProcAddress(nt, "NtAlpcCreatePort");
    pConnect = (fnConnectPort)GetProcAddress(nt, "NtAlpcConnectPort");
    pAccept = (fnAccept)GetProcAddress(nt, "NtAlpcAcceptConnectPort");
    pSWR = (fnSWR)GetProcAddress(nt, "NtAlpcSendWaitReceivePort");
    pInit = (fnInitAttr)GetProcAddress(nt, "AlpcInitializeMessageAttribute");
    pGet = (fnGetAttr)GetProcAddress(nt, "AlpcGetMessageAttribute");
    if (!pCreate || !pConnect || !pAccept || !pSWR || !pInit || !pGet) { printf("MISSING_EXPORT\n"); return 1; }

    swprintf(port_name, 128, L"\\BaseNamedObjects\\XodusSendAttrProbe%lu", GetCurrentProcessId());
    RtlInitUnicodeString(&us, port_name);
    InitializeObjectAttributes(&oa, &us, OBJ_CASE_INSENSITIVE, NULL, NULL);
    memset(&pa, 0, sizeof(pa));
    pa.SecurityQos.Length = sizeof(pa.SecurityQos);
    pa.SecurityQos.ImpersonationLevel = SecurityImpersonation;
    pa.MaxMessageLength = 0x1000;
    st = pCreate(&srv, &oa, &pa);
    printf("SERVER create status=%08lx\n", st);
    if (st) return 1;
    server_tid = GetCurrentThreadId();
    th = CreateThread(NULL, 0, client_thread, NULL, 0, NULL);
    to.QuadPart = -20000000;
    while (idle < 3 && !done)
    {
        ra = alloc_attrs(0xe2000000);
        memset(&rm, 0, sizeof(rm));
        len = sizeof(rm);
        st = pSWR(srv, 0, NULL, NULL, &rm.h, &len, ra, &to);
        if (st == STATUS_TIMEOUT) { idle++; free(ra); continue; }
        idle = 0;
        printf("SERVER recv status=%08lx type=%u len=%u\n", st, rm.h.u2.s2.Type & 0xff, rm.h.u1.s1.DataLength);
        if (st) { free(ra); break; }
        if ((rm.h.u2.s2.Type & 0xff) == 10)
        {
            dump_attrs("SERVER_CONN", -1, ra);
            st = pAccept(&comm, srv, 0, NULL, &pa, (void *)0x5150, &rm.h, NULL, TRUE);
            printf("SERVER accept status=%08lx\n", st);
        }
        else if ((rm.h.u2.s2.Type & 0xff) == 1)
        {
            int idx = rm.data[0];
            dump_attrs("SERVER_REQ", idx, ra);
            memset(&reply, 0, sizeof(reply));
            reply.h = rm.h;
            reply.h.u1.s1.DataLength = DATA;
            reply.h.u1.s1.TotalLength = sizeof(PORT_MSG) + DATA;
            reply.h.u2.ZeroInit = 0;
            st = pSWR(comm, reply_flags[idx], &reply.h, NULL, NULL, NULL, NULL, NULL);
            printf("SERVER reply case=%d flags=%08lx status=%08lx\n", idx, reply_flags[idx], st);
            if (idx == (int)(sizeof(cases) / sizeof(cases[0])) - 1) done = 1;
        }
        else dump_attrs("SERVER_OTHER", -1, ra);
        fflush(stdout);
        free(ra);
    }
    WaitForSingleObject(th, 10000);
    printf("SENDATTR_DONE\n");
    return 0;
}