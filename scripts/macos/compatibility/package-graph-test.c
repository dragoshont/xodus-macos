#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#ifndef APPMODEL_ERROR_NO_MUTABLE_DIRECTORY
#define APPMODEL_ERROR_NO_MUTABLE_DIRECTORY 15707
#endif

typedef struct
{
    UINT32 reserved, architecture;
    UINT64 version;
    WCHAR *name, *publisher, *resource_id, *publisher_id;
} PACKAGE_ID_VIEW;

typedef struct
{
    UINT32 reserved, flags;
    WCHAR *path, *full_name, *family;
    PACKAGE_ID_VIEW identity;
} PACKAGE_INFO_VIEW;

typedef LONG (WINAPI *QUERY_GRAPH)(UINT32, UINT32, UINT32 *, BYTE *, UINT32 *);
typedef HRESULT (WINAPI *QUERY_GRAPH3)(UINT32, UINT32, UINT32 *, void *, UINT32 *);
typedef LONG (WINAPI *QUERY_NAME)(UINT32 *, WCHAR *);
typedef LONG (WINAPI *QUERY_ID)(UINT32 *, BYTE *);
typedef HRESULT (WINAPI *QUERY_MAX_VERSION)(const WCHAR *, UINT64 *);
typedef LONG (WINAPI *QUERY_STAGED)(const WCHAR *, UINT32, UINT32 *, WCHAR *);
typedef LONG (WINAPI *QUERY_GLOBALIZATION)(UINT32, void *, void **);
typedef LONG (WINAPI *QUERY_GLOBALIZATION_PROPERTY)(void *, UINT32, UINT32 *, void *);
struct globalization_context_view
{
    UINT32 tag, size, flags, app_id_bytes, reserved;
    WCHAR app_id[1];
};

static unsigned checks, failures;
static void check(int condition, const char *label)
{
    checks++;
    if (!condition) {
        failures++;
        printf("FAIL %s\n", label);
    }
}

static void test_globalization(HMODULE module)
{
    QUERY_GLOBALIZATION context = (QUERY_GLOBALIZATION)(ULONG_PTR)GetProcAddress(module, "GetCurrentPackageGlobalizationContext");
    QUERY_GLOBALIZATION_PROPERTY property = (QUERY_GLOBALIZATION_PROPERTY)(ULONG_PTR)GetProcAddress(module, "GetPackageGlobalizationProperty");
    unsigned before = checks, failed_before = failures;
    void *value = (void *)0x1234, *again = NULL;
    UINT32 size, buffer[2];
    LONG result;

    check(context && property, "globalization exports present");
    if (!context || !property) goto done;
    check(context(0, NULL, NULL) == ERROR_INVALID_PARAMETER, "null context output rejected");
    check(context(2, NULL, &value) == ERROR_NOT_FOUND && value == (void *)0x1234,
          "out of range preserves context output");
    result = context(0, NULL, &value);
    check(!result && value && value != (void *)0x1234, "manifest-bound default context");
    if (result || !value || value == (void *)0x1234) goto done;
    check(!context(0, (void *)0x1234, &again) && again == value,
          "reserved ignored and process context stable");
    {
        struct globalization_context_view *record = value;
        check(record->tag == 0x424f4c47 && record->size == 28 && record->flags == 0 &&
              record->app_id_bytes == 8 && !record->reserved && !wcscmp(record->app_id, L"App"),
              "first context follows native variable-length record");
        check(!context(1, NULL, &again) && again && again != value,
              "second declared application has its own context");
        if (again && again != value) {
            record = again;
            check(record->tag == 0x424f4c47 && record->size == 40 && record->flags == 0 &&
                  record->app_id_bytes == 20 && !wcscmp(record->app_id, L"Secondary"),
                  "second context uses real declared application id");
            size = 4; buffer[0] = 0xcccccccc;
            check(!property(again, 2, &size, buffer) && size == 4 && !buffer[0],
                  "second default context property");
        }
        again = (void *)0x1234;
        check(context(~0u, NULL, &again) == ERROR_NOT_FOUND && again == (void *)0x1234,
              "maximum context index preserves output");
    }
    for (UINT32 id = 1; id <= 2; id++) {
        size = 0;
        check(property(value, id, &size, NULL) == ERROR_INSUFFICIENT_BUFFER && size == 4,
              "default property sizing");
        size = 3; buffer[0] = 0xcccccccc;
        check(property(value, id, &size, buffer) == ERROR_INSUFFICIENT_BUFFER &&
              size == 4 && buffer[0] == 0xcccccccc, "short property preserves buffer");
        size = sizeof(buffer); buffer[0] = buffer[1] = 0xcccccccc;
        check(!property(value, id, &size, buffer) && size == 4 && !buffer[0] && !buffer[1],
              "default property clears full caller buffer");
    }
    size = sizeof(buffer); buffer[0] = buffer[1] = 0xcccccccc;
    check(property(value, 3, &size, buffer) == ERROR_INVALID_PARAMETER &&
          size == sizeof(buffer) && buffer[0] == 0xcccccccc, "unknown property preserves outputs");
    check(property(NULL, 1, &size, buffer) == ERROR_INVALID_PARAMETER, "null property context rejected");
    check(property(value, 1, NULL, buffer) == ERROR_INVALID_PARAMETER, "null property size rejected");
    check(property(value, 1, &size, NULL) == ERROR_INVALID_PARAMETER, "null sized property buffer rejected");
    check(property((void *)0x1234, 1, &size, buffer) == ERROR_INVALID_PARAMETER,
          "foreign context rejected without dereference");
done:
    printf("RESULT package_globalization_checks=%u failures=%u\n", checks - before, failures - failed_before);
}

