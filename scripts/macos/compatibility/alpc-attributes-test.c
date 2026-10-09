#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "wine-alpc-attributes-types.h"

typedef SIZE_T (WINAPI *GET_SIZE)(ULONG);
typedef void *(WINAPI *GET_ATTRIBUTE)(ALPC_MESSAGE_ATTRIBUTES *, ULONG);
typedef LONG (WINAPI *INITIALIZE)(ULONG, ALPC_MESSAGE_ATTRIBUTES *, SIZE_T, SIZE_T *);

static unsigned checks, failures;

static void check(int condition, const char *name, ULONG flags)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        printf("FAIL %s flags=%08lx\n", name, flags);
    }
}

int main(int argc, char **argv)
{
    const ULONG bits[] = {0x80000000, 0x40000000, 0x20000000, 0x10000000,
                          0x08000000, 0x04000000, 0x02000000};
    const SIZE_T sizes[] = {24, 32, 32, 24, 24, 8, 8};
    union { ULONGLONG alignment; BYTE bytes[256]; } buffer;
    HMODULE module = GetModuleHandleW(L"ntdll.dll");
    GET_SIZE get_size = (GET_SIZE)(ULONG_PTR)GetProcAddress(module, "AlpcGetHeaderSize");
    GET_ATTRIBUTE get_attribute = (GET_ATTRIBUTE)(ULONG_PTR)GetProcAddress(module, "AlpcGetMessageAttribute");
    INITIALIZE initialize = (INITIALIZE)(ULONG_PTR)GetProcAddress(module, "AlpcInitializeMessageAttribute");
    unsigned mask, i, j;
    int native = argc == 2 && !strcmp(argv[1], "--native");
    if (!get_size || !get_attribute || !initialize || sizeof(SIZE_T) != 8)
    {
        fprintf(stderr, "Missing exports or non-x64 test runtime\n");
        return 2;
    }
    for (mask = 0; mask < 128; ++mask)
    {
        ULONG flags = 1;
        SIZE_T expected = 8, required, offset = 8;
        ALPC_MESSAGE_ATTRIBUTES *attributes = (ALPC_MESSAGE_ATTRIBUTES *)buffer.bytes;
        LONG status;
        for (i = 0; i < 7; ++i)
            if (mask & (1u << i)) { flags |= bits[i]; expected += sizes[i]; }
        check(get_size(flags) == expected, "header size", flags);
        required = 0xcccccccc00000000ULL;
        status = initialize(flags, NULL, 0, &required);
        check((ULONG)status == 0xc0000023 && required == expected, "query and eight-byte ABI", flags);
        memset(buffer.bytes, 0xcc, sizeof(buffer.bytes));
        status = initialize(flags, attributes, expected - 1, &required);
        check((ULONG)status == 0xc0000023 && required == expected, "undersized status", flags);
        for (j = 0; j < sizeof(buffer.bytes); ++j)
            check(buffer.bytes[j] == 0xcc, "undersized untouched", flags);
        status = initialize(flags, attributes, expected, &required);
        check(!status && required == expected, "exact-size initialization", flags);
        check(attributes->AllocatedAttributes == flags && !attributes->ValidAttributes, "header fields", flags);
        for (j = 8; j < sizeof(buffer.bytes); ++j)
            check(buffer.bytes[j] == 0xcc, "payload and tail untouched", flags);
        for (i = 0; i < 7; ++i)
        {
            void *expected_attribute = NULL;
            if (mask & (1u << i)) { expected_attribute = buffer.bytes + offset; offset += sizes[i]; }
            check(get_attribute(attributes, bits[i]) == expected_attribute, "attribute offset", flags);
        }
        check(!get_attribute(attributes, 0), "zero flag rejected", flags);
        /* Windows returns the end for allocated unknown bits; upstream rejects them. */
        check(get_attribute(attributes, 1) == (native ? buffer.bytes + expected : NULL),
              "unknown allocated flag behavior", flags);
        check(!get_attribute(attributes, 0xc0000000), "multiple flags rejected", flags);
        check(!initialize(flags, NULL, expected, &required), "null buffer sufficient size", flags);
    }
    {
        SIZE_T required = 0;
        LONG status = initialize(0xffffffff, (ALPC_MESSAGE_ATTRIBUTES *)buffer.bytes,
                                 sizeof(buffer.bytes), &required);
        check(!status && required == 160 &&
              ((ALPC_MESSAGE_ATTRIBUTES *)buffer.bytes)->AllocatedAttributes == 0xffffffff,
              "all unknown bits retained", 0xffffffff);
    }
    printf("ALPC_ATTRIBUTES checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
