/*
 * Prefix-local catalog-backed activation, identity only.
 * Copyright 2026 Xodus contributors
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Catalogs are explicit local-developer registrations verified by the registrar.
 * There is no public arbitrary-attribute setter, Store/sign-in state, sandbox
 * assertion or capability/privilege grant. Only caller-owned suspended children
 * with the exact registered image can acquire that registration's identity.
 */
#include "config.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#endif
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"
#include "process.h"
#include "thread.h"
#include "request.h"
#include "security.h"
#include "wine/token_security.h"

#define CATALOG_MAX (4 * 1024 * 1024)
struct catalog {
    unsigned char *data;
    size_t bytes;
    char *full, *app, *family, *image;
    unsigned char *files;
    unsigned int file_count;
};

static int u32( unsigned char **p, const unsigned char *end, unsigned int *value )
{
    if (end - *p < 4) return 0;
    memcpy( value, *p, 4 ); *p += 4;
    return 1;
}

static char *get_string( unsigned char **p, const unsigned char *end )
{
    unsigned int bytes, i;
    char *s;
    if (!u32( p, end, &bytes ) || !bytes || bytes > 4096 || end - *p < bytes) return NULL;
    for (i = 0; i < bytes; ++i)
        if ((*p)[i] < 32 || (*p)[i] > 126) return NULL; /* bounded ASCII catalog ABI */
    if (!(s = malloc( bytes + 1 ))) return NULL;
    memcpy( s, *p, bytes ); s[bytes] = 0; *p += bytes;
    return s;
}

static void free_catalog( struct catalog *c )
{
    free( c->full ); free( c->app ); free( c->family ); free( c->image ); free( c->data );
    memset( c, 0, sizeof(*c) );
}

static int file_hash_matches( const char *path, const unsigned char *expected )
{
#ifdef __APPLE__
    int fd;
    struct stat st;
    CC_SHA256_CTX ctx;
    unsigned char buf[65536], actual[CC_SHA256_DIGEST_LENGTH];
    ssize_t bytes;
    if (path[0] != '/' || strstr( path, "/../" ) || strstr( path, "/./" ) ||
        (fd = open( path, O_RDONLY | O_NOFOLLOW )) == -1) return 0;
    if (fstat( fd, &st ) || !S_ISREG( st.st_mode ))
    {
        close( fd ); return 0;
    }
    CC_SHA256_Init( &ctx );
    while ((bytes = read( fd, buf, sizeof(buf) )) > 0)
        CC_SHA256_Update( &ctx, buf, bytes );
    close( fd );
    if (bytes < 0) return 0;
    CC_SHA256_Final( actual, &ctx );
    return !memcmp( actual, expected, sizeof(actual) );
#else
    return 0;
#endif
}

static unsigned int load_catalog( struct catalog *c )
{
    const char *prefix = getenv( "WINEPREFIX" );
    char *path;
    struct stat st;
    int fd;
    unsigned int version, size, reserved, i;
    unsigned char *p, *end;
    size_t got = 0;
    ssize_t n;
    memset( c, 0, sizeof(*c) );
    if (!prefix || prefix[0] != '/') return STATUS_NOT_FOUND;
    if (!(path = malloc( strlen(prefix) + sizeof("/.xodus-local-packages/catalog.bin") )))
        return STATUS_NO_MEMORY;
    strcpy( path, prefix ); strcat( path, "/.xodus-local-packages/catalog.bin" );
    fd = open( path, O_RDONLY | O_NOFOLLOW ); free( path );
    if (fd == -1) return STATUS_NOT_FOUND;
    if (fstat( fd, &st ) || !S_ISREG( st.st_mode ) || st.st_uid != getuid() ||
        (st.st_mode & 0022) || st.st_size < 16 || st.st_size > CATALOG_MAX)
    {
        close( fd ); return STATUS_ACCESS_DENIED;
    }
    c->bytes = st.st_size;
    if (!(c->data = malloc( c->bytes ))) { close( fd ); return STATUS_NO_MEMORY; }
    while (got < c->bytes && (n = read( fd, c->data + got, c->bytes - got )) > 0) got += n;
    close( fd );
    if (got != c->bytes || memcmp( c->data, "XPA1", 4 )) goto invalid;
    p = c->data + 4; end = c->data + c->bytes;
    if (!u32( &p, end, &version ) || !u32( &p, end, &size ) || !u32( &p, end, &reserved ) ||
        version != 1 || reserved || size != c->bytes - 16 ||
        !(c->full = get_string( &p, end )) || !(c->app = get_string( &p, end )) ||
        !(c->family = get_string( &p, end )) || !(c->image = get_string( &p, end )) ||
        strncmp( c->image, "\\??\\", 4 ) ||
        !u32( &p, end, &c->file_count ) || !c->file_count || c->file_count > 8192) goto invalid;
    c->files = p;
    for (i = 0; i < c->file_count; ++i)
    {
        char *name = get_string( &p, end );
        int valid = name && end - p >= 32 && file_hash_matches( name, p );
        free( name );
        if (!valid) { free_catalog( c ); return STATUS_INVALID_IMAGE_HASH; }
        p += 32;
    }
    if (p != end) goto invalid;
    return STATUS_SUCCESS;
invalid:
    free_catalog( c );
    return STATUS_INVALID_PARAMETER;
}

