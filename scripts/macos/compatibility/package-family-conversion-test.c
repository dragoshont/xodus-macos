#include <windows.h>
#include <stdio.h>
#include <wchar.h>

typedef LONG (WINAPI *CONVERT)(const WCHAR *, UINT32 *, WCHAR *);
static unsigned checks, failures;

static void check(int condition, const char *label)
{
    checks++;
    if (!condition) {
        failures++;
        printf("FAIL %s\n", label);
    }
}

int main(void)
{
    static const WCHAR original[] = L"StudioMDHR.20872A364DAA1_1.3.8.2_x64__tm1s6a95559gt";
    static const WCHAR expected[] = L"StudioMDHR.20872A364DAA1_tm1s6a95559gt";
    static const WCHAR *invalid[] = {
        L"X_1.0.0.0_x64__8wekyb3d8bbwe",
        L"Bad_1.2_x64__8wekyb3d8bbwe",
        L"Bad_1.2.3.4_x99__8wekyb3d8bbwe",
        L"Bad_1.2.3.4_x64__BAD",
        L"B$d_1.2.3.4_x64__8wekyb3d8bbwe",
        L"Bad_65536.2.3.4_x64__8wekyb3d8bbwe",
        L"Bad_1.2.3.4junk_x64__8wekyb3d8bbwe",
        L"Bad_1.2.3.4_x64__!!!!!!!!!!!!!",
        L"Bad_1.2.3.4_x64_rrrrrrrrrrrrrrrrrrrrrrrrrrrrrrr_8wekyb3d8bbwe",
        NULL,
    };
    CONVERT convert = (CONVERT)(ULONG_PTR)GetProcAddress(GetModuleHandleW(L"kernelbase.dll"),
                                                       "PackageFamilyNameFromFullName");
    WCHAR output[128];
    UINT32 length, i;
    LONG result;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!convert) return 2;
    length = 0;
    check(convert(original, &length, NULL) == ERROR_INSUFFICIENT_BUFFER && length == 39,
          "exact family size includes terminator");
    wcscpy(output, L"sentinel");
    length = 38;
    check(convert(original, &length, output) == ERROR_INSUFFICIENT_BUFFER &&
          length == 39 && !wcscmp(output, L"sentinel"), "short buffer stays untouched");
    length = 128;
    check(!convert(original, &length, output) && length == 39 && !wcscmp(output, expected),
          "actual Cuphead family conversion");
    length = 128;
    check(!convert(L"Missing.Game_1.2.3.4_x64__8wekyb3d8bbwe", &length, output) &&
          length == 27 && !wcscmp(output, L"Missing.Game_8wekyb3d8bbwe"),
          "conversion needs no installed-package state");
    length = 128;
    check(!convert(L"Bad_01.2.3.4_X64_foo.bar_AAAAAAAAAAAAA", &length, output) &&
          !wcscmp(output, L"Bad_AAAAAAAAAAAAA"), "native case and leading-zero behavior");
    length = 128;
    check(convert(original, &length, NULL) == ERROR_INVALID_PARAMETER && length == 128,
          "nonzero capacity requires output");
    check(convert(original, NULL, NULL) == ERROR_INVALID_PARAMETER, "length is required");
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        length = 128;
        wcscpy(output, L"sentinel");
        result = convert(invalid[i], &length, output);
        check(result == ERROR_INVALID_PARAMETER && length == 128 &&
              !wcscmp(output, L"sentinel"), "invalid identity preserves output and capacity");
    }
    printf("RESULT package_family_checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
