/* SPDX-License-Identifier: GPL-3.0-only */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#else
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#include "xodus_rps.h"
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RPS_MAGIC 0x58445358u
#define RPS_MAX_TIMEOUT 40000u
#define RPS_MAX_UNIX_PATH 103u

#ifdef _WIN32
typedef SOCKET rps_socket;
typedef int rps_socklen;
#define RPS_INVALID_SOCKET INVALID_SOCKET
struct rps_address
{
    ADDRESS_FAMILY sun_family;
    char sun_path[108];
};
#else
typedef int rps_socket;
typedef socklen_t rps_socklen;
#define RPS_INVALID_SOCKET (-1)
#define rps_address sockaddr_un
#endif

static void erase(void *buffer, size_t length)
{
    volatile unsigned char *bytes = buffer;
    while (length--) *bytes++ = 0;
}

static uint64_t monotonic_ms(void)
{
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value)) return UINT64_MAX;
    return (uint64_t)value.tv_sec * 1000 + (uint64_t)value.tv_nsec / 1000000;
#endif
}

static int retryable_error(void)
{
#ifdef _WIN32
    int error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEINTR;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EINTR;
#endif
}

static void close_socket(rps_socket socket_handle)
{
#ifdef _WIN32
    closesocket(socket_handle);
#else
    close(socket_handle);
#endif
}

#ifdef _WIN32
static int wine_socket_path(const char *endpoint, char *target, int capacity)
{
    typedef WCHAR * (__cdecl *dos_path_function)(const char *);
    typedef char * (__cdecl *unix_path_function)(const WCHAR *);
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    dos_path_function dos_path;
    unix_path_function unix_path;
    WCHAR *wide;
    char *roundtrip;
    BOOL substituted = FALSE;
    UINT codepage = GetACP();
    int size;

    if (!kernel) return 0;
    dos_path = (dos_path_function)GetProcAddress(kernel, "wine_get_dos_file_name");
    unix_path = (unix_path_function)GetProcAddress(kernel, "wine_get_unix_file_name");
    if (!dos_path || !unix_path || !(wide = dos_path(endpoint))) return 0;
    /* Wine Winsock decodes sockaddr paths as ACP DOS names, not raw UTF-8 Unix names. */
    size = WideCharToMultiByte(codepage, codepage == CP_UTF8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS,
        wide, -1, target, capacity, NULL, codepage == CP_UTF8 ? NULL : &substituted);
    roundtrip = unix_path(wide);
    if (!size || substituted || !roundtrip || strcmp(roundtrip, endpoint)) size = 0;
    if (roundtrip) HeapFree(GetProcessHeap(), 0, roundtrip);
    HeapFree(GetProcessHeap(), 0, wide);
    return size > 0;
}
#endif

static enum xodus_rps_result wait_socket(rps_socket socket_handle, int writing, uint64_t deadline)
{
    for (;;)
    {
        uint64_t now = monotonic_ms(), remaining;
        struct timeval timeout;
        fd_set ready;
        int result;

        if (now == UINT64_MAX) return XODUS_RPS_TRANSPORT;
        if (now >= deadline) return XODUS_RPS_TIMEOUT;
        remaining = deadline - now;
        timeout.tv_sec = (long)(remaining / 1000);
        timeout.tv_usec = (long)((remaining % 1000) * 1000);
        FD_ZERO(&ready);
        FD_SET(socket_handle, &ready);
#ifdef _WIN32
        result = select(0, writing ? NULL : &ready, writing ? &ready : NULL, NULL, &timeout);
#else
        result = select(socket_handle + 1, writing ? NULL : &ready, writing ? &ready : NULL, NULL, &timeout);
#endif
        if (result > 0) return XODUS_RPS_OK;
        if (!result) return XODUS_RPS_TIMEOUT;
        if (!retryable_error()) return XODUS_RPS_TRANSPORT;
    }
}