int main(void)
{
    HMODULE kernelbase = GetModuleHandleW(L"kernelbase.dll");
    QUERY_GRAPH query = (QUERY_GRAPH)(ULONG_PTR)GetProcAddress(kernelbase, "GetCurrentPackageInfo2");
    QUERY_GRAPH3 query3 = (QUERY_GRAPH3)(ULONG_PTR)GetProcAddress(kernelbase, "GetCurrentPackageInfo3");
    QUERY_NAME name = (QUERY_NAME)(ULONG_PTR)GetProcAddress(kernelbase, "GetCurrentPackageFullName");
    QUERY_NAME path = (QUERY_NAME)(ULONG_PTR)GetProcAddress(kernelbase, "GetCurrentPackagePath");
    QUERY_NAME aumid = (QUERY_NAME)(ULONG_PTR)GetProcAddress(kernelbase, "GetCurrentApplicationUserModelId");
    QUERY_ID id = (QUERY_ID)(ULONG_PTR)GetProcAddress(kernelbase, "GetCurrentPackageId");
    QUERY_MAX_VERSION max_version = (QUERY_MAX_VERSION)(ULONG_PTR)GetProcAddress(kernelbase, "AppXGetOSMaxVersionTested");
    QUERY_STAGED staged = (QUERY_STAGED)(ULONG_PTR)GetProcAddress(kernelbase, "GetStagedPackagePathByFullName2");
    const UINT32 filters[] = {0, 0x10, 0x20, 0x80000};
    const UINT32 counts[] = {4, 1, 3, 4};
    WCHAR full_name[128];
    UINT32 length = 128, count, need, i, j;
    BYTE *buffer;
    LONG result;
    UINT64 version;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!query || !query3 || !name || !path || !aumid || !id || !max_version || !staged) {
        puts("FAIL package graph exports missing");
        return 2;
    }
    result = name(&length, full_name);
    check(!result && !wcsncmp(full_name, L"Xodus.PackageGraphTest_1.0.0.0_x64__",
                             wcslen(L"Xodus.PackageGraphTest_1.0.0.0_x64__")),
          "full name follows actual fixture identity");
    for (i = 0; i < sizeof(filters) / sizeof(filters[0]); i++) {
        length = 0; count = 77;
        result = query(filters[i], 2, &length, NULL, &count);
        check(result == ERROR_INSUFFICIENT_BUFFER && length > 0 && count == counts[i],
              "graph sizing and selected count");
        need = length;
        if (result != ERROR_INSUFFICIENT_BUFFER || need > 65536) continue;
        buffer = malloc(need);
        if (!buffer) return 3;
        result = query(filters[i], 2, &length, buffer, &count);
        check(!result && length == need && count == counts[i], "graph fill shape");
        if (!result) {
            PACKAGE_INFO_VIEW *info = (PACKAGE_INFO_VIEW *)buffer;
            for (j = 0; j < count; j++) {
                check(info[j].identity.architecture == 9 && info[j].path &&
                      info[j].full_name && info[j].family, "actual x64 graph and paths");
                if (info[j].flags & 1) {
                    check(!wcscmp(info[j].identity.publisher_id, L"8wekyb3d8bbwe") &&
                          !wcsncmp(info[j].identity.name, L"Microsoft.", 10),
                          "framework publisher is not inherited from test publisher");
                } else {
                    check(!wcscmp(info[j].full_name, full_name) &&
                          !wcscmp(info[j].identity.publisher, L"CN=Xodus.PackageGraphTest"),
                          "head matches registered token and real manifest");
                }
            }
        }
        length = need - 1;
        result = query(filters[i], 2, &length, buffer, &count);
        check(result == ERROR_INSUFFICIENT_BUFFER && length == need && count == counts[i],
              "short graph buffer reports exact requirement");
        free(buffer);
    }
    length = 0; count = 77;
    check(query3(0, 17, &length, NULL, &count) == S_OK && !length && !count,
          "static catalog has no dynamic dependencies");
    check(query3(0, 17, NULL, NULL, NULL) == E_INVALIDARG, "dynamic query requires length");
    length = 0; count = 77;
    check(query(0, 1, &length, NULL, &count) == APPMODEL_ERROR_NO_MUTABLE_DIRECTORY,
          "no invented mutable directory");
    length = 0;
    check(id(&length, NULL) == ERROR_INSUFFICIENT_BUFFER && length > sizeof(PACKAGE_ID_VIEW),
          "actual package id sizing");
    if (length && length < 65536) {
        buffer = malloc(length);
        if (!buffer) return 3;
        result = id(&length, buffer);
        check(!result && !wcscmp(((PACKAGE_ID_VIEW *)buffer)->name, L"Xodus.PackageGraphTest") &&
              !wcscmp(((PACKAGE_ID_VIEW *)buffer)->publisher, L"CN=Xodus.PackageGraphTest"),
              "package id binds real manifest publisher");
        free(buffer);
    }
    length = 0;
    check(path(&length, NULL) == ERROR_INSUFFICIENT_BUFFER && length > 0,
          "package path sizing");
    length = 128;
    result = path(&length, full_name);
    check(!result && wcsstr(full_name, L"\\graph-fixture") != NULL, "actual fixture root path");
    length = 0;
    check(aumid(&length, NULL) == ERROR_INSUFFICIENT_BUFFER && length > 0, "actual aumid sizing");
    length = 128;
    result = aumid(&length, full_name);
    check(!result && !wcscmp(full_name, L"Xodus.PackageGraphTest_rjy19t36rmgqt!App"),
          "aumid binds family and declared app id");
    length = 128;
    result = name(&length, full_name);
    version = 0;
    check(!result && max_version(full_name, &version) == S_OK &&
          version == (((UINT64)10 << 48) | ((UINT64)19041 << 16)),
          "OS max version follows declared manifest");
    version = 0xccccccccccccccccULL;
    check(max_version(L"Unknown_1.0.0.0_x64__8wekyb3d8bbwe", &version) ==
          HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) && version == 0xccccccccccccccccULL,
          "unknown package does not invent OS metadata");
    check(max_version(NULL, &version) == E_INVALIDARG && max_version(full_name, NULL) == E_INVALIDARG,
          "OS max version rejects null arguments");
    length = 0;
    check(staged(full_name, 2, &length, NULL) == ERROR_INSUFFICIENT_BUFFER && length > 0,
          "real staged package path sizing");
    {
        WCHAR root[260];
        length = 260;
        check(!staged(full_name, 2, &length, root) && wcsstr(root, L"\\graph-fixture") != NULL,
              "staged path follows actual manifest root");
    }
    length = 0;
    check(staged(full_name, 1, &length, NULL) == APPMODEL_ERROR_NO_MUTABLE_DIRECTORY,
          "staged path does not invent mutable location");
    check(staged(L"Unknown_1.0.0.0_x64__8wekyb3d8bbwe", 0, &length, NULL) == ERROR_NOT_FOUND,
          "unregistered package path rejected");
    check(staged(L"invalid", 0, &length, NULL) == ERROR_INVALID_PARAMETER,
          "malformed full package name rejected");
    check(staged(NULL, 0, &length, NULL) == ERROR_INVALID_PARAMETER,
          "null staged package name rejected");
    check(staged(L"", 0, &length, NULL) == ERROR_MORE_DATA, "empty staged name reports native error");
    printf("RESULT package_graph_checks=%u failures=%u\n", checks, failures);
    test_globalization(kernelbase);
    return failures ? 1 : 0;
}
