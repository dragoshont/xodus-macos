#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef LONG (WINAPI *FIND_FAMILY)(const WCHAR *, UINT32, UINT32 *, WCHAR **,
                                 UINT32 *, WCHAR *, UINT32 *);

static void probe(FIND_FAMILY find, const char *label, const WCHAR *family, UINT32 filters,
                  UINT32 count, UINT32 length, int names_present, int buffer_present)
{
    WCHAR buffer[2048], *names[32];
    UINT32 properties[32];
    LONG result;
    if (count > 32 || length > 2048) return;
    memset(buffer, 0xcc, sizeof(buffer));
    memset(names, 0xcc, sizeof(names));
    memset(properties, 0xcc, sizeof(properties));
    result = find(family, filters, &count, names_present ? names : NULL, &length,
                  buffer_present ? buffer : NULL, properties);
    printf("FAMILY_PROBE %s rc=%ld count=%u chars=%u property0=%08lx name0_written=%d buffer_written=%d\n",
           label, result, count, length, (unsigned long)properties[0],
           names[0] != (WCHAR *)(ULONG_PTR)0xccccccccccccccccULL, buffer[0] != 0xcccc);
}

int main(int argc, char **argv)
{
    HMODULE module = GetModuleHandleW(L"kernelbase.dll");
    FIND_FAMILY find = (FIND_FAMILY)(ULONG_PTR)GetProcAddress(module, "FindPackagesByPackageFamily");
    WCHAR family[256], path[MAX_PATH];
    const WCHAR *unknown = L"Xodus.Unknown_rjy19t36rmgqt";
    UINT32 count = 7, length = 9;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (!find) return 2;
    if (!GetModuleFileNameW(module, path, MAX_PATH)) return 2;
    wprintf(L"FAMILY_PROVIDER=%ls\n", path);
    probe(find, "null-family", NULL, 0x10, 0, 0, 0, 0);
    probe(find, "empty-family", L"", 0x10, 0, 0, 0, 0);
    probe(find, "malformed-family", L"invalid", 0x10, 0, 0, 0, 0);
    probe(find, "unknown-zero-filter", unknown, 0, 0, 0, 0, 0);
    probe(find, "unknown-head", unknown, 0x10, 0, 0, 0, 0);
    probe(find, "unknown-head-resource", unknown, 0x50, 0, 0, 0, 0);
    printf("FAMILY_PROBE null-count rc=%ld\n", find(unknown, 0x10, NULL, NULL, &length, NULL, NULL));
    printf("FAMILY_PROBE null-length rc=%ld\n", find(unknown, 0x10, &count, NULL, NULL, NULL, NULL));
    if (argc == 2) {
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argv[1], -1, family, 256)) return 2;
        probe(find, "known-size", family, 0x10, 0, 0, 0, 0);
        probe(find, "known-fill", family, 0x10, 32, 2048, 1, 1);
        probe(find, "known-no-name-array", family, 0x10, 0, 2048, 0, 1);
        probe(find, "known-no-name-array-capacity", family, 0x10, 32, 2048, 0, 1);
        probe(find, "known-null-buffer", family, 0x10, 32, 2048, 1, 0);
        probe(find, "known-short-count", family, 0x10, 0, 2048, 1, 1);
        probe(find, "known-short-chars", family, 0x10, 32, 1, 1, 1);
        probe(find, "known-resource", family, 0x40, 32, 2048, 1, 1);
        probe(find, "known-direct", family, 0x20, 32, 2048, 1, 1);
        probe(find, "known-head-resource", family, 0x50, 32, 2048, 1, 1);
    } else if (argc != 1) return 2;
    return 0;
}
