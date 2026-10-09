#include <windows.h>
#include <rpc.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>

/* SDK V1 layouts, local names allow comparison with older MinGW headers. */
typedef struct {
    ULONG Version, Flags, ProtocolSequence;
    WCHAR *NetworkAddress, *StringEndpoint, *Reserved;
    UUID ObjectUuid;
} CREATE_TEMPLATE;
typedef struct {
    ULONG Version;
    WCHAR *ServerPrincName;
    ULONG AuthnLevel, AuthnSvc;
    SEC_WINNT_AUTH_IDENTITY_W *AuthIdentity;
    RPC_SECURITY_QOS *SecurityQos;
} CREATE_SECURITY;
typedef struct { ULONG Version, Flags, ComTimeout, CallTimeout; } CREATE_OPTIONS;
typedef RPC_STATUS (WINAPI *CREATE_FN)(CREATE_TEMPLATE *, CREATE_SECURITY *, CREATE_OPTIONS *, RPC_BINDING_HANDLE *);

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line=%d %s\n", __LINE__, #c); ++failures; } } while (0)

int main(int argc, char **argv)
{
    CREATE_FN create = (CREATE_FN)(ULONG_PTR)GetProcAddress(LoadLibraryW(L"rpcrt4.dll"), "RpcBindingCreateW");
    CREATE_TEMPLATE t = {1,0,3,NULL,L"Xodus.RpcBindingCreate.Fixture",NULL,{0}};
    CREATE_SECURITY s = {1,NULL,0,0,NULL,NULL};
    CREATE_OPTIONS o = {1,0,5,0};
    RPC_BINDING_HANDLE b = NULL, copy = NULL, classic = NULL;
    RPC_WSTR str = NULL, endpoint = NULL;
    RPC_STATUS status;
    ULONG level = 99, svc = 99, authz = 99;
    RPC_SECURITY_QOS q = {1,0,0,3};
    int compare = argc > 1 && !strcmp(argv[1], "--compare");
    CHECK(create != NULL);
    if (!create) return 2;
    for (ULONG seq = 0; seq <= 5; ++seq) {
        t.ProtocolSequence = seq; b = NULL;
        status = create(&t, NULL, NULL, &b);
        printf("NATIVE_SEQ=%lu STATUS=%lu HANDLE=%u\n", seq, status, !!b);
        CHECK(status == (seq == 3 ? 0 : 1764));
        if (b) RpcBindingFree(&b);
    }
    t.ProtocolSequence = 3;
    for (int which = 0; which < 8; ++which) {
        t.Version = which == 1 ? 2 : 1; t.Flags = which == 2 ? 2 : 0;
        t.Reserved = which == 3 ? L"reserved" : NULL;
        s.Version = which == 4 ? 2 : 1;
        o.Version = which == 5 ? 2 : 1; o.Flags = which == 6 ? 1 : 0;
        o.CallTimeout = which == 7 ? 500 : 0;
        b = NULL;
        status = create(&t, which == 4 ? &s : NULL, which >= 5 ? &o : NULL, &b);
        printf("CREATE_CASE=%d STATUS=%lu HANDLE=%u\n", which, status, !!b);
        if (b) {
            if (!which) {
                status = RpcBindingInqAuthInfoExW(b, NULL, &level, &svc, NULL, &authz, 1, &q);
                printf("DEFAULT_AUTH_STATUS=%lu LEVEL=%lu SVC=%lu QOS=%lu,%lu,%lu,%lu\n",
                    status,level,svc,q.Version,q.Capabilities,q.IdentityTracking,q.ImpersonationType);
            }
            RpcBindingFree(&b);
        }
    }
    if (compare) return failures ? 1 : 0;
    t.Version = 1; t.Flags = 0; t.Reserved = NULL;
    CHECK(create(&t, NULL, NULL, &b) == RPC_S_OK && b);
    CHECK(RpcBindingCopy(b, &copy) == RPC_S_OK && copy);
    CHECK(RpcBindingReset(copy) == RPC_S_OK);
    CHECK(RpcBindingToStringBindingW(copy, &str) == RPC_S_OK);
    CHECK(RpcStringBindingParseW(str,NULL,NULL,NULL,&endpoint,NULL) == RPC_S_OK);
    CHECK(endpoint && !wcscmp((WCHAR *)endpoint, t.StringEndpoint));
    RpcStringFreeW(&endpoint); RpcStringFreeW(&str);
    CHECK(RpcBindingFree(&copy) == RPC_S_OK && !copy);
    CHECK(RpcBindingFree(&b) == RPC_S_OK && !b);
    CHECK(RpcBindingFromStringBindingW((RPC_WSTR)L"ncalrpc:[Xodus.RpcBindingCreate.Fixture]", &classic) == RPC_S_OK);
    CHECK(RpcBindingReset(classic) == RPC_S_OK);
    CHECK(RpcBindingToStringBindingW(classic, &str) == RPC_S_OK);
    CHECK(RpcStringBindingParseW(str,NULL,NULL,NULL,&endpoint,NULL) == RPC_S_OK);
    CHECK(!endpoint || !endpoint[0]);
    RpcStringFreeW(&endpoint); RpcStringFreeW(&str); RpcBindingFree(&classic);
    t.StringEndpoint = NULL;
    CHECK(create(&t, NULL, NULL, &b) == RPC_S_OK && b);
    CHECK(RpcBindingReset(b) == RPC_S_OK);
    RpcBindingFree(&b);
    printf("RPCBINDINGCREATE_TEST_%s failures=%d\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
