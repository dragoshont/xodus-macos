/* Measures NtAlpcImpersonateClientOfPort for a pending connection request when the server passes
 * a port handle other than the connection port that received it (CoreMessaging registrar pattern).
 * Server: connection ports P and X; client A connects to P (accepted -> commA); client X connects
 * to X (accepted -> commX); client B connects to P and, while B is pending, the server impersonates
 * B's request through commA, commX, P and X. Run on native and Wine. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

#define MSGFLG_SYNC 0x20000

typedef NTSTATUS (NTAPI *fnCreatePort)(HANDLE *, OBJECT_ATTRIBUTES *, PORT_ATTR *);
typedef NTSTATUS (NTAPI *fnConnectPort)(HANDLE *, UNICODE_STRING *, OBJECT_ATTRIBUTES *, PORT_ATTR *, ULONG, PSID,
                                        PORT_MSG *, SIZE_T *, void *, void *, LARGE_INTEGER *);
typedef NTSTATUS (NTAPI *fnAccept)(HANDLE *, HANDLE, ULONG, OBJECT_ATTRIBUTES *, PORT_ATTR *, void *, PORT_MSG *,
                                   void *, BOOLEAN);
typedef NTSTATUS (NTAPI *fnSWR)(HANDLE, ULONG, PORT_MSG *, void *, PORT_MSG *, SIZE_T *, void *, LARGE_INTEGER *);
typedef NTSTATUS (NTAPI *fnImp)(HANDLE, PORT_MSG *, void *);

static fnCreatePort pCreate;
static fnConnectPort pConnect;
static fnAccept pAccept;
static fnSWR pSWR;
static fnImp pImp;

typedef struct { PORT_MSG h; unsigned char data[0x200]; } MSGBUF;
static ULONG client_flags, server_flags;
typedef struct { const WCHAR *name; const char *tag; HANDLE port; NTSTATUS st; } CLIENT;

static DWORD WINAPI client_thread(void *arg)
{
    CLIENT *c = arg;
    UNICODE_STRING us;
    PORT_ATTR pa;
    MSGBUF cm;
    SIZE_T len;
    LARGE_INTEGER to;

    RtlInitUnicodeString(&us, c->name);
    memset(&pa, 0, sizeof(pa));
    pa.SecurityQos.Length = sizeof(pa.SecurityQos);
    pa.Flags = client_flags;
    pa.SecurityQos.ImpersonationLevel = SecurityImpersonation;
    pa.MaxMessageLength = 0x1000;
    memset(&cm, 0, sizeof(cm));
    cm.h.u1.s1.TotalLength = sizeof(PORT_MSG);
    len = sizeof(cm);
    to.QuadPart = -100000000;
    c->st = pConnect(&c->port, &us, NULL, &pa, MSGFLG_SYNC, NULL, &cm.h, &len, NULL, NULL, &to);
    printf("CLIENT %s connect status=%08lx\n", c->tag, c->st);
    fflush(stdout);
    if (!c->st && c->tag[0] == 'A')
    {
        MSGBUF sm, rm;
        memset(&sm, 0, sizeof(sm));
        memset(&rm, 0, sizeof(rm));
        sm.h.u1.s1.DataLength = 16;
        sm.h.u1.s1.TotalLength = sizeof(PORT_MSG) + 16;
        len = sizeof(rm);
        c->st = pSWR(c->port, MSGFLG_SYNC, &sm.h, NULL, &rm.h, &len, NULL, &to);
        printf("CLIENT A request status=%08lx reply_type=%u\n", c->st, rm.h.u2.s2.Type & 0xff);
        fflush(stdout);
    }
    return 0;
}

static HANDLE make_port(const WCHAR *name)
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
    pa.Flags = server_flags;
    pa.MaxMessageLength = 0x1000;
    st = pCreate(&h, &oa, &pa);
    printf("SERVER create status=%08lx\n", st);
    return st ? NULL : h;
}

typedef NTSTATUS (NTAPI *fnInitAttr)(ULONG, void *, SIZE_T, SIZE_T *);
typedef void *(NTAPI *fnGetAttr)(void *, ULONG);
typedef struct { ULONG AllocatedAttributes, ValidAttributes; } MSG_ATTRS;
typedef struct { void *PortContext, *MessageContext; ULONG Sequence, MessageId, CallbackId; } CTX_ATTR;
static fnInitAttr pInit;
static fnGetAttr pGet;
static MSG_ATTRS *g_ra;

static NTSTATUS recv_conn(HANDLE port, MSGBUF *m, const char *tag)
{
    SIZE_T len = sizeof(*m), need = 0;
    LARGE_INTEGER to;
    NTSTATUS st;
    MSG_ATTRS *ra;
    CTX_ATTR *c;

    if (!g_ra)
    {
        pInit(0xe2000000, NULL, 0, &need);
        g_ra = calloc(1, need);
        pInit(0xe2000000, g_ra, need, &need);
        memset(pGet(g_ra, 0x20000000), 0x5a, sizeof(CTX_ATTR));
    }
    ra = g_ra;
    c = pGet(ra, 0x20000000);
    printf("SERVER pre-recv %s valid=%08lx ctx_port=%p\n", tag, ra->ValidAttributes, c->PortContext);
    to.QuadPart = -50000000;
    memset(m, 0, sizeof(*m));
    st = pSWR(port, 0, NULL, NULL, &m->h, &len, ra, &to);
    printf("SERVER recv %s attrs valid=%08lx ctx_port=%p ctx_msg=%p seq=%08lx msgid_eq=%d cb=%08lx\n", tag,
           ra->ValidAttributes, c->PortContext, c->MessageContext, c->Sequence, c->MessageId == m->h.MessageId,
           c->CallbackId);
    printf("SERVER recv %s status=%08lx type=%u id_nonzero=%u\n", tag, st, m->h.u2.s2.Type & 0xff, m->h.MessageId != 0);
    return st;
}

static void serve_request(HANDLE port)
{
    MSGBUF m, reply;
    NTSTATUS st;

    if (recv_conn(port, &m, "A_request")) return;
    memset(&reply, 0, sizeof(reply));
    reply.h = m.h;
    reply.h.u1.s1.DataLength = 16;
    reply.h.u1.s1.TotalLength = sizeof(PORT_MSG) + 16;
    reply.h.u2.ZeroInit = 0;
    st = pSWR(port, 0x10000, &reply.h, g_ra, NULL, NULL, NULL, NULL);
    printf("SERVER reply via connection port status=%08lx post_valid=%08lx\n", st, g_ra->ValidAttributes);
}

static void try_imp(const char *via, HANDLE port, MSGBUF *m, DWORD expect_tid)
{
    HANDLE tok = NULL;
    NTSTATUS st = pImp(port, &m->h, NULL);
    DWORD sess = 0xffffffff, ret = 0;
    TOKEN_TYPE type = 0;
    SECURITY_IMPERSONATION_LEVEL lvl = 0;
    BOOL ok;

    printf("IMPERSONATE via=%s status=%08lx", via, st);
    if (!st)
    {
        ok = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &tok);
        printf(" open_thread_token=%d", ok);
        if (!ok) printf(" err=%lu", GetLastError());
        if (ok)
        {
            GetTokenInformation(tok, TokenSessionId, &sess, sizeof(sess), &ret);
            GetTokenInformation(tok, TokenType, &type, sizeof(type), &ret);
            GetTokenInformation(tok, TokenImpersonationLevel, &lvl, sizeof(lvl), &ret);
            {
                DWORD psess = 0xfffffffe;
                ProcessIdToSessionId(GetCurrentProcessId(), &psess);
                printf(" session_matches_process=%d type=%u level=%u", sess == psess, type, lvl);
            }
            CloseHandle(tok);
        }
        RevertToSelf();
    }
    printf(" msg_tid_is_client=%d\n", (DWORD)(ULONG_PTR)m->h.ClientId.UniqueThread == expect_tid);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    WCHAR nameP[128], nameX[128];
    CLIENT a = {0}, b = {0}, x = {0};
    HANDLE srvP, srvX, commA = NULL, commB = NULL, commX = NULL, ta, tb, tx;
    PORT_ATTR pa;
    MSGBUF m;
    DWORD tida, tidb, tidx;
    NTSTATUS st;

    pCreate = (fnCreatePort)GetProcAddress(nt, "NtAlpcCreatePort");
    pConnect = (fnConnectPort)GetProcAddress(nt, "NtAlpcConnectPort");
    pAccept = (fnAccept)GetProcAddress(nt, "NtAlpcAcceptConnectPort");
    pSWR = (fnSWR)GetProcAddress(nt, "NtAlpcSendWaitReceivePort");
    pImp = (fnImp)GetProcAddress(nt, "NtAlpcImpersonateClientOfPort");
    pInit = (fnInitAttr)GetProcAddress(nt, "AlpcInitializeMessageAttribute");
    pGet = (fnGetAttr)GetProcAddress(nt, "AlpcGetMessageAttribute");
    if (!pInit || !pGet) { printf("MISSING_EXPORT\n"); return 1; }
    if (!pCreate || !pConnect || !pAccept || !pSWR || !pImp) { printf("MISSING_EXPORT\n"); return 1; }
    if (argc > 1) client_flags = strtoul(argv[1], NULL, 16);
    if (argc > 2) server_flags = strtoul(argv[2], NULL, 16);
    printf("FLAGS client=%08lx server=%08lx\n", client_flags, server_flags);

    swprintf(nameP, 128, L"\\BaseNamedObjects\\XodusImpProbeP%lu", GetCurrentProcessId());
    swprintf(nameX, 128, L"\\BaseNamedObjects\\XodusImpProbeX%lu", GetCurrentProcessId());
    if (!(srvP = make_port(nameP)) || !(srvX = make_port(nameX))) return 1;
    memset(&pa, 0, sizeof(pa));
    pa.SecurityQos.Length = sizeof(pa.SecurityQos);
    pa.SecurityQos.ImpersonationLevel = SecurityImpersonation;
    pa.MaxMessageLength = 0x1000;

    x.name = nameX; x.tag = "X";
    tx = CreateThread(NULL, 0, client_thread, &x, 0, &tidx);
    if (recv_conn(srvX, &m, "X")) return 1;
    st = pAccept(&commX, srvX, 0, NULL, &pa, (void *)0xb, &m.h, NULL, TRUE);
    printf("SERVER accept X status=%08lx\n", st);
    WaitForSingleObject(tx, 10000);

    a.name = nameP; a.tag = "A";
    ta = CreateThread(NULL, 0, client_thread, &a, 0, &tida);
    if (recv_conn(srvP, &m, "A")) return 1;
    try_imp("P_for_A", srvP, &m, tida);
    st = pAccept(&commA, srvP, 0, NULL, &pa, (void *)0xa, &m.h, NULL, TRUE);
    printf("SERVER accept A status=%08lx\n", st);
    serve_request(srvP);
    WaitForSingleObject(ta, 10000);

    b.name = nameP; b.tag = "B";
    tb = CreateThread(NULL, 0, client_thread, &b, 0, &tidb);
    if (recv_conn(srvP, &m, "B")) return 1;
    try_imp("commA_for_B", commA, &m, tidb);
    try_imp("commX_for_B", commX, &m, tidb);
    try_imp("X_for_B", srvX, &m, tidb);
    try_imp("P_for_B", srvP, &m, tidb);
    st = pAccept(&commB, srvP, 0, NULL, &pa, (void *)0xc, &m.h, NULL, TRUE);
    printf("SERVER accept B status=%08lx\n", st);
    WaitForSingleObject(tb, 10000);
    try_imp("commA_for_B_after_accept", commA, &m, tidb);
    printf("IMPPENDING_DONE\n");
    return 0;
}
