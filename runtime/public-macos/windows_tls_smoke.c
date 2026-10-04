/* SPDX-License-Identifier: GPL-3.0-only */
#include <windows.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int trust_fixture(const char *path)
{
    unsigned char certificate[16385];
    HCERTSTORE store;
    FILE *file = fopen(path, "rb");
    size_t size;
    BOOL added;

    if (!file)
    {
        fputs("Cannot open owned certificate fixture.\n", stderr);
        return 0;
    }
    size = fread(certificate, 1, sizeof(certificate), file);
    if (ferror(file) || size == 0 || size == sizeof(certificate))
    {
        fclose(file);
        fputs("Certificate fixture is empty, unreadable or over the 16 KiB bound.\n", stderr);
        return 0;
    }
    if (fclose(file))
    {
        fputs("Cannot close certificate fixture.\n", stderr);
        return 0;
    }
    store = CertOpenStore(CERT_STORE_PROV_SYSTEM_REGISTRY_A, 0, 0,
                          CERT_SYSTEM_STORE_CURRENT_USER, "ROOT");
    if (!store)
    {
        fprintf(stderr, "Cannot open private-prefix certificate registry: %lu.\n",
                (unsigned long)GetLastError());
        return 0;
    }
    added = CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING, certificate,
                                             (DWORD)size, CERT_STORE_ADD_REPLACE_EXISTING, NULL);
    if (!added)
        fprintf(stderr, "Cannot add owned certificate fixture: %lu.\n",
                (unsigned long)GetLastError());
    if (!CertCloseStore(store, 0))
    {
        fputs("Cannot close private-prefix certificate registry.\n", stderr);
        return 0;
    }
    return added != FALSE;
}

int main(int argc, char **argv)
{
    const char *expected_body = "xodus-tls-ok";
    const WCHAR *host = L"localhost";
    HINTERNET session = NULL, connection = NULL, request = NULL;
    DWORD expected_error = 0, error = 0, status, size, read, total = 0;
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES |
                     WINHTTP_DISABLE_AUTHENTICATION;
    DWORD security_flags = 0;
    char body[128], *end;
    long port;
    int result = 1;
    BOOL received;

    if (argc == 2 && !strcmp(argv[1], "--bootstrap"))
    {
        puts("Isolated Windows check started; no credentials requested.");
        return 0;
    }
    if (argc != 4) return 2;
    errno = 0;
    port = strtol(argv[1], &end, 10);
    if (errno || end == argv[1] || *end || port < 1 || port > 65535) return 2;
    if (!strcmp(argv[3], "untrusted"))
        expected_error = ERROR_WINHTTP_SECURE_INVALID_CA;
    else if (!strcmp(argv[3], "hostname"))
    {
        host = L"127.0.0.1";
        expected_error = ERROR_WINHTTP_SECURE_CERT_CN_INVALID;
    }
    else if (strcmp(argv[3], "trusted")) return 2;
    if (strcmp(argv[3], "untrusted") && !trust_fixture(argv[2])) return 1;

    session = WinHttpOpen(L"Xodus owned TLS component check", WINHTTP_ACCESS_TYPE_NO_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session || !WinHttpSetTimeouts(session, 2000, 2000, 2000, 2000)) goto failed;
    connection = WinHttpConnect(session, host, (INTERNET_PORT)port, 0);
    if (!connection) goto failed;
    request = WinHttpOpenRequest(connection, L"GET", L"/component-check", NULL,
                                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                WINHTTP_FLAG_SECURE);
    if (!request || !WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE,
                                      &disabled, sizeof(disabled))) goto failed;
    size = sizeof(security_flags);
    if (!WinHttpQueryOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &security_flags, &size))
        goto failed;
    if (security_flags & (SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                          SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE))
    {
        fputs("Certificate validation was disabled unexpectedly.\n", stderr);
        goto done;
    }
    received = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (received) received = WinHttpReceiveResponse(request, NULL);
    if (!received)
    {
        error = GetLastError();
        if (expected_error && error == expected_error)
        {
            result = 0;
            goto done;
        }
        fprintf(stderr, "TLS result %lu did not match expected %lu.\n",
                (unsigned long)error, (unsigned long)expected_error);
        goto done;
    }
    if (expected_error)
    {
        fputs("An invalid certificate unexpectedly succeeded.\n", stderr);
        goto done;
    }
    size = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                             WINHTTP_NO_HEADER_INDEX)) goto failed;
    if (status != 200)
    {
        fprintf(stderr, "Unexpected owned-peer HTTP status %lu.\n", (unsigned long)status);
        goto done;
    }
    for (;;)
    {
        if (total == sizeof(body))
        {
            fputs("Owned-peer response exceeded its bound.\n", stderr);
            goto done;
        }
        if (!WinHttpReadData(request, body + total, sizeof(body) - total, &read)) goto failed;
        if (!read) break;
        total += read;
    }
    if (total != strlen(expected_body) || memcmp(body, expected_body, total))
    {
        fputs("Owned-peer response did not match its exact synthetic marker.\n", stderr);
        goto done;
    }
    result = 0;
    goto done;

failed:
    fprintf(stderr, "Windows TLS operation failed: %lu.\n", (unsigned long)GetLastError());
done:
    if (request && !WinHttpCloseHandle(request))
    {
        fputs("Cannot close owned HTTP request.\n", stderr);
        result = 1;
    }
    if (connection && !WinHttpCloseHandle(connection))
    {
        fputs("Cannot close owned HTTP connection.\n", stderr);
        result = 1;
    }
    if (session && !WinHttpCloseHandle(session))
    {
        fputs("Cannot close owned HTTP session.\n", stderr);
        result = 1;
    }
    if (!result) puts("Isolated Windows TLS outcome passed.");
    return result;
}
