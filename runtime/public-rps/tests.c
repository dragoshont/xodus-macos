/* SPDX-License-Identifier: GPL-3.0-only */
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "xodus_rps.h"
#include <libxml/parser.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CLIENT_ID "0000000048176A1E"
#define GOOD_FIELDS "<Token>user-test-ticket</Token><Expiry>4102444800</Expiry>" \
    "<DeviceRps>device-test-ticket</DeviceRps><DeviceExpiry>4102444800</DeviceExpiry>"
#define GOOD_BODY "<MSATokenResponse>" GOOD_FIELDS "</MSATokenResponse>"

enum response_mode { NORMAL, FRAGMENTED, WRONG_MAGIC, WRONG_TYPE, EMPTY, TRUNCATED, CLOSED, STALLED, TRICKLED };
static unsigned int checks, external_loads;
static char endpoint[104];

static void require(int condition, const char *name)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }
}

static xmlParserInputPtr refuse_external(const char *url, const char *id, xmlParserCtxtPtr context)
{
    (void)url;
    (void)id;
    (void)context;
    external_loads++;
    return NULL;
}

static void sleep_ms(long milliseconds)
{
    struct timespec remaining = { milliseconds / 1000, (milliseconds % 1000) * 1000000 };
    while (nanosleep(&remaining, &remaining) && errno == EINTR) {}
}

static int exact_read(int fd, unsigned char *bytes, size_t length)
{
    while (length)
    {
        ssize_t count = read(fd, bytes, length);
        if (count <= 0) return 0;
        bytes += count;
        length -= (size_t)count;
    }
    return 1;
}

static int peer_write(int fd, const unsigned char *bytes, size_t length, int fragments, int trickle)
{
    while (length)
    {
        size_t part = fragments ? 1 : length;
        ssize_t count = send(fd, bytes, part, 0);
        if (count <= 0) return 0;
        bytes += count;
        length -= (size_t)count;
        if (trickle) sleep_ms(30);
    }
    return 1;
}

static void peer(int listener, const char *body, size_t length, enum response_mode mode,
    int allow_ui, int full_trust)
{
    unsigned char header[8] = { 0x58, 0x53, 0x44, 0x58, 4, 0, 0, 0 };
    char request[256], expected[256];
    unsigned int request_length;
    int connection;

    signal(SIGPIPE, SIG_IGN);
    alarm(4);
    connection = accept(listener, NULL, NULL);
    if (connection < 0 || !exact_read(connection, header, sizeof(header))) _exit(2);
    if (memcmp(header, "\x58\x53\x44\x58\x03\x00", 6)) _exit(3);
    request_length = (unsigned int)header[6] | ((unsigned int)header[7] << 8);
    if (!request_length || request_length >= sizeof(request) ||
        !exact_read(connection, (unsigned char *)request, request_length)) _exit(4);
    request[request_length] = '\0';
    snprintf(expected, sizeof(expected),
        "<MSATokenRequest><ClientId>" CLIENT_ID "</ClientId><AllowUi>%s</AllowUi>"
        "<MsaFullTrust>%s</MsaFullTrust></MSATokenRequest>",
        allow_ui ? "true" : "false", full_trust ? "true" : "false");
    if (strcmp(request, expected)) _exit(5);
    if (mode == CLOSED) goto done;
    if (mode == STALLED)
    {
        sleep_ms(250);
        goto done;
    }
    memcpy(header, "\x58\x53\x44\x58\x04\x00", 6);
    if (mode == WRONG_MAGIC) header[0] ^= 1;
    if (mode == WRONG_TYPE) header[4] = 2;
    if (mode == EMPTY) length = 0;
    header[6] = (unsigned char)length;
    header[7] = (unsigned char)(length >> 8);
    if (!peer_write(connection, header, sizeof(header), mode == FRAGMENTED || mode == TRICKLED, mode == TRICKLED))
        goto done;
    if (mode == TRUNCATED && length) length--;
    (void)peer_write(connection, (const unsigned char *)body, length, mode == FRAGMENTED, 0);
done:
    close(connection);
    close(listener);
    _exit(0);
}

static uint64_t clock_ms(void)
{
    struct timespec value;
    require(!clock_gettime(CLOCK_MONOTONIC, &value), "test clock");
    return (uint64_t)value.tv_sec * 1000 + (uint64_t)value.tv_nsec / 1000000;
}