static unsigned short fold( unsigned short c )
{
    return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
}

static int string_matches( const char *ascii, const WCHAR *text, unsigned int bytes )
{
    unsigned int i;
    if (bytes & 1 || strlen(ascii) != bytes / 2) return 0;
    for (i = 0; i < bytes / 2; ++i)
        if (fold( (unsigned char)ascii[i] ) != fold( text[i] )) return 0;
    return 1;
}

static int requested_registration( struct catalog *c, const void *data, unsigned int bytes,
                                   unsigned int package_bytes )
{
    if (!package_bytes || package_bytes >= bytes || package_bytes & 1 || bytes & 1) return 0;
    return string_matches( c->full, data, package_bytes ) &&
           string_matches( c->app, (const WCHAR *)data + package_bytes / 2, bytes - package_bytes );
}

static unsigned int write_utf16( unsigned char *data, unsigned int *at, const char *text )
{
    unsigned int start = *at, i;
    for (i = 0; text[i]; ++i)
    {
        uint16_t ch = (unsigned char)text[i];
        memcpy( data + *at, &ch, 2 ); *at += 2;
    }
    return start;
}

static int provision_identity( struct process *process, struct catalog *c )
{
    unsigned char *data;
    struct tsa_header *header;
    struct tsa_attribute *a;
    struct tsa_string *values;
    unsigned int at = sizeof(*header) + 2 * sizeof(*a), i, start;
    const char *strings[3] = { c->full, c->app, c->family };
    uint64_t claim = ((uint64_t)4 << 32) | 1; /* local DeveloperUnsigned; packaged format only */
    size_t capacity = 256 + 2 * (strlen(c->full) + strlen(c->app) + strlen(c->family));
    if (capacity > TSA_MAX_BYTES || !(data = calloc( 1, capacity ))) return 0;
    header = (struct tsa_header *)data; header->version = 1; header->count = 2;
    a = (struct tsa_attribute *)(header + 1);
    a[0].name = write_utf16( data, &at, "WIN://SYSAPPID" );
    a[0].name_bytes = at - a[0].name;
    a[1].name = write_utf16( data, &at, "WIN://PKG" );
    a[1].name_bytes = at - a[1].name;
    at = (at + 7) & ~7u;
    a[0].type = 3; a[0].count = 3; a[0].values = at;
    values = (struct tsa_string *)(data + at); at += 3 * sizeof(*values);
    for (i = 0; i < 3; ++i)
    {
        start = at; values[i].offset = write_utf16( data, &at, strings[i] );
        values[i].bytes = at - start;
    }
    at = (at + 7) & ~7u;
    a[1].type = 2; a[1].count = 1; a[1].values = at;
    memcpy( data + at, &claim, sizeof(claim) ); at += sizeof(claim);
    i = token_replace_security_attributes( process->token, data, at );
    free( data );
    return i;
}

DECL_HANDLER(query_registered_package)
{
    struct catalog catalog;
    unsigned int status = load_catalog( &catalog ), i, bytes;
    WCHAR *image;
    if (status) { set_error( status ); return; }
    if (!requested_registration( &catalog, get_req_data(), get_req_data_size(), req->package_bytes ))
    {
        set_error( STATUS_NOT_FOUND ); goto done;
    }
    bytes = (strlen(catalog.image) + 1) * sizeof(WCHAR);
    reply->image_bytes = bytes;
    if (get_reply_max_size() < bytes) { set_error( STATUS_BUFFER_TOO_SMALL ); goto done; }
    if (!(image = malloc( bytes ))) { set_error( STATUS_NO_MEMORY ); goto done; }
    for (i = 0; i < bytes / 2; ++i) image[i] = (unsigned char)catalog.image[i];
    set_reply_data_ptr( image, bytes );
done:
    free_catalog( &catalog );
}

DECL_HANDLER(activate_registered_package)
{
    struct catalog catalog;
    struct process *process;
    struct thread *thread;
    unsigned int status;
    const char *image;
    if (!(process = get_process_from_handle( req->process, PROCESS_SET_INFORMATION |
                                            PROCESS_QUERY_LIMITED_INFORMATION ))) return;
    if (process->parent_id != current->process->id || process->is_terminating ||
        process->startup_state != STARTUP_DONE || !process->imagelen ||
        !token_security_attributes_empty( process->token ))
    {
        set_error( STATUS_ACCESS_DENIED ); goto done_process;
    }
    LIST_FOR_EACH_ENTRY( thread, &process->thread_list, struct thread, proc_entry )
        if (!process->suspend && !thread->suspend) { set_error( STATUS_ACCESS_DENIED ); goto done_process; }
    status = load_catalog( &catalog );
    if (status) { set_error( status ); goto done_process; }
    if (!requested_registration( &catalog, get_req_data(), get_req_data_size(), req->package_bytes ))
    {
        set_error( STATUS_NOT_FOUND ); goto done_catalog;
    }
    image = catalog.image;
    if (process->imagelen == (strlen(image) - 4) * sizeof(WCHAR)) image += 4;
    if (!string_matches( image, process->image, process->imagelen ))
    {
        set_error( STATUS_INVALID_IMAGE_HASH ); goto done_catalog;
    }
    if (!provision_identity( process, &catalog )) set_error( STATUS_NO_MEMORY );
done_catalog:
    free_catalog( &catalog );
done_process:
    release_object( process );
}
