/* Paired native/Wine probe for GetCurrentPackageInfo3.
 * ABI (appmodel.h, MrmCoreR call sites): HRESULT (UINT32 flags, UINT32 type, UINT32 *length,
 * void *buffer, UINT32 *count). MrmCoreR uses type 0x10 (PackageInfoGeneration) with a 4-byte
 * buffer and type 0x11 with a NULL sizing call expecting HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER). */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef LONG (WINAPI *fn_t)(UINT32, UINT32, UINT32 *, void *, UINT32 *);

static void dump(const BYTE *b, UINT32 n)
{
    UINT32 i;
    for (i = 0; i < n; i++) printf("%02x%s", b[i], (i % 32 == 31) ? "\n    " : " ");
    printf("\n");
}

static void ascii(const BYTE *b, UINT32 n)
{
    UINT32 i;
    printf("    text: ");
    for (i = 0; i + 1 < n; i += 2) putchar(b[i] >= 32 && b[i] < 127 && !b[i + 1] ? b[i] : (b[i] || b[i + 1] ? '.' : '|'));
    printf("\n");
}

/* One NULL-argument case per process so a native access violation only loses that case. */
static int edge(fn_t fn, int which)
{
    static const struct { UINT32 type, len; BOOL buf, count, null_len; } cases[] =
    {
        { 17, 0, FALSE, FALSE, FALSE }, { 17, 8, FALSE, FALSE, FALSE }, { 17, 8, TRUE, FALSE, FALSE },
        { 17, 8, FALSE, TRUE, FALSE },  { 17, 0, FALSE, TRUE, TRUE },   { 17, 0, FALSE, FALSE, TRUE },
        { 0, 0, FALSE, FALSE, FALSE },  { 2, 0, FALSE, FALSE, FALSE },  { 0, 8, FALSE, TRUE, FALSE },
        { 17, 8, TRUE, TRUE, FALSE },   { 0, 8, TRUE, FALSE, FALSE },
    };
    static BYTE buf[64];
    UINT32 len, count = 77;
    LONG hr;

    if (which < 0 || which >= (int)ARRAYSIZE(cases)) return 2;
    len = cases[which].len;
    memset(buf, 0xcc, sizeof(buf));
    printf("edge %d type %u len_in=%u buf=%s count=%s len_ptr=%s: ", which, cases[which].type, len,
           cases[which].buf ? "ptr" : "NULL", cases[which].count ? "ptr" : "NULL",
           cases[which].null_len ? "NULL" : "ptr");
    fflush(stdout);
    hr = fn(0, cases[which].type, cases[which].null_len ? NULL : &len, cases[which].buf ? buf : NULL,
            cases[which].count ? &count : NULL);
    printf("hr=0x%08lx len=%u count=%u buf0=%02x\n", (unsigned long)hr, len, count, buf[0]);
    return 0;
}

int main(int argc, char **argv)
{
    fn_t fn = (fn_t)GetProcAddress(GetModuleHandleA("kernelbase.dll"), "GetCurrentPackageInfo3");
    static BYTE buf[65536];
    UINT32 type, flags, len, count;
    LONG hr;

    if (!fn) { printf("no export\n"); return 1; }
    if (argc == 3 && !strcmp(argv[1], "edge")) return edge(fn, atoi(argv[2]));
    for (type = 0; type <= 20; type++)
    {
        len = 0; count = 77;
        hr = fn(0, type, &len, NULL, &count);
        printf("type %u size: hr=0x%08lx len=%u count=%u\n", type, (unsigned long)hr, len, count);
        if (len && len <= sizeof(buf))
        {
            UINT32 need = len;
            memset(buf, 0xcc, sizeof(buf));
            count = 77;
            hr = fn(0, type, &len, buf, &count);
            printf("type %u fill: hr=0x%08lx len=%u count=%u\n    ", type, (unsigned long)hr, len, count);
            dump(buf, need > 512 ? 512 : need);
            ascii(buf, need > 2048 ? 2048 : need);
            len = need - 1; count = 77;
            hr = fn(0, type, &len, buf, &count);
            printf("type %u short: hr=0x%08lx len=%u count=%u\n", type, (unsigned long)hr, len, count);
        }
    }
    len = 4; count = 77; memset(buf, 0xcc, 8);
    hr = fn(0, 16, &len, buf, &count);
    printf("gen4: hr=0x%08lx len=%u count=%u val=%u\n", (unsigned long)hr, len, count, *(UINT32 *)buf);
    len = 8; count = 77; memset(buf, 0xcc, 8);
    hr = fn(0, 16, &len, buf, &count);
    printf("gen8: hr=0x%08lx len=%u count=%u bytes ", (unsigned long)hr, len, count); dump(buf, 8);
    for (flags = 0x10; flags <= 0x100000; flags <<= 1)
    {
        len = 0; count = 77;
        hr = fn(flags, 17, &len, NULL, &count);
        printf("flags %#x type 17 size: hr=0x%08lx len=%u count=%u\n", flags, (unsigned long)hr, len, count);
    }
    len = 4; count = 77;
    hr = fn(0, 16, &len, NULL, &count);
    printf("gen null buf len4: hr=0x%08lx len=%u count=%u\n", (unsigned long)hr, len, count);
    fflush(stdout);
    hr = fn(0, 16, NULL, buf, &count);
    printf("null len: hr=0x%08lx\n", (unsigned long)hr);
    fflush(stdout);
    len = 4;
    hr = fn(0, 16, &len, buf, NULL);
    printf("null count: hr=0x%08lx len=%u\n", (unsigned long)hr, len);
    return 0;
}
