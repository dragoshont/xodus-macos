/*
 * Runs the shared production storage/marshaller and the EXACT patched Wine
 * server handler, internal setter, and NtQuerySecurityAttributesToken function.
 * token-security-wine-under-test.h is extracted mechanically from the pinned
 * patched source; only the handle/object/RPC transport is substituted below.
 * This is not a claim that a complete Wine runtime was installed or tested.
 *
 * clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined
 *   token-security-attributes-test.c -o token-security-attributes-test
 * The same test is cross-built/run on Windows x64/x86 to verify native layouts.
 */
#include "token-security-attributes.h"
#include <stdio.h>

#define STATUS_SUCCESS TSA_SUCCESS
#define STATUS_BUFFER_TOO_SMALL TSA_TOO_SMALL
#define STATUS_INVALID_PARAMETER TSA_INVALID
#define STATUS_NO_MEMORY TSA_NO_MEMORY
#define TOKEN_QUERY 8
#define WINAPI
typedef uint32_t NTSTATUS;
typedef uint32_t ULONG;
typedef uintptr_t HANDLE;
typedef struct {
    uint16_t Length, MaximumLength;
    uint16_t *Buffer;
} UNICODE_STRING;

struct token {
    struct tsa_store security_attributes;
    unsigned int modified_id;
};
struct process { int unused; };
struct thread { struct process *process; };
static struct process process;
static struct thread thread = {&process}, *current = &thread;
static struct token original, duplicate;
struct test_request { uintptr_t handle; void *buffer; unsigned int capacity; };
struct test_reply { unsigned int attributes_len; };
static struct test_request *req;
static struct test_reply *reply;
static uint32_t server_error;
static unsigned assertions, failures, releases, modifications, handle_checks;

static void set_error(unsigned int error) { server_error = error; }
static void allocate_luid(unsigned int *id) { *id = ++modifications; }
static struct token *get_token_obj(struct process *p, uintptr_t handle, unsigned int access)
{
    ++handle_checks;
    if (p != &process || access != TOKEN_QUERY) {
        puts("FAIL server bypassed process/TOKEN_QUERY"); ++failures;
        set_error(TSA_INVALID); return NULL;
    }
    if (handle == 3) { set_error(UINT32_C(0xc0000022)); return NULL; }
    if (handle == 1) return &original;
    if (handle == 2) return &duplicate;
    set_error(UINT32_C(0xc0000008)); return NULL;
}
static unsigned int get_reply_max_size(void) { return req->capacity; }
static void set_reply_data(const void *data, unsigned int bytes)
{
    if (!req->buffer || bytes > req->capacity) {
        puts("FAIL server reply overflow"); ++failures; return;
    }
    memcpy(req->buffer, data, bytes);
}
static void release_object(struct token *token) { (void)token; ++releases; }
static uintptr_t wine_server_obj_handle(HANDLE handle) { return handle; }
static void wine_server_set_reply(struct test_request *r, void *buffer, unsigned int bytes)
{
    r->buffer = buffer; r->capacity = bytes;
}
static void server_get_token_security_attributes(void);
static NTSTATUS wine_server_call(struct test_request *request)
{
    req = request;
    server_error = 0;
    server_get_token_security_attributes();
    return server_error;
}
#define DECL_HANDLER(name) static void server_##name(void)
#define SERVER_START_REQ(name) do { \
    struct test_request request = {0}; \
    struct test_reply response = {0}; \
    struct test_request *req = &request; \
    struct test_reply *reply = &response; \
    test_set_reply(reply);
#define SERVER_END_REQ } while (0)
static void test_set_reply(struct test_reply *r) { reply = r; }

#include "token-security-wine-under-test.h"

static void check(int condition, const char *name)
{
    ++assertions;
    if (!condition) { ++failures; printf("FAIL %s\n", name); }
}

static uint32_t text(uint16_t *out, const char *in)
{
    uint32_t n = 0;
    while (*in) out[n++] = (unsigned char)*in++;
    out[n] = 0;
    return n * 2;
}

