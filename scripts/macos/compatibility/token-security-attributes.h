/*
 * Token security-attribute wire storage and native query marshalling.
 * Copyright 2026 Xodus contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This header is also installed as include/wine/token_security.h by the
 * accompanying Wine patch. No package provisioning or public token setter is
 * implemented here. Producers must supply genuine activation attributes.
 */
#ifndef XODUS_TOKEN_SECURITY_ATTRIBUTES_H
#define XODUS_TOKEN_SECURITY_ATTRIBUTES_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define TSA_SUCCESS       UINT32_C(0)
#define TSA_INVALID       UINT32_C(0xc000000d)
#define TSA_NO_MEMORY     UINT32_C(0xc0000017)
#define TSA_TOO_SMALL     UINT32_C(0xc0000023)
#define TSA_NOT_SUPPORTED UINT32_C(0xc00000bb)
#define TSA_NOT_FOUND     UINT32_C(0xc0000225)
#define TSA_MAX_BYTES     (1024u * 1024u)
#define TSA_MAX_ATTRS     256u

struct tsa_header { uint32_t version, count; };
struct tsa_attribute {
    uint32_t name, name_bytes;
    uint16_t type, reserved;
    uint32_t flags, count, values;
};
struct tsa_string { uint32_t offset, bytes; };
struct tsa_name { const void *text; uint32_t bytes; };
struct tsa_store { void *data; uint32_t bytes; };

static inline int tsa_range(uint32_t bytes, uint32_t offset, uint64_t size)
{
    return offset <= bytes && size <= bytes - offset;
}

