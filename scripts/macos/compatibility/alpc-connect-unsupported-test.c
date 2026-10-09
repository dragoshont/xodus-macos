#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <string.h>

typedef LONG (WINAPI *CONNECT)(HANDLE *, OBJECT_ATTRIBUTES *, OBJECT_ATTRIBUTES *,
    void *, ULONG, PSECURITY_DESCRIPTOR, void *, SIZE_T *, void *, void *, LARGE_INTEGER *);

int main(void)
{
    WCHAR path[] = L"\\BaseNamedObjects\\CoreMessagingRegistrar";
    UNICODE_STRING name = {sizeof(path) - sizeof(WCHAR), sizeof(path), path};
    OBJECT_ATTRIBUTES connection = {0}, client = {0};
    CONNECT connect = (CONNECT)(ULONG_PTR)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtAlpcConnectPortEx");
    BYTE message[128], send[32], receive[32];
    SIZE_T bytes = sizeof(message);
    HANDLE port = (HANDLE)(ULONG_PTR)0x12345678;
    LARGE_INTEGER timeout;
    SECURITY_DESCRIPTOR security;
    unsigned i, checks = 0, failures = 0;
    LONG status;
    connection.Length = sizeof(connection);
    connection.ObjectName = &name;
    client.Length = sizeof(client);
    if (!connect || !InitializeSecurityDescriptor(&security, SECURITY_DESCRIPTOR_REVISION))
    {
        fprintf(stderr, "Unsupported-export test setup failed error=%lu\n", GetLastError());
        return 2;
    }
    timeout.QuadPart = -100000;
    memset(message, 0xcc, sizeof(message));
    memset(send, 0xcc, sizeof(send));
    memset(receive, 0xcc, sizeof(receive));
    status = connect(&port, &connection, &client, NULL, 0, &security,
                     message, &bytes, send, receive, &timeout);
    ++checks; if ((ULONG)status != 0xc0000002) ++failures;
    ++checks; if (port != (HANDLE)(ULONG_PTR)0x12345678 || bytes != sizeof(message)) ++failures;
    ++checks; if (timeout.QuadPart != -100000) ++failures;
    for (i = 0; i < sizeof(message); ++i)
    { ++checks; if (message[i] != 0xcc) ++failures; }
    for (i = 0; i < sizeof(send); ++i)
    { ++checks; if (send[i] != 0xcc || receive[i] != 0xcc) ++failures; }
    status = connect(NULL, NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL, NULL, NULL);
    ++checks; if ((ULONG)status != 0xc0000002) ++failures;
    printf("ALPC_UNSUPPORTED_BOUNDARY checks=%u failures=%u status=%08lx no_connection_claim=1\n",
           checks, failures, (unsigned long)status);
    return failures ? 1 : 0;
}
