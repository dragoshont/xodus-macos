#include <windows.h>
#include <stdio.h>

typedef HRESULT (WINAPI *QUERY_INFO)(UINT32, UINT32, UINT32 *, void *, UINT32 *);

int main(void)
{
    QUERY_INFO query = (QUERY_INFO)(ULONG_PTR)GetProcAddress(
        GetModuleHandleW(L"kernelbase.dll"), "GetCurrentPackageInfo3");
    UINT32 length, count, generation;
    unsigned failures = 0, checks = 0;
    HRESULT result;
    if (!query) {
        puts("FAIL GetCurrentPackageInfo3 export missing");
        return 2;
    }
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; \
    printf("FAIL line=%u\n", (unsigned)__LINE__); } } while (0)
    length = 0; count = 77;
    result = query(0, 16, &length, NULL, &count);
    CHECK(result == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));
    CHECK(length == 0 && count == 0);
    length = 3; count = 77; generation = 0xcccccccc;
    result = query(0, 16, &length, &generation, &count);
    CHECK(result == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));
    CHECK(length == 3 && count == 0 && generation == 0xcccccccc);
    length = sizeof(generation); count = 77;
    result = query(0, 16, &length, &generation, &count);
    CHECK(result == S_OK && generation == 0);
    CHECK(length == sizeof(generation) && count == 0);
    length = sizeof(generation); generation = 0xcccccccc;
    result = query(0, 16, &length, &generation, NULL);
    CHECK(result == S_OK && generation == 0);
    result = query(0, 16, NULL, &generation, NULL);
    CHECK(result == E_INVALIDARG);
    length = 4;
    result = query(0, 16, &length, NULL, &count);
    CHECK(result == E_INVALIDARG && length == 4);
    length = 0; count = 77;
    result = query(0, 0, &length, NULL, &count);
    CHECK(result == HRESULT_FROM_WIN32(APPMODEL_ERROR_NO_PACKAGE));
    CHECK(length == 0 && count == 77);
    printf("RESULT package_generation_checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