static inline uint16_t tsa_u16(const void *p)
{
    uint16_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static inline int tsa_equal(const void *a, const void *b, uint32_t bytes)
{
    uint32_t i;
    for (i = 0; i < bytes; i += 2) {
        uint16_t x = tsa_u16((const char *)a + i), y = tsa_u16((const char *)b + i);
        /* Security-attribute names used by package activation are ASCII.
           Preserve exact equality for non-ASCII instead of locale folding. */
        if (x >= 'a' && x <= 'z') x -= 'a' - 'A';
        if (y >= 'a' && y <= 'z') y -= 'a' - 'A';
        if (x != y) return 0;
    }
    return 1;
}

static inline uint32_t tsa_validate(const void *data, uint32_t bytes)
{
    const struct tsa_header *h = data;
    const struct tsa_attribute *attrs;
    uint32_t i, j;
    if (!data || bytes < sizeof(*h) || bytes > TSA_MAX_BYTES ||
        h->version != 1 || h->count > TSA_MAX_ATTRS ||
        !tsa_range(bytes, sizeof(*h), (uint64_t)h->count * sizeof(*attrs)))
        return TSA_INVALID;
    attrs = (const struct tsa_attribute *)(h + 1);
    for (i = 0; i < h->count; ++i) {
        const struct tsa_attribute *a = attrs + i;
        if (!a->name_bytes || a->name_bytes > 65532 || (a->name_bytes & 1) ||
            !tsa_range(bytes, a->name, a->name_bytes) || a->reserved ||
            a->count > TSA_MAX_BYTES / 8) return TSA_INVALID;
        for (j = 0; j < a->name_bytes; j += 2)
            if (!tsa_u16((const char *)data + a->name + j)) return TSA_INVALID;
        for (j = 0; j < i; ++j)
            if (attrs[j].name_bytes == a->name_bytes &&
                tsa_equal((const char *)data + attrs[j].name,
                          (const char *)data + a->name, a->name_bytes))
                return TSA_INVALID;
        switch (a->type) {
        case 1: case 2: case 6:
            if (!tsa_range(bytes, a->values, (uint64_t)a->count * 8)) return TSA_INVALID;
            if (a->type == 6)
                for (j = 0; j < a->count; ++j) {
                    uint64_t value;
                    memcpy(&value, (const char *)data + a->values + j * 8, 8);
                    if (value > 1) return TSA_INVALID;
                }
            break;
        case 3:
            if (!tsa_range(bytes, a->values,
                           (uint64_t)a->count * sizeof(struct tsa_string))) return TSA_INVALID;
            for (j = 0; j < a->count; ++j) {
                struct tsa_string s;
                memcpy(&s, (const char *)data + a->values + j * sizeof(s), sizeof(s));
                if (s.bytes > 65532 || (s.bytes & 1) ||
                    !tsa_range(bytes, s.offset, s.bytes)) return TSA_INVALID;
            }
            break;
        default: return TSA_NOT_SUPPORTED;
        }
    }
    return TSA_SUCCESS;
}

static inline void tsa_store_destroy(struct tsa_store *s)
{
    free(s->data);
    s->data = NULL;
    s->bytes = 0;
}

/* Atomic replacement: invalid or unallocatable data leaves the old state. */
static inline uint32_t tsa_store_replace(struct tsa_store *s, const void *data,
                                        uint32_t bytes)
{
    uint32_t status = tsa_validate(data, bytes);
    void *copy;
    if (status) return status;
    if (!(copy = malloc(bytes))) return TSA_NO_MEMORY;
    memcpy(copy, data, bytes);
    tsa_store_destroy(s);
    s->data = copy;
    s->bytes = bytes;
    return TSA_SUCCESS;
}

static inline uint32_t tsa_store_clone(struct tsa_store *dest, const struct tsa_store *src)
{
    if (!src->data) { tsa_store_destroy(dest); return TSA_SUCCESS; }
    return tsa_store_replace(dest, src->data, src->bytes);
}

static inline const void *tsa_store_data(const struct tsa_store *s, uint32_t *bytes)
{
    static const struct tsa_header empty = {1, 0};
    *bytes = s->data ? s->bytes : sizeof(empty);
    return s->data ? s->data : &empty;
}

static inline uint32_t tsa_align(uint32_t offset, uint32_t align)
{
    return (offset + align - 1) & ~(align - 1);
}

static inline void tsa_pointer(void *dest, const void *value, uint32_t word)
{
    uintptr_t address = (uintptr_t)value;
    memcpy(dest, &address, word);
}

static inline void tsa_native_string(char *u, char *text, const void *source,
                                     uint32_t bytes, uint32_t word)
{
    uint16_t length = (uint16_t)bytes, maximum = (uint16_t)(bytes + 2);
    memcpy(u, &length, 2); memcpy(u + 2, &maximum, 2);
    tsa_pointer(u + (word == 8 ? 8 : 4), text, word);
    if (bytes) memcpy(text, source, bytes);
    memset(text + bytes, 0, 2);
}

/* names==NULL/count==0 means all; any missing requested name is NOT_FOUND.
   Caller pointers must be valid as for the native syscall. No data is written
   on a short buffer or a missing filter. The native pointer-width layout is
   computed explicitly so server storage never embeds client pointers. */
static inline uint32_t tsa_query(const void *data, uint32_t bytes,
                                const struct tsa_name *names, uint32_t count,
                                void *output, uint32_t capacity, uint32_t *required)
{
    const struct tsa_header *h = data;
    const struct tsa_attribute *attrs;
    uint32_t selected[TSA_MAX_ATTRS], n = 0, i, j, offset, total, status;
    uint32_t word = sizeof(void *), us = word == 8 ? 16 : 8;
    uint32_t attr_bytes = word == 8 ? 40 : 24, header = word == 8 ? 16 : 12;
    char *out = output;
    if (!required || (count && !names) || (!count && names) ||
        count > TSA_MAX_ATTRS) return TSA_INVALID;
    *required = 0;
    if ((status = tsa_validate(data, bytes))) return status;
    attrs = (const struct tsa_attribute *)(h + 1);
    if (names) {
        for (i = 0; i < count; ++i) {
            if (!names[i].text || !names[i].bytes || (names[i].bytes & 1))
                return TSA_INVALID;
            for (j = 0; j < h->count; ++j)
                if (attrs[j].name_bytes == names[i].bytes &&
                    tsa_equal((const char *)data + attrs[j].name,
                              names[i].text, names[i].bytes)) break;
            if (j == h->count) return TSA_NOT_FOUND;
            selected[n++] = j;
        }
    } else {
        for (i = 0; i < h->count; ++i) selected[n++] = i;
    }
    offset = header + n * attr_bytes;
    for (i = 0; i < n; ++i) {
        const struct tsa_attribute *a = attrs + selected[i];
        offset += a->name_bytes + 2;
        offset = tsa_align(offset, a->type == 3 ? word : 8);
        offset += a->count * (a->type == 3 ? us : 8);
        if (offset > TSA_MAX_BYTES) return TSA_INVALID;
        if (a->type == 3)
            for (j = 0; j < a->count; ++j) {
                struct tsa_string s;
                memcpy(&s, (const char *)data + a->values + j * sizeof(s), sizeof(s));
                if (s.bytes + 2 > TSA_MAX_BYTES - offset) return TSA_INVALID;
                offset += s.bytes + 2;
            }
        /* Native expansion may exceed the wire size but is bounded too. */
        if (offset > TSA_MAX_BYTES) return TSA_INVALID;
    }
    *required = total = offset;
    if (capacity < total) return TSA_TOO_SMALL;
    if (!output) return TSA_INVALID;
    memset(output, 0, total);
    out[0] = 1;
    memcpy(out + 4, &n, 4);
    if (n) tsa_pointer(out + 8, out + header, word);
    offset = header + n * attr_bytes;
    for (i = 0; i < n; ++i) {
        const struct tsa_attribute *a = attrs + selected[i];
        char *native = out + header + i * attr_bytes, *values;
        tsa_native_string(native, out + offset, (const char *)data + a->name,
                          a->name_bytes, word);
        memcpy(native + us, &a->type, 2);
        memcpy(native + us + 4, &a->flags, 4);
        memcpy(native + us + 8, &a->count, 4);
        offset = tsa_align(offset + a->name_bytes + 2, a->type == 3 ? word : 8);
        values = out + offset;
        tsa_pointer(native + (word == 8 ? 32 : 20), values, word);
        offset += a->count * (a->type == 3 ? us : 8);
        if (a->type == 3) {
            for (j = 0; j < a->count; ++j) {
                struct tsa_string s;
                memcpy(&s, (const char *)data + a->values + j * sizeof(s), sizeof(s));
                tsa_native_string(values + j * us, out + offset,
                                  (const char *)data + s.offset, s.bytes, word);
                offset += s.bytes + 2;
            }
        } else if (a->count) memcpy(values, (const char *)data + a->values, a->count * 8);
    }
    return TSA_SUCCESS;
}

#endif
