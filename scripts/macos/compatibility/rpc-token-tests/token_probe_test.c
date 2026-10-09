#include <windows.h>
#include <rpc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "token_probe.h"

void *__RPC_USER MIDL_user_allocate(size_t size) { return malloc(size); }
void __RPC_USER MIDL_user_free(void *memory) { free(memory); }

#ifdef TOKEN_PROBE_SERVER
static HANDLE last_token;
static unsigned calls;

LONG __cdecl CheckToken(handle_t binding, hyper value)
{
    HANDLE token = (HANDLE)(ULONG_PTR)value;
    TOKEN_TYPE type;
    TOKEN_STATISTICS stats;
    DWORD required;
    HANDLE forbidden = NULL;
    if (!token)
    {
        puts("GENUINE_NDR_NULL_TOKEN_RECEIVED"); fflush(stdout);
        return (LONG)GetCurrentProcessId();
    }
    if (!GetTokenInformation(token, TokenStatistics, &stats, sizeof(stats), &required))
        return -2;
    if (DuplicateToken(token, SecurityImpersonation, &forbidden))
    {
        CloseHandle(forbidden);
        return -3;
    }
    last_token = token;
    ++calls;
    printf("REAL_RECEIVER_TOKEN call=%u pid=%lu tokenid=%08lx:%08lx query_only=1\n",
           calls, GetCurrentProcessId(), stats.TokenId.HighPart, stats.TokenId.LowPart);
    fflush(stdout);
    return (LONG)GetCurrentProcessId();
}

LONG __cdecl StopServer(handle_t binding)
{
    TOKEN_TYPE type;
    DWORD required;
    int closed = !GetTokenInformation(last_token, TokenType, &type, sizeof(type), &required);
    printf("ACTUAL_RECEIVER_CLEANUP calls=%u last_duplicate_closed=%d\n", calls, closed);
    fflush(stdout);
    RpcMgmtStopServerListening(NULL);
    return closed ? 0 : -4;
}

int main(int argc, char **argv)
{
    RPC_STATUS status;
    status = RpcServerUseProtseqEpA((RPC_CSTR)"ncalrpc", 4,
                                   (RPC_CSTR)"xodus-token-handle-probe-20261006", NULL);
    if (status) return status;
    status = RpcServerRegisterIf(TokenProbe_v1_0_s_ifspec, NULL, NULL);
    if (status) return status;
    puts("REAL_TOKEN_RPC_SERVER_READY"); fflush(stdout);
    return RpcServerListen(1, 4, FALSE);
}
#else
int main(int argc, char **argv)
{
    RPC_CSTR text = NULL;
    RPC_BINDING_HANDLE binding = NULL;
    HANDLE token, event;
    TOKEN_STATISTICS stats;
    TOKEN_TYPE type;
    DWORD required;
    LONG first, second, stopped;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &token)) return 1;
    if (!GetTokenInformation(token, TokenStatistics, &stats, sizeof(stats), &required)) return 2;
    printf("ACTUAL_SENDER_TOKEN pid=%lu tokenid=%08lx:%08lx\n",
           GetCurrentProcessId(), stats.TokenId.HighPart, stats.TokenId.LowPart);
    if (RpcStringBindingComposeA(NULL, (RPC_CSTR)"ncalrpc", NULL,
            (RPC_CSTR)"xodus-token-handle-probe-20261006", NULL, &text)) return 3;
    if (RpcBindingFromStringBindingA(text, &binding)) return 4;
    RpcStringFreeA(&text);
    if (argc > 1)
    {
        if (!strcmp(argv[1], "--null"))
        {
            first = CheckToken(binding, 0);
            printf("NATIVE_NULL_TOKEN_CALL_RESULT=%ld\n", first);
            StopServer(binding);
            return first > 0 ? 0 : 7;
        }
        event = CreateEventW(NULL, FALSE, FALSE, NULL);
        CheckToken(binding, (hyper)(ULONG_PTR)event);
        return 6;
    }
    first = CheckToken(binding, (hyper)(ULONG_PTR)token);
    second = CheckToken(binding, (hyper)(ULONG_PTR)token);
    stopped = StopServer(binding);
    printf("TOKEN_RPC_RESULT first=%ld second=%ld distinct_receiver=%d original_token_alive=%d cleanup_status=%ld\n",
           first, second, first > 0 && first != (LONG)GetCurrentProcessId(),
           GetTokenInformation(token, TokenType, &type, sizeof(type), &required), stopped);
    CloseHandle(token);
    RpcBindingFree(&binding);
    return first > 0 && first == second && first != (LONG)GetCurrentProcessId() &&
           stopped == 0 ? 0 : 5;
}
#endif
