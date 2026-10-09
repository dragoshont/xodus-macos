#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#define XODUS_SERVER_CATALOG_DECODER_ONLY
#include "rpcbindingcreate-server-catalog.c"

int main(int argc, char **argv)
{
    FILE *file;
    WCHAR *data;
    long size;
    DWORD count, i, pass;
    HSTRING *classes;
    const WCHAR *cursor;
    int failures = 0;
    if (argc != 2 || FAILED(RoInitialize(RO_INIT_MULTITHREADED)) ||
        !(file = fopen(argv[1], "rb"))) return 2;
    fseek(file, 0, SEEK_END); size = ftell(file); rewind(file);
    if (size < 4 || size > 128 * 1024 || !(data = malloc(size))) return 2;
    if (fread(data, 1, size, file) != (size_t)size) return 2;
    fclose(file);
    for (pass = 0; pass < 500; ++pass)
    {
        if (xodus_decode_server_classes(data, size, &classes, &count) != S_OK || count != 4) return 1;
        cursor = data;
        for (i = 0; i < count; ++i)
        {
            if (wcscmp(cursor, WindowsGetStringRawBuffer(classes[i], NULL))) ++failures;
            if (!pass) printf("AUTHORITATIVE_CLASS_%lu=%ls\n", i, cursor);
            cursor += wcslen(cursor) + 1;
            WindowsDeleteString(classes[i]);
        }
        CoTaskMemFree(classes);
    }
    classes = (HSTRING *)(ULONG_PTR)1; count = 99;
    if (xodus_decode_server_classes(data, size - 1, &classes, &count) != E_INVALIDARG || classes || count) ++failures;
    if (xodus_decode_server_classes(data, 0, &classes, &count) != E_INVALIDARG || classes || count) ++failures;
    data[size / 2 - 1] = 'x';
    if (xodus_decode_server_classes(data, size, &classes, &count) != E_INVALIDARG || classes || count) ++failures;
    data[size / 2 - 1] = 0; data[0] = 0;
    if (xodus_decode_server_classes(data, size, &classes, &count) != E_INVALIDARG || classes || count) ++failures;
    if (xodus_decode_server_classes(NULL, 4, &classes, &count) != E_INVALIDARG || classes || count) ++failures;
    if (xodus_decode_server_classes(NULL, 4, NULL, &count) != E_POINTER) ++failures;
    free(data); RoUninitialize();
    printf("SERVER_CLASS_ALLOCATION_%s cycles=500 failures=%d\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
