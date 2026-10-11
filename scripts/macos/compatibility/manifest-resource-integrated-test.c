#include <windows.h>
#include <stdio.h>
#include <wchar.h>

typedef LONG (WINAPI *QUERY_CONTEXT)(UINT32, void *, void **);
typedef LONG (WINAPI *QUERY_PROPERTY)(void *, UINT32, UINT32 *, void *, UINT32 *);
typedef HRESULT (WINAPI *QUERY_STRING)(UINT32, UINT32, SIZE_T, WCHAR *, SIZE_T *);

static unsigned checks, failures;

static void check(BOOL condition, const char *name)
{
    checks++;
    if (!condition) {
        failures++;
        printf("FAIL %s\n", name);
    }
}

int wmain(int argc, WCHAR **argv)
{
    HMODULE kernelbase = GetModuleHandleW(L"kernelbase.dll");
    HMODULE provider;
    QUERY_CONTEXT package, application;
    QUERY_PROPERTY property;
    QUERY_STRING string;
    WCHAR buffer[512], path[MAX_PATH];
    void *context = (void *)(ULONG_PTR)0x1234;
    SIZE_T count;
    UINT32 bytes, flags;
    HRESULT hr;
    LONG status;
    BOOL packaged = argc == 3 && !wcscmp(argv[1], L"--packaged-literal");

    if (argc != 1 && !packaged) return 2;
    if (packaged && wcslen(argv[2]) >= 511) return 2;
    package = (QUERY_CONTEXT)(ULONG_PTR)GetProcAddress(kernelbase, "GetCurrentPackageResourcesContext");
    if (!package) {
        printf("FAIL export=GetCurrentPackageResourcesContext error=%lu\n", GetLastError());
        return 2;
    }
    application = (QUERY_CONTEXT)(ULONG_PTR)GetProcAddress(kernelbase, "GetCurrentPackageApplicationResourcesContext");
    if (!application) {
        printf("FAIL export=GetCurrentPackageApplicationResourcesContext error=%lu\n", GetLastError());
        return 2;
    }
    property = (QUERY_PROPERTY)(ULONG_PTR)GetProcAddress(kernelbase, "GetPackageResourcesProperty");
    if (!property) {
        printf("FAIL export=GetPackageResourcesProperty error=%lu\n", GetLastError());
        return 2;
    }
    if (!GetModuleFileNameW(kernelbase, path, MAX_PATH)) return 2;
    printf("KERNELBASE %ls package_rva=%llx application_rva=%llx property_rva=%llx\n",
           path, (unsigned long long)((ULONG_PTR)package - (ULONG_PTR)kernelbase),
           (unsigned long long)((ULONG_PTR)application - (ULONG_PTR)kernelbase),
           (unsigned long long)((ULONG_PTR)property - (ULONG_PTR)kernelbase));
    puts("APISET request=ext-ms-win-mrmcorer-resmanager-l1-1-0.dll");
    provider = LoadLibraryW(L"ext-ms-win-mrmcorer-resmanager-l1-1-0.dll");
    if (!provider) {
        printf("FAIL provider_load=ext-ms-win-mrmcorer-resmanager-l1-1-0.dll error=%lu\n", GetLastError());
        return 2;
    }
    string = (QUERY_STRING)(ULONG_PTR)GetProcAddress(provider, "GetStringValueForManifestField");
    if (!string) {
        printf("FAIL export=GetStringValueForManifestField error=%lu\n", GetLastError());
        return 2;
    }
    if (!GetModuleFileNameW(provider, path, MAX_PATH)) return 2;
    printf("PROVIDER %ls\n", path);
    check(package(0, NULL, NULL) == ERROR_INVALID_PARAMETER, "null_package_output");
    check(application(0, NULL, NULL) == ERROR_INVALID_PARAMETER, "null_application_output");
    bytes = 0; flags = 0xfeed;
    check(property(NULL, 1, &bytes, NULL, &flags) == ERROR_INVALID_PARAMETER &&
          bytes == 0 && flags == 0xfeed, "null_property_context");
    status = application(0, NULL, &context);
    printf("CONTEXT status=%ld\n", status);
    if (!packaged) {
        check(status == APPMODEL_ERROR_NO_PACKAGE && context == (void *)(ULONG_PTR)0x1234,
              "unpackaged_application_context");
        check(package(0, NULL, &context) == APPMODEL_ERROR_NO_PACKAGE &&
              context == (void *)(ULONG_PTR)0x1234, "unpackaged_package_context");
        count = 0xfeed;
        buffer[0] = 0xaaaa;
        hr = string(2, 1, 512, buffer, &count);
        printf("STRING unpackaged hr=%08lx count=%llu first=%04x\n",
               (unsigned long)hr, (unsigned long long)count, buffer[0]);
        check(hr == HRESULT_FROM_WIN32(APPMODEL_ERROR_NO_PACKAGE) &&
              count == 0 && buffer[0] == 0, "unpackaged_native_consumer");
    } else {
        SIZE_T expected = wcslen(argv[2]) + 1;
        check(status == ERROR_SUCCESS && context != NULL &&
              context != (void *)(ULONG_PTR)0x1234, "fixture_application_context");
        if (status != ERROR_SUCCESS) return 1;
        bytes = 0; flags = 0xfeed;
        status = property(context, 1, &bytes, NULL, &flags);
        check(status == ERROR_INSUFFICIENT_BUFFER &&
              bytes == 8 + expected * sizeof(WCHAR) && flags == 0, "fixture_property_byte_count");
        count = 0xfeed;
        hr = string(2, 1, 0, NULL, &count);
        printf("STRING query hr=%08lx count=%llu\n", (unsigned long)hr, (unsigned long long)count);
        check(hr == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) &&
              count == expected, "fixture_native_size_query");
        buffer[0] = 0xaaaa; count = 0xfeed;
        hr = string(2, 1, expected - 1, buffer, &count);
        check(hr == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) &&
              count == expected && (!expected || expected == 1 || buffer[0] == 0),
              "fixture_native_short_buffer");
        buffer[0] = 0xaaaa; buffer[expected] = 0xaaaa; count = 0xfeed;
        hr = string(2, 1, expected, buffer, &count);
        printf("STRING exact hr=%08lx count=%llu\n", (unsigned long)hr, (unsigned long long)count);
        check(hr == S_OK && count == expected && !wcscmp(buffer, argv[2]) &&
              buffer[expected] == 0xaaaa, "fixture_native_literal");
    }
    FreeLibrary(provider);
    printf("RESULT manifest_resource_integrated_checks=%u failures=%u fixture=%u\n",
           checks, failures, packaged);
    return failures ? 1 : 0;
}