static UNICODE_STRING name(uint16_t *buffer, const char *ascii)
{
    UNICODE_STRING s;
    s.Length = (uint16_t)text(buffer, ascii);
    s.MaximumLength = s.Length + 2; s.Buffer = buffer;
    return s;
}

static uint32_t fixture(unsigned char *data)
{
    struct tsa_header *h = (struct tsa_header *)data;
    struct tsa_attribute *a = (struct tsa_attribute *)(h + 1);
    struct tsa_string *s;
    uint32_t at = sizeof(*h) + 2 * sizeof(*a);
    uint64_t flags = UINT64_C(0x1122334455667788);
    memset(data, 0, 1024);
    h->version = 1; h->count = 2;
    a[0].name = at;
    a[0].name_bytes = text((uint16_t *)(data + at), "Fixture.Identifier");
    at += a[0].name_bytes + 2;
    a[1].name = at;
    a[1].name_bytes = text((uint16_t *)(data + at), "Fixture.Flags");
    at = tsa_align(at + a[1].name_bytes + 2, 8);
    a[0].type = 3; a[0].count = 1; a[0].values = at;
    s = (struct tsa_string *)(data + at); at += sizeof(*s);
    s->offset = at;
    s->bytes = text((uint16_t *)(data + at), "Fixture.Value");
    at = tsa_align(at + s->bytes + 2, 8);
    a[1].type = 2; a[1].count = 1; a[1].values = at;
    memcpy(data + at, &flags, 8);
    return at + 8;
}

static int sentinel(const unsigned char *out, size_t bytes)
{
    size_t i;
    for (i = 0; i < bytes; ++i) if (out[i] != 0xcc) return 0;
    return 1;
}