static enum xodus_rps_result transfer(rps_socket socket_handle, unsigned char *bytes,
    size_t length, int writing, uint64_t deadline)
{
    while (length)
    {
        enum xodus_rps_result result = wait_socket(socket_handle, writing, deadline);
        int count;
        if (result != XODUS_RPS_OK) return result;
#ifdef _WIN32
        count = writing ? send(socket_handle, (const char *)bytes, (int)length, 0)
                        : recv(socket_handle, (char *)bytes, (int)length, 0);
#else
        int flags = 0;
#ifdef MSG_NOSIGNAL
        if (writing) flags = MSG_NOSIGNAL;
#endif
        count = writing ? (int)send(socket_handle, bytes, length, flags)
                        : (int)recv(socket_handle, bytes, length, 0);
#endif
        if (count > 0)
        {
            bytes += count;
            length -= (size_t)count;
        }
        else if (!count || !retryable_error()) return XODUS_RPS_TRANSPORT;
    }
    return XODUS_RPS_OK;
}

static int valid_configuration(const char *endpoint, const char *client_id,
    int allow_ui, int full_trust, unsigned int timeout_ms)
{
    struct rps_address address;
    size_t length, i;
    const char *part;

    if (!endpoint || !client_id || endpoint[0] != '/' || !timeout_ms || timeout_ms > RPS_MAX_TIMEOUT ||
        (allow_ui != 0 && allow_ui != 1) || (full_trust != 0 && full_trust != 1)) return 0;
    length = strlen(endpoint);
    if (length < 2 || length > RPS_MAX_UNIX_PATH || length >= sizeof(address.sun_path) || endpoint[length - 1] == '/' ||
        !xmlCheckUTF8((const xmlChar *)endpoint) || strstr(endpoint, "//") ||
        !strcmp(endpoint, "/tmp/xodus.sock") || !strcmp(endpoint, "/private/tmp/xodus.sock")) return 0;
    for (i = 0; i < length; i++)
        if ((unsigned char)endpoint[i] < 32 || (unsigned char)endpoint[i] == 127 || endpoint[i] == '\\') return 0;
    for (part = endpoint + 1; *part;)
    {
        const char *end = strchr(part, '/');
        size_t size = end ? (size_t)(end - part) : strlen(part);
        if ((size == 1 && part[0] == '.') || (size == 2 && part[0] == '.' && part[1] == '.')) return 0;
        if (!end) break;
        part = end + 1;
    }
    if (strlen(client_id) != 16) return 0;
    for (i = 0; i < 16; i++)
        if (!((client_id[i] >= '0' && client_id[i] <= '9') ||
              (client_id[i] >= 'A' && client_id[i] <= 'F') ||
              (client_id[i] >= 'a' && client_id[i] <= 'f'))) return 0;
    return 1;
}

static int whitespace(const xmlChar *text)
{
    for (; *text; text++)
        if (*text != ' ' && *text != '\t' && *text != '\r' && *text != '\n') return 0;
    return 1;
}

static int future_expiry(const xmlChar *text, uint64_t now)
{
    uint64_t value = 0;
    if (!*text) return 0;
    for (; *text; text++)
    {
        unsigned int digit = (unsigned int)(*text - '0');
        if (digit > 9 || value > ((uint64_t)INT64_MAX - digit) / 10) return 0;
        value = value * 10 + digit;
    }
    return value > now;
}

static int safe_ticket(const xmlChar *text)
{
    if (!*text) return 0;
    for (; *text; text++)
        if (*text < 33 || *text > 126 || *text == '"' || *text == '\\') return 0;
    return 1;
}

static char *copy_ticket(const xmlChar *text)
{
    size_t length = strlen((const char *)text) + 1;
    char *copy = malloc(length);
    if (copy) memcpy(copy, text, length);
    return copy;
}

