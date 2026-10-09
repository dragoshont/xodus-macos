#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

typedef HRESULT (WINAPI *ENUM_CLASSES)(HSTRING, HSTRING **, DWORD *);

int main(void)
{
    static const WCHAR server[] = L"Microsoft.Xbox.AppL.AppXhg7mrtrf7ayze6gb4syw3zwjgyr2xv4r.mca";
    static const WCHAR *inputs[] = {
        L"-ServerName:Microsoft.Xbox.AppL.AppXhg7mrtrf7ayze6gb4syw3zwjgyr2xv4r.mca",
        L"-ServerName:-ServerName:Microsoft.Xbox.AppL.AppXhg7mrtrf7ayze6gb4syw3zwjgyr2xv4r.mca"
    };
    HMODULE module = LoadLibraryW(L"combase.dll");
    ENUM_CLASSES enumerate = (ENUM_CLASSES)(ULONG_PTR)GetProcAddress(module, "RoGetServerActivatableClasses");
    HSTRING_HEADER header;
    HSTRING prefix;
    UINT32 length;
    int failures = 0;
    unsigned int i;
    if (!enumerate || FAILED(RoInitialize(RO_INIT_MULTITHREADED))) return 2;
    memset(&header, 0xa5, sizeof(header));
    if (WindowsCreateStringReference(L"-ServerName:", 12, &header, &prefix) != S_OK) return 2;
    length = WindowsGetStringLen(prefix);
    printf("PREFIX_LENGTH=%u HEADER_SIZE=%u HANDLE_IS_HEADER=%d HEADER_BYTES=",
           length, (unsigned int)sizeof(header), prefix == (HSTRING)&header);
    for (i = 0; i < sizeof(header); ++i) printf("%02x", ((const unsigned char *)&header)[i]);
    printf("\n");
    if (length != 12 || wcscmp(WindowsGetStringRawBuffer(prefix, NULL), L"-ServerName:")) ++failures;
    for (i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i)
    {
        HSTRING input, output, *classes = NULL;
        DWORD count = 99;
        HRESULT status;
        const WCHAR *actual;
        if (FAILED(WindowsCreateString(inputs[i], lstrlenW(inputs[i]), &input))) return 2;
        status = WindowsSubstring(input, length, &output);
        if (status != S_OK) return 2;
        actual = WindowsGetStringRawBuffer(output, NULL);
        printf("SUBSTRING case=%u start=%u input_length=%u output_length=%u text=%ls\n",
               i, length, WindowsGetStringLen(input), WindowsGetStringLen(output), actual);
        if (wcscmp(actual, i ? inputs[0] : server)) ++failures;
        status = enumerate(output, &classes, &count);
        printf("SERVER_QUERY case=%u hr=%08lx count=%lu classes=%p\n", i, (unsigned long)status, count, classes);
        if (status != REGDB_E_CLASSNOTREG || count || classes) ++failures;
        WindowsDeleteString(output);
        WindowsDeleteString(input);
    }
    WindowsDeleteString(prefix);
    RoUninitialize();
    FreeLibrary(module);
    printf("HSTRING_PARSER_%s failures=%d\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