static void run_case(const char *name, const char *body, size_t length, enum response_mode mode,
    enum xodus_rps_result expected, int allow_ui, int full_trust)
{
    struct sockaddr_un address;
    int listener = socket(AF_UNIX, SOCK_STREAM, 0), status;
    char *user = (char *)(uintptr_t)1, *device = (char *)(uintptr_t)1;
    uint64_t started, elapsed;
    enum xodus_rps_result result;
    pid_t child;

    require(listener >= 0, "test listener");
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, endpoint, strlen(endpoint) + 1);
    require(!bind(listener, (struct sockaddr *)&address, sizeof(address)), "owned test endpoint");
    require(!listen(listener, 1), "test listen");
    child = fork();
    require(child >= 0, "test fork");
    if (!child) peer(listener, body, length, mode, allow_ui, full_trust);
    close(listener);
    started = clock_ms();
    result = xodus_rps_get(endpoint, CLIENT_ID, allow_ui, full_trust,
        mode == STALLED || mode == TRICKLED ? 100 : 1500, &user, &device);
    elapsed = clock_ms() - started;
    require(result == expected, name);
    if (result == XODUS_RPS_OK)
    {
        require(user && *user && device && !strcmp(device, "device-test-ticket"), name);
        if (length == strlen(GOOD_BODY)) require(!strcmp(user, "user-test-ticket"), name);
        free(user);
        free(device);
    }
    else require(!user && !device, "failure output ownership");
    if (mode == STALLED || mode == TRICKLED)
        require(elapsed >= 80 && elapsed < 500, "single overall IO deadline");
    require(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status), "fake peer outcome");
    require(!unlink(endpoint), "owned socket cleanup");
    require(!external_loads, "no external XML resources");
    checks++;
    printf("PASS: %s\n", name);
}

static void rejected_config(const char *name, const char *path, const char *client_id,
    int allow_ui, int full_trust, unsigned int timeout)
{
    char *user = (char *)(uintptr_t)1, *device = (char *)(uintptr_t)1;
    require(xodus_rps_get(path, client_id, allow_ui, full_trust, timeout, &user, &device) ==
        XODUS_RPS_CONFIGURATION && !user && !device, name);
    checks++;
    printf("PASS: %s\n", name);
}