static enum xodus_rps_result parse_response(char *body, size_t length,
    char **user_ticket, char **device_ticket)
{
    static const char *names[] = { "Token", "Expiry", "DeviceRps", "DeviceExpiry" };
    xmlDoc *document = NULL;
    xmlNode *root, *node;
    const xmlChar *values[4] = { NULL, NULL, NULL, NULL };
    enum xodus_rps_result result = XODUS_RPS_PROTOCOL;
    time_t now = time(NULL);
    size_t i;

    if (now < 0 || !length || memchr(body, '\0', length) || strstr(body, "<!DOCTYPE") ||
        strstr(body, "<!ENTITY")) return result;
    /* Never expand a DTD or print a parser error containing credential bytes. */
    document = xmlReadMemory(body, (int)length, NULL, "UTF-8",
        XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!document || document->intSubset || document->extSubset) goto done;
    root = xmlDocGetRootElement(document);
    if (!root || root->ns || root->nsDef || root->properties ||
        xmlStrcmp(root->name, (const xmlChar *)"MSATokenResponse")) goto done;
    for (node = document->children; node; node = node->next)
        if (node != root && !(node->type == XML_TEXT_NODE && whitespace(node->content))) goto done;
    for (node = root->children; node; node = node->next)
    {
        xmlNode *text;
        if (node->type == XML_TEXT_NODE && whitespace(node->content)) continue;
        if (node->type != XML_ELEMENT_NODE || node->ns || node->nsDef || node->properties) goto done;
        for (i = 0; i < 4 && xmlStrcmp(node->name, (const xmlChar *)names[i]); i++) {}
        if (i == 4 || values[i]) goto done;
        text = node->children;
        if (!text || text->type != XML_TEXT_NODE || text->next || !text->content) goto done;
        values[i] = text->content;
    }
    for (i = 0; i < 4; i++) if (!values[i]) goto done;
    if (!safe_ticket(values[0]) || !safe_ticket(values[2]) ||
        !future_expiry(values[1], (uint64_t)now) || !future_expiry(values[3], (uint64_t)now)) goto done;
    *user_ticket = copy_ticket(values[0]);
    *device_ticket = copy_ticket(values[2]);
    if (!*user_ticket || !*device_ticket)
    {
        result = XODUS_RPS_MEMORY;
        if (*user_ticket) erase(*user_ticket, strlen(*user_ticket));
        if (*device_ticket) erase(*device_ticket, strlen(*device_ticket));
        free(*user_ticket);
        free(*device_ticket);
        *user_ticket = *device_ticket = NULL;
        goto done;
    }
    result = XODUS_RPS_OK;
done:
    if (document)
    {
        for (i = 0; i < 4; i++)
            if (values[i]) erase((void *)values[i], strlen((const char *)values[i]));
        xmlFreeDoc(document);
    }
    return result;
}

static void put_u16(unsigned char *bytes, unsigned int value)
{
    bytes[0] = (unsigned char)value;
    bytes[1] = (unsigned char)(value >> 8);
}

static void put_u32(unsigned char *bytes, uint32_t value)
{
    put_u16(bytes, value & 65535u);
    put_u16(bytes + 2, value >> 16);
}

static unsigned int get_u16(const unsigned char *bytes)
{
    return (unsigned int)bytes[0] | ((unsigned int)bytes[1] << 8);
}

enum xodus_rps_result xodus_rps_get(const char *endpoint, const char *client_id,
    int allow_ui, int full_trust, unsigned int timeout_ms,
    char **user_ticket, char **device_ticket)
{
    struct rps_address address;
    rps_socket socket_handle = RPS_INVALID_SOCKET;
    unsigned char header[8];
    char request[256], *response = NULL;
    int request_length, error = 0;
    rps_socklen error_size = sizeof(error);
    size_t response_length = 0;
    uint64_t start, deadline;
    enum xodus_rps_result result = XODUS_RPS_CONFIGURATION;
#ifdef _WIN32
    WSADATA wsa;
    unsigned long nonblocking = 1;
    int wsa_started = 0;
#endif

    if (user_ticket) *user_ticket = NULL;
    if (device_ticket) *device_ticket = NULL;
    if (!user_ticket || !device_ticket || user_ticket == device_ticket) return result;
    if (!valid_configuration(endpoint, client_id, allow_ui, full_trust, timeout_ms)) return result;
    start = monotonic_ms();
    if (start == UINT64_MAX || start > UINT64_MAX - timeout_ms) return XODUS_RPS_TRANSPORT;
    deadline = start + timeout_ms;
#ifdef _WIN32
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) return XODUS_RPS_TRANSPORT;
    wsa_started = 1;
