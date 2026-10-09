#include <windows.h>
#include <appmodel.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    WCHAR buffer[256];
    UINT32 length;
    HANDLE process = NULL;
    LONG result;
    UINT32 required = 0;
    unsigned i, failures = 0;
    if (argc > 1)
    {
        process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, strtoul(argv[1], NULL, 10));
        if (!process) return 2;
        if (GetPackageFamilyName(process, &required, NULL) != ERROR_INSUFFICIENT_BUFFER)
            return 3;
    }
    for (i = 0; i < 6; ++i)
    {
        UINT32 *size = i < 2 ? NULL : &length;
        WCHAR *output = i == 0 || i == 2 || i == 3 ? NULL : buffer;
        length = i == 2 ? 1 : i == 5 ? 256 : 0;
        buffer[0] = 0xcccc;
        result = process ? GetPackageFamilyName(process, size, output)
                         : GetCurrentPackageFamilyName(size, output);
        printf("FAMILY_CASE=%u STATUS=%ld LENGTH=%u FIRST=%04x\n",
               i, result, length, (unsigned)buffer[0]);
        if (i < 3)
        {
            if (result != ERROR_INVALID_PARAMETER || length != (i == 2 ? 1u : 0u) ||
                buffer[0] != 0xcccc) ++failures;
        }
        else if (process)
        {
            if (result != (i == 5 ? ERROR_SUCCESS : ERROR_INSUFFICIENT_BUFFER) ||
                length != required || (i != 5 && buffer[0] != 0xcccc)) ++failures;
        }
        else if (result != APPMODEL_ERROR_NO_PACKAGE || length != (i == 5 ? 256u : 0u) ||
                 buffer[0] != 0xcccc) ++failures;
        if (!result && output) wprintf(L"ACTUAL_PACKAGE_FAMILY=%ls\n", output);
    }
    if (process) CloseHandle(process);
    printf("FAMILY_NAME_TEST failures=%u\n", failures);
    return failures ? 1 : 0;
}