int main(void)
{
    static const struct { const char *name; const char *body; } malformed[] = {
        { "incomplete user only", "<MSATokenResponse><Token>user</Token><Expiry>4102444800</Expiry></MSATokenResponse>" },
        { "empty device ticket", "<MSATokenResponse><Token>user</Token><Expiry>4102444800</Expiry><DeviceRps/><DeviceExpiry>4102444800</DeviceExpiry></MSATokenResponse>" },
        { "duplicate token", "<MSATokenResponse><Token>user</Token><Token>other</Token><Expiry>4102444800</Expiry><DeviceRps>device</DeviceRps><DeviceExpiry>4102444800</DeviceExpiry></MSATokenResponse>" },
        { "expired user", "<MSATokenResponse><Token>user</Token><Expiry>1</Expiry><DeviceRps>device</DeviceRps><DeviceExpiry>4102444800</DeviceExpiry></MSATokenResponse>" },
        { "expired device", "<MSATokenResponse><Token>user</Token><Expiry>4102444800</Expiry><DeviceRps>device</DeviceRps><DeviceExpiry>1</DeviceExpiry></MSATokenResponse>" },
        { "overflow expiry", "<MSATokenResponse><Token>user</Token><Expiry>9223372036854775808</Expiry><DeviceRps>device</DeviceRps><DeviceExpiry>4102444800</DeviceExpiry></MSATokenResponse>" },
        { "negative expiry", "<MSATokenResponse><Token>user</Token><Expiry>-1</Expiry><DeviceRps>device</DeviceRps><DeviceExpiry>4102444800</DeviceExpiry></MSATokenResponse>" },
        { "nondecimal expiry", "<MSATokenResponse><Token>user</Token><Expiry>4102444800x</Expiry><DeviceRps>device</DeviceRps><DeviceExpiry>4102444800</DeviceExpiry></MSATokenResponse>" },
        { "unknown root", "<Other>" GOOD_FIELDS "</Other>" },
        { "unknown response field", "<MSATokenResponse>" GOOD_FIELDS "<Other/></MSATokenResponse>" },
        { "root namespace", "<MSATokenResponse xmlns='example'>" GOOD_FIELDS "</MSATokenResponse>" },
        { "root attributes", "<MSATokenResponse changed='true'>" GOOD_FIELDS "</MSATokenResponse>" },
        { "nested token", "<MSATokenResponse><Token><Nested>user</Nested></Token><Expiry>4102444800</Expiry><DeviceRps>device</DeviceRps><DeviceExpiry>4102444800</DeviceExpiry></MSATokenResponse>" },
        { "malformed XML", "<MSATokenResponse>" },
        { "trailing XML", GOOD_BODY "<Other/>" },
        { "external DTD", "<!DOCTYPE MSATokenResponse SYSTEM 'file:///not-a-real-credential'>" GOOD_BODY },
        { "external entity", "<!DOCTYPE MSATokenResponse [<!ENTITY ticket SYSTEM 'https://127.0.0.1/never'>]><MSATokenResponse><Token>&ticket;</Token></MSATokenResponse>" },
        { "entity expansion", "<!DOCTYPE MSATokenResponse [<!ENTITY a 'aaaa'><!ENTITY b '&a;&a;&a;'>]><MSATokenResponse><Token>&b;</Token></MSATokenResponse>" },
        { "JSON unsafe ticket", "<MSATokenResponse><Token>user&quot;injection</Token><Expiry>4102444800</Expiry><DeviceRps>device</DeviceRps><DeviceExpiry>4102444800</DeviceExpiry></MSATokenResponse>" },
        { "JSON unsafe device", "<MSATokenResponse><Token>user</Token><Expiry>4102444800</Expiry><DeviceRps>device\\injection</DeviceRps><DeviceExpiry>4102444800</DeviceExpiry></MSATokenResponse>" },
        { "UTF8 unsafe JSON ticket", "<MSATokenResponse><Token>user\xc3\xa9</Token><Expiry>4102444800</Expiry><DeviceRps>device</DeviceRps><DeviceExpiry>4102444800</DeviceExpiry></MSATokenResponse>" },
        { "whitespace ticket", "<MSATokenResponse><Token> </Token><Expiry>4102444800</Expiry><DeviceRps>device</DeviceRps><DeviceExpiry>4102444800</DeviceExpiry></MSATokenResponse>" }
    };
    char directory[] = "/tmp/xodus-rps-check-XXXXXX", oversized_path[200], path_limit[105], original_endpoint[104];
    char *large, *escaped;
    char *output = NULL;
    size_t i, base_length = strlen(GOOD_BODY), prefix_length = strlen("<MSATokenResponse><Token>");

    require(mkdtemp(directory) != NULL, "owned test directory");
    require(!chmod(directory, 0700), "private test directory");
    require(snprintf(endpoint, sizeof(endpoint), "%s/r.sock", directory) < (int)sizeof(endpoint), "test path bound");
    xmlSetExternalEntityLoader(refuse_external);
    for (i = 0; i < 4; i++)
        run_case("exact request booleans and complete response", GOOD_BODY, base_length, NORMAL, XODUS_RPS_OK,
            (int)(i & 1), (int)(i >> 1));
    run_case("fragmented header and payload", GOOD_BODY, base_length, FRAGMENTED, XODUS_RPS_OK, 0, 0);
    run_case("wrong magic", GOOD_BODY, base_length, WRONG_MAGIC, XODUS_RPS_PROTOCOL, 0, 0);
    run_case("wrong response type", GOOD_BODY, base_length, WRONG_TYPE, XODUS_RPS_PROTOCOL, 0, 0);
    run_case("zero payload", GOOD_BODY, base_length, EMPTY, XODUS_RPS_PROTOCOL, 0, 0);
    run_case("truncated body", GOOD_BODY, base_length, TRUNCATED, XODUS_RPS_TRANSPORT, 0, 0);
    run_case("backend closes on checked failure", GOOD_BODY, base_length, CLOSED, XODUS_RPS_TRANSPORT, 0, 0);
    run_case("stalled response deadline", GOOD_BODY, base_length, STALLED, XODUS_RPS_TIMEOUT, 0, 0);
    run_case("trickle cannot reset deadline", GOOD_BODY, base_length, TRICKLED, XODUS_RPS_TIMEOUT, 0, 0);
    for (i = 0; i < sizeof(malformed) / sizeof(malformed[0]); i++)
        run_case(malformed[i].name, malformed[i].body, strlen(malformed[i].body), NORMAL, XODUS_RPS_PROTOCOL, 0, 0);
    {
        static const char embedded_nul[] = GOOD_BODY "\0<Other/>";
        run_case("embedded NUL", embedded_nul, sizeof(embedded_nul) - 1, NORMAL, XODUS_RPS_PROTOCOL, 0, 0);
    }
    large = malloc(65536);
    require(large != NULL, "test allocation");
    memcpy(large, GOOD_BODY, prefix_length);
    memset(large + prefix_length, 'u', 65535 - base_length + strlen("user-test-ticket"));
    strcpy(large + prefix_length + 65535 - base_length + strlen("user-test-ticket"),
        &GOOD_BODY[prefix_length + strlen("user-test-ticket")]);
    require(strlen(large) == 65535, "exact inclusive u16 boundary");
    run_case("65535 byte valid frame", large, 65535, NORMAL, XODUS_RPS_OK, 0, 0);
    free(large);
    escaped = malloc(base_length + 10);
    require(escaped != NULL, "test allocation");
    snprintf(escaped, base_length + 10,
        "<MSATokenResponse><Token>t=user&amp;p=proof</Token><Expiry>4102444800</Expiry>"
        "<DeviceRps>device-test-ticket</DeviceRps><DeviceExpiry>4102444800</DeviceExpiry></MSATokenResponse>");
    run_case("predefined entity decoding", escaped, strlen(escaped), NORMAL, XODUS_RPS_OK, 0, 0);
    free(escaped);
    strcpy(original_endpoint, endpoint);
    strcpy(endpoint, directory);
    strcat(endpoint, "/");
    i = strlen(endpoint);
    memset(endpoint + i, 'p', 103 - i);
    endpoint[103] = '\0';
    run_case("103 byte producer path boundary", GOOD_BODY, base_length, NORMAL, XODUS_RPS_OK, 0, 0);
    memcpy(path_limit, endpoint, 103);
    path_limit[103] = 'p';
    path_limit[104] = '\0';
    rejected_config("104 byte producer path rejected", path_limit, CLIENT_ID, 0, 0, 100);
    strcpy(endpoint, original_endpoint);
    memset(oversized_path, 'a', sizeof(oversized_path));
    oversized_path[0] = '/';
    oversized_path[sizeof(oversized_path) - 1] = '\0';
    rejected_config("missing endpoint", NULL, CLIENT_ID, 0, 0, 100);
    rejected_config("relative endpoint", "relative.sock", CLIENT_ID, 0, 0, 100);
    rejected_config("legacy default endpoint", "/tmp/xodus.sock", CLIENT_ID, 0, 0, 100);
    rejected_config("legacy Mac alias", "/private/tmp/xodus.sock", CLIENT_ID, 0, 0, 100);
    rejected_config("dot path", "/tmp/./r.sock", CLIENT_ID, 0, 0, 100);
    rejected_config("parent path", "/tmp/other/../r.sock", CLIENT_ID, 0, 0, 100);
    rejected_config("duplicate separator", "/tmp//r.sock", CLIENT_ID, 0, 0, 100);
    rejected_config("oversized endpoint", oversized_path, CLIENT_ID, 0, 0, 100);
    rejected_config("control path", "/tmp/\nr.sock", CLIENT_ID, 0, 0, 100);
    rejected_config("DEL path", "/tmp/\x7f.sock", CLIENT_ID, 0, 0, 100);
    rejected_config("invalid UTF8 path", "/tmp/\xff.sock", CLIENT_ID, 0, 0, 100);
    rejected_config("missing client ID", endpoint, NULL, 0, 0, 100);
    rejected_config("short client ID", endpoint, "0123456789abcde", 0, 0, 100);
    rejected_config("long client ID", endpoint, "0123456789abcdef0", 0, 0, 100);
    rejected_config("nonhex client ID", endpoint, "0123456789abcdeg", 0, 0, 100);
    rejected_config("invalid UI flag", endpoint, CLIENT_ID, 2, 0, 100);
    rejected_config("invalid trust flag", endpoint, CLIENT_ID, 0, -1, 100);
    rejected_config("zero deadline", endpoint, CLIENT_ID, 0, 0, 0);
    rejected_config("excessive deadline", endpoint, CLIENT_ID, 0, 0, 40001);
    output = (char *)(uintptr_t)1;
    require(xodus_rps_get(endpoint, CLIENT_ID, 0, 0, 100, &output, &output) == XODUS_RPS_CONFIGURATION && !output,
        "aliased output pointers");
    output = (char *)(uintptr_t)1;
    require(xodus_rps_get(endpoint, CLIENT_ID, 0, 0, 100, &output, NULL) == XODUS_RPS_CONFIGURATION && !output,
        "missing device output clears user");
    output = (char *)(uintptr_t)1;
    require(xodus_rps_get(endpoint, CLIENT_ID, 0, 0, 100, NULL, &output) == XODUS_RPS_CONFIGURATION && !output,
        "missing user output clears device");
    require(!rmdir(directory), "owned directory cleanup");
    printf("%u isolated RPS client checks passed; no accounts or service started.\n", checks + 3);
    return 0;
}