#endif
    result = XODUS_RPS_TRANSPORT;
#ifdef _WIN32
    socket_handle = WSASocketW(AF_UNIX, SOCK_STREAM, 0, NULL, 0,
        WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
#else
    socket_handle = socket(AF_UNIX, SOCK_STREAM, 0);
#endif
    if (socket_handle == RPS_INVALID_SOCKET) goto done;
#ifdef _WIN32
    if (ioctlsocket(socket_handle, FIONBIO, &nonblocking)) goto done;
#else
    if (socket_handle >= FD_SETSIZE || fcntl(socket_handle, F_SETFD, FD_CLOEXEC) ||
        fcntl(socket_handle, F_SETFL, O_NONBLOCK)) goto done;
#ifdef SO_NOSIGPIPE
    {
        int enabled = 1;
        if (setsockopt(socket_handle, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled))) goto done;
    }
#endif
#endif
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
#ifdef _WIN32
    if (!wine_socket_path(endpoint, address.sun_path, sizeof(address.sun_path)))
    {
        result = XODUS_RPS_CONFIGURATION;
        goto done;
    }
#else
    memcpy(address.sun_path, endpoint, strlen(endpoint) + 1);
#endif
    if (connect(socket_handle, (const struct sockaddr *)&address, sizeof(address)))
    {
        if (!retryable_error()) goto done;
        result = wait_socket(socket_handle, 1, deadline);
        if (result != XODUS_RPS_OK) goto done;
        result = XODUS_RPS_TRANSPORT;
        if (getsockopt(socket_handle, SOL_SOCKET, SO_ERROR, (void *)&error, &error_size) || error) goto done;
    }
    request_length = snprintf(request, sizeof(request),
        "<MSATokenRequest><ClientId>%s</ClientId><AllowUi>%s</AllowUi>"
        "<MsaFullTrust>%s</MsaFullTrust></MSATokenRequest>",
        client_id, allow_ui ? "true" : "false", full_trust ? "true" : "false");
    if (request_length < 0 || (size_t)request_length >= sizeof(request)) goto done;
    put_u32(header, RPS_MAGIC);
    put_u16(header + 4, 3);
    put_u16(header + 6, (unsigned int)request_length);
    result = transfer(socket_handle, header, sizeof(header), 1, deadline);
    if (result != XODUS_RPS_OK) goto done;
    result = transfer(socket_handle, (unsigned char *)request, (size_t)request_length, 1, deadline);
    if (result != XODUS_RPS_OK) goto done;
    result = transfer(socket_handle, header, sizeof(header), 0, deadline);
    if (result != XODUS_RPS_OK) goto done;
    result = XODUS_RPS_PROTOCOL;
    if (get_u16(header) != (RPS_MAGIC & 65535u) || get_u16(header + 2) != (RPS_MAGIC >> 16) ||
        get_u16(header + 4) != 4 || !(response_length = get_u16(header + 6))) goto done;
    response = malloc(response_length + 1);
    if (!response)
    {
        result = XODUS_RPS_MEMORY;
        goto done;
    }
    result = transfer(socket_handle, (unsigned char *)response, response_length, 0, deadline);
    if (result != XODUS_RPS_OK) goto done;
    response[response_length] = '\0';
    result = parse_response(response, response_length, user_ticket, device_ticket);
done:
    if (response) erase(response, response_length + 1);
    free(response);
    if (socket_handle != RPS_INVALID_SOCKET) close_socket(socket_handle);
#ifdef _WIN32
    if (wsa_started) WSACleanup();
#endif
    return result;
}
