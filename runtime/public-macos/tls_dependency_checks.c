/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdio.h>
#include <string.h>
#include <gnutls/crypto.h>
#include <gnutls/gnutls.h>

int main(void)
{
    static const unsigned char expected[] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };
    unsigned char digest[sizeof(expected)];
    gnutls_priority_t priority;
    int result = 1;

    if (gnutls_global_init() < 0)
    {
        fputs("GnuTLS initialization failed.\n", stderr);
        return 1;
    }
    if (!gnutls_check_version("3.8.13"))
        fputs("The compiled GnuTLS dependency is too old.\n", stderr);
    else if (gnutls_hash_fast(GNUTLS_DIG_SHA256, "abc", 3, digest) < 0
             || memcmp(digest, expected, sizeof(expected)))
        fputs("The real dependency failed its SHA-256 known-answer check.\n", stderr);
    else if (gnutls_priority_init(&priority, "NORMAL", NULL) < 0)
        fputs("Default TLS priorities could not be initialized.\n", stderr);
    else
    {
        gnutls_priority_deinit(priority);
        puts("GnuTLS version, SHA-256 and default priorities passed offline.");
        result = 0;
    }
    gnutls_global_deinit();
    return result;
}