static void query_tests(void)
{
    union { uint64_t alignment; unsigned char data[4096]; } out, provision;
    uint16_t strings[2][64];
    UNICODE_STRING filter[2];
    uint32_t required, status, bytes, i, len = sizeof(void *) == 8 ? 16 : 12;
    const void *raw;
    memset(out.data, 0xcc, sizeof(out.data)); required = UINT32_C(0xcccccccc);
    status = NtQuerySecurityAttributesToken(1, NULL, 0, NULL, 0, &required);
    check(status == TSA_TOO_SMALL && required == len, "honest empty token required native header");
    status = NtQuerySecurityAttributesToken(1, NULL, 0, out.data, len - 1, &required);
    check(status == TSA_TOO_SMALL && sentinel(out.data, sizeof(out.data)), "empty short buffer untouched");
    status = NtQuerySecurityAttributesToken(1, NULL, 0, out.data, len, &required);
    check(!status && out.data[0] == 1 && !tsa_u16(out.data + 2) &&
          !memcmp(out.data + 4, "\0\0\0\0", 4) &&
          sentinel(out.data + len, sizeof(out.data) - len), "honest empty header only");
    filter[0] = name(strings[0], "WIN://SYSAPPID");
    memset(out.data, 0xcc, sizeof(out.data));
    status = NtQuerySecurityAttributesToken(1, filter, 1, out.data, sizeof(out.data), &required);
    check(status == TSA_NOT_FOUND && !required && sentinel(out.data, sizeof(out.data)),
          "legacy package filter NOT_FOUND, not packaged success");
    for (i = 0; i < 2; ++i) {
        required = UINT32_C(0xcccccccc);
        status = NtQuerySecurityAttributesToken(i ? 3 : 0x1234, NULL, 0,
                                                out.data, sizeof(out.data), &required);
        check(status == (i ? UINT32_C(0xc0000022) : UINT32_C(0xc0000008)) &&
              required == UINT32_C(0xcccccccc) && sentinel(out.data, sizeof(out.data)),
              "actual handler enforces valid handle and TOKEN_QUERY");
    }
    required = UINT32_C(0xcccccccc);
    status = NtQuerySecurityAttributesToken(1, filter, 0, out.data, sizeof(out.data), &required);
    check(sizeof(void *) == 8 ?
          status == TSA_INVALID && required == UINT32_C(0xcccccccc) :
          status == TSA_SUCCESS && required == 12,
          "native zero-count/non-NULL-name architecture behavior");
    memset(out.data, 0xcc, sizeof(out.data));
    check(NtQuerySecurityAttributesToken(1, NULL, 1, NULL, 0, &required) == TSA_INVALID,
          "NULL filter with count");
    check(NtQuerySecurityAttributesToken(1, NULL, 0, NULL, 0, NULL) == TSA_INVALID,
          "required pointer required");

    bytes = fixture(provision.data);
    check(token_replace_security_attributes(&original, provision.data, bytes),
          "actual internal setter accepts provisioned fixture");
    check(original.modified_id == 1, "internal replacement updates modified id");
    raw = tsa_store_data(&original.security_attributes, &len);
    check(raw != provision.data && len == bytes && !memcmp(raw, provision.data, bytes),
          "token owns validated pointer-free data");
    check(!tsa_store_clone(&duplicate.security_attributes, &original.security_attributes) &&
          duplicate.security_attributes.data != original.security_attributes.data &&
          !memcmp(duplicate.security_attributes.data, original.security_attributes.data, bytes),
          "deep duplication preserves provisioned attributes");
    status = NtQuerySecurityAttributesToken(2, NULL, 0, NULL, 0, &required);
    check(status == TSA_TOO_SMALL && required < sizeof(out.data), "duplicated token query required");
    len = required;
    status = NtQuerySecurityAttributesToken(2, NULL, 0, out.data, len - 1, &required);
    check(status == TSA_TOO_SMALL && required == len && sentinel(out.data, sizeof(out.data)),
          "provisioned short buffer entirely untouched");
    status = NtQuerySecurityAttributesToken(2, NULL, 0, out.data, len, &required);
    check(!status && out.data[0] == 1 && out.data[4] == 2 &&
          sentinel(out.data + len, sizeof(out.data) - len), "native full query and canary");
    {
        uintptr_t attr, values, p;
        uint64_t value;
        uint32_t us = sizeof(void *) == 8 ? 16 : 8;
        uint32_t ab = sizeof(void *) == 8 ? 40 : 24;
        memcpy(&attr, out.data + 8, sizeof(attr));
        check(attr == (uintptr_t)(out.data + (sizeof(void *) == 8 ? 16 : 12)),
              "native header attribute pointer");
        memcpy(&values, (const void *)(attr + (sizeof(void *) == 8 ? 32 : 20)), sizeof(values));
        memcpy(&p, (const void *)(values + (sizeof(void *) == 8 ? 8 : 4)), sizeof(p));
        check(p >= (uintptr_t)out.data && p + 28 <= (uintptr_t)(out.data + len) &&
              tsa_u16((const void *)values) == 26 &&
              tsa_u16((const void *)p) == 'F', "native string UNICODE_STRING payload");
        memcpy(&values, (const void *)(attr + ab + (sizeof(void *) == 8 ? 32 : 20)), sizeof(values));
        memcpy(&value, (const void *)values, 8);
        check(value == UINT64_C(0x1122334455667788) &&
              tsa_u16((const void *)(attr + ab + us)) == 2,
              "native UINT64 payload preserved");
    }
    filter[0] = name(strings[0], "fixture.flags");
    memset(out.data, 0xcc, sizeof(out.data));
    status = NtQuerySecurityAttributesToken(2, filter, 1, out.data, sizeof(out.data), &required);
    check(!status && out.data[4] == 1 && required < len, "case-insensitive filtered query");
    filter[1] = name(strings[1], "Fixture.Missing");
    memset(out.data, 0xcc, sizeof(out.data));
    status = NtQuerySecurityAttributesToken(2, filter, 2, out.data, sizeof(out.data), &required);
    check(status == TSA_NOT_FOUND && !required && sentinel(out.data, sizeof(out.data)),
          "any missing filter fails without partial output");
    status = NtQuerySecurityAttributesToken(2, filter, 1, NULL, 4096, &required);
    check(status == TSA_INVALID, "NULL output with sufficient length");
    provision.data[0] = 2;
    check(!token_replace_security_attributes(&original, provision.data, bytes) &&
          server_error == TSA_INVALID && original.modified_id == 1,
          "failed replacement is atomic");
    tsa_store_destroy(&original.security_attributes);
    status = NtQuerySecurityAttributesToken(2, NULL, 0, out.data, sizeof(out.data), &required);
    check(!status && out.data[4] == 2, "duplicate survives original destruction");
    tsa_store_destroy(&duplicate.security_attributes);
}

