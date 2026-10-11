#include <windows.h>
#include <stdio.h>

typedef LONG (WINAPI *QUERY_CONTEXT)(UINT32, void *, void **);

int main(void)
{
    HMODULE module = GetModuleHandleW(L"kernelbase.dll");
    QUERY_CONTEXT query = (QUERY_CONTEXT)(ULONG_PTR)GetProcAddress(module, "GetCurrentPackageGlobalizationContext");
    void *context;
    unsigned checks = 0, failures = 0;
    LONG status;

    if (!query) return 2;
    checks++;
    if (query(0, NULL, NULL) != ERROR_INVALID_PARAMETER) failures++;
    for (UINT32 index = 0; index < 3; index++) {
        context = (void *)0x1234;
        status = query(index, (void *)0x1234, &context);
        checks++;
        if (status != APPMODEL_ERROR_NO_PACKAGE || context != (void *)0x1234) failures++;
    }
    printf("RESULT unpackaged_globalization_checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