static void validation_tests(void)
{
    union { uint64_t alignment; unsigned char data[4096]; } fixture_data, out;
    struct tsa_header *h = (struct tsa_header *)fixture_data.data;
    struct tsa_attribute *a = (struct tsa_attribute *)(h + 1);
    uint32_t bytes = fixture(fixture_data.data), saved, needed;
    struct tsa_store store = {0}, copy = {0};
    check(!tsa_validate(fixture_data.data, bytes), "valid fixture");
    check(tsa_validate(fixture_data.data, sizeof(*h) - 1) == TSA_INVALID, "truncated header");
    saved = a[0].values; a[0].values = bytes - 1;
    check(tsa_validate(fixture_data.data, bytes) == TSA_INVALID, "out-of-bounds value descriptor");
    a[0].values = saved;
    a[0].type = 4;
    check(tsa_validate(fixture_data.data, bytes) == TSA_NOT_SUPPORTED, "FQBN explicitly unsupported");
    a[0].type = 3; a[0].name_bytes |= 1;
    check(tsa_validate(fixture_data.data, bytes) == TSA_INVALID, "odd UTF16 name bytes");
    a[0].name_bytes &= ~1u;
    check(tsa_store_replace(&store, fixture_data.data, TSA_MAX_BYTES + 1) == TSA_INVALID,
          "bounded provisioning allocation");
    check(!tsa_store_clone(&copy, &store) && !copy.data, "empty duplication");
    memset(out.data, 0xcc, sizeof(out.data));
    check(tsa_query(fixture_data.data, bytes, NULL, 0, out.data, 0, &needed) == TSA_TOO_SMALL &&
          sentinel(out.data, sizeof(out.data)), "core size probe untouched");
    a[1].type = 6;
    check(tsa_validate(fixture_data.data, bytes) == TSA_INVALID, "boolean must be zero or one");
    {
        uint64_t value = 1;
        memcpy(fixture_data.data + a[1].values, &value, 8);
        check(!tsa_validate(fixture_data.data, bytes), "genuine boolean value accepted");
        a[1].type = 1;
        value = UINT64_C(0xffffffffffffffff);
        memcpy(fixture_data.data + a[1].values, &value, 8);
        check(!tsa_validate(fixture_data.data, bytes), "signed INT64 bit pattern preserved");
    }
    {
        struct tsa_header empty = {1, 0};
        uint32_t n;
        check(!tsa_store_replace(&store, &empty, sizeof(empty)) &&
              !tsa_store_clone(&copy, &store) && copy.data != store.data,
              "explicit empty storage cloned independently");
        check(tsa_store_data(&copy, &n) && n == sizeof(empty), "empty storage retrieval");
    }
    tsa_store_destroy(&store); tsa_store_destroy(&copy);
}

int main(void)
{
    query_tests(); validation_tests();
    check(handle_checks > 20 && releases > 15, "queries use checked/released server objects");
    printf("RESULT ABI=%u assertions=%u failures=%u checked_handles=%u released=%u\n",
           (unsigned)(sizeof(void *) * 8), assertions, failures, handle_checks, releases);
    return failures ? 1 : 0;
}
