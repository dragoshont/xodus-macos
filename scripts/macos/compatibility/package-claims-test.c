/*
 * Differential control against native Windows ntdll and the standalone DLL.
 * Only a verified existing Microsoft.GamingApp XboxPcApp[CE].exe token and the
 * current test-process token are opened; no application is launched.
 *
 * x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Werror -O2 -DXPC_TEST
 *   package-claims.c package-claims-test.c -ladvapi32 -o package-claims-test.exe
 * Run on Windows: package-claims-test.exe <absolute DLL path> <Xbox PID>
 * Build/run the i686 variant too to check NTAPI and SIZE_T on both ABIs.
 * Synthetic provider cases below are fixtures, never production identities.
 *
 * Windows 2026-10-05 control: package/app byte counts include NUL; zero/one-byte
 * buffers return INVALID_PARAMETER, small buffers BUFFER_OVERFLOW (no size
 * update). Claim/mask precede string copies; GUID follows successful copies.
 * Native app!=NULL/app_size==NULL crashes: the prototype deliberately returns
 * INVALID_PARAMETER instead. No access-violation behavior is emulated.
 */
#define _WIN32_WINNT 0x0602
#include "package-claims.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef char claim_size_must_be_eight[(sizeof(XPC_PACKAGE_CLAIM) == 8) ? 1 : -1];

typedef struct OUTPUT {
    WCHAR Package[1024];
    WCHAR App[1024];
    SIZE_T PackageBytes;
    SIZE_T AppBytes;
    BYTE Dynamic[32];
    BYTE Claim[32];
    ULONG64 Present;
} OUTPUT;

static unsigned comparisons, failures, fixtures;

static NTSTATUS invoke(XPC_QUERY_CLAIMS query, HANDLE token, OUTPUT *out,
                       SIZE_T pn, SIZE_T an, unsigned omit)
{
    memset(out, 0xcc, sizeof(*out));
    out->PackageBytes = pn;
    out->AppBytes = an;
    return query(token, omit & 1 ? NULL : out->Package,
                 omit & 2 ? NULL : &out->PackageBytes,
                 omit & 4 ? NULL : out->App,
                 omit & 8 ? NULL : &out->AppBytes,
                 omit & 16 ? NULL : (GUID *)out->Dynamic,
                 omit & 32 ? NULL : (XPC_PACKAGE_CLAIM *)out->Claim,
                 omit & 64 ? NULL : &out->Present);
}

static void compare(XPC_QUERY_CLAIMS native, XPC_QUERY_CLAIMS compat, HANDLE token,
                    const char *label, SIZE_T pn, SIZE_T an, unsigned omit)
{
    OUTPUT a, b;
    NTSTATUS sa = invoke(native, token, &a, pn, an, omit);
    NTSTATUS sb = invoke(compat, token, &b, pn, an, omit);
    ++comparisons;
    if (sa != sb || memcmp(&a, &b, sizeof(a))) {
        ++failures;
        if (failures <= 20) {
            SIZE_T i;
            printf("FAIL %s capacities=%llu/%llu omit=%u status=%08lx/%08lx "
                   "sizes=%llu,%llu/%llu,%llu mask=%llx/%llx\n", label,
                   (unsigned long long)pn, (unsigned long long)an, omit,
                   (unsigned long)sa, (unsigned long)sb,
                   (unsigned long long)a.PackageBytes, (unsigned long long)a.AppBytes,
                   (unsigned long long)b.PackageBytes, (unsigned long long)b.AppBytes,
                   (unsigned long long)a.Present, (unsigned long long)b.Present);
            for (i = 0; i < sizeof(a); ++i) {
                BYTE x = ((BYTE *)&a)[i], y = ((BYTE *)&b)[i];
                if (x != y) {
                    printf(" first differing output byte=%llu native=%02x compat=%02x\n",
                           (unsigned long long)i, x, y);
                    break;
                }
            }
        }
    }
}

static int verified_xbox_token(DWORD pid, HANDLE *token)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    WCHAR path[2048], *file;
    DWORD count = sizeof(path) / sizeof(path[0]);
    static const WCHAR prefix[] = L"C:\\Program Files\\WindowsApps\\Microsoft.GamingApp_";
    int valid;
    if (!process) { printf("OpenProcess error=%lu\n", GetLastError()); return 0; }
    if (!QueryFullProcessImageNameW(process, 0, path, &count)) {
        printf("QueryFullProcessImageName error=%lu\n", GetLastError());
        CloseHandle(process); return 0;
    }
    file = wcsrchr(path, L'\\');
    valid = !_wcsnicmp(path, prefix, wcslen(prefix)) && file &&
        (!_wcsicmp(file, L"\\XboxPcApp.exe") || !_wcsicmp(file, L"\\XboxPcAppCE.exe"));
    if (!valid) {
        puts("Refusing token access: PID is not a verified official Xbox executable");
        CloseHandle(process); return 0;
    }
    wprintf(L"VERIFIED_XBOX_PATH=%ls PID=%lu\n", path, pid);
    valid = OpenProcessToken(process, TOKEN_QUERY, token);
    if (!valid) printf("OpenProcessToken error=%lu\n", GetLastError());
    CloseHandle(process);
    return valid;
}

typedef struct FIXTURE {
    XPC_ATTRIBUTES Info;
    XPC_ATTRIBUTE Attribute[2];
    UNICODE_STRING Strings[4];
    ULONG64 Packed;
    WCHAR Names[2][32];
    WCHAR Text[4][80];
} FIXTURE;

enum MODE {
    NORMAL, ABSENT_PKG, UNSUPPORTED, BAD_CLASS, EMPTY, WRONG_TYPE,
    MISSING_APP, DYNAMIC_GUID, INVALID_GUID, OPTIONAL_PRESENT,
    BAD_POINTER, BAD_VERSION, BAD_PKG_TYPE, RACING_SIZE, HUGE_SIZE
};
static enum MODE mode;
static unsigned calls;

static UNICODE_STRING fixture_string(WCHAR *s)
{
    UNICODE_STRING u;
    u.Length = (USHORT)(wcslen(s) * sizeof(WCHAR));
    u.MaximumLength = u.Length + sizeof(WCHAR);
    u.Buffer = s;
    return u;
}

static int is_name(const UNICODE_STRING *s, const WCHAR *name)
{
    SIZE_T n = wcslen(name) * sizeof(WCHAR);
    return s->Length == n && !memcmp(s->Buffer, name, n);
}

static NTSTATUS NTAPI fake_provider(HANDLE token, UNICODE_STRING *names,
                                   ULONG count, void *buffer, ULONG bytes,
                                   ULONG *needed)
{
    FIXTURE *f = buffer;
    unsigned i;
    (void)token;
    ++calls;
    *needed = 0;
    if (mode == UNSUPPORTED) return (NTSTATUS)0xc0000002;
    if (mode == BAD_CLASS) return (NTSTATUS)0xc0000003;
    if (mode == EMPTY) return (NTSTATUS)0xc0000225;
    if (count == 1 && (is_name(names, L"WP://SKUID") || is_name(names, L"XBOX://LI"))) {
        if (mode == OPTIONAL_PRESENT) { *needed = 100; return (NTSTATUS)0xc0000023; }
        return (NTSTATUS)0xc0000225;
    }
    if (!is_name(names, L"WIN://SYSAPPID") ||
        (count == 2 && !is_name(names + 1, L"WIN://PKG"))) {
        puts("FAIL fixture: queried non-package attributes"); ++failures;
        return (NTSTATUS)0xc000000d;
    }
    if (count == 2 && mode == ABSENT_PKG) return (NTSTATUS)0xc0000225;
    *needed = mode == HUGE_SIZE ? 2 * 1024 * 1024 : sizeof(FIXTURE);
    if (!buffer || bytes < sizeof(FIXTURE)) return (NTSTATUS)0xc0000023;
    if (mode == RACING_SIZE) { *needed = bytes + 16; return (NTSTATUS)0xc0000023; }
    memset(f, 0, sizeof(*f));
    f->Info.Version = mode == BAD_VERSION ? 2 : 1;
    f->Info.AttributeCount = count;
    f->Info.AttributeV1 = mode == BAD_POINTER ? (XPC_ATTRIBUTE *)(ULONG_PTR)1 : f->Attribute;
    wcscpy(f->Names[0], L"WIN://SYSAPPID");
    wcscpy(f->Names[1], L"WIN://PKG");
    wcscpy(f->Text[0], L"Fixture.Package_1.0_test");
    wcscpy(f->Text[1], L"Fixture.App");
    wcscpy(f->Text[2], L"Fixture.Family_test");
    wcscpy(f->Text[3], mode == INVALID_GUID ? L"not-a-guid" :
           L"{01234567-89ab-cdef-0123-456789abcdef}");
    for (i = 0; i < 4; ++i) f->Strings[i] = fixture_string(f->Text[i]);
    for (i = 0; i < count; ++i) f->Attribute[i].Name = fixture_string(f->Names[i]);
    f->Attribute[0].ValueType = mode == WRONG_TYPE ? 2 : 3;
    f->Attribute[0].ValueCount = mode == MISSING_APP ? 1 :
        (mode == DYNAMIC_GUID || mode == INVALID_GUID ? 4 : 3);
    f->Attribute[0].Values.String = f->Strings;
    f->Packed = ((ULONG64)3 << 32) | 5;
    f->Attribute[1].ValueType = mode == BAD_PKG_TYPE ? 3 : 2;
    f->Attribute[1].ValueCount = 1;
    f->Attribute[1].Values.Uint64 = &f->Packed;
    return 0;
}

static void check(int condition, const char *label)
{
    ++fixtures;
    if (!condition) { ++failures; printf("FAIL fixture %s\n", label); }
}

static void fixture_tests(void)
{
    enum MODE m;
    OUTPUT o;
    NTSTATUS status;
    for (m = NORMAL; m <= HUGE_SIZE; ++m) {
        mode = m; calls = 0;
        memset(&o, 0xcc, sizeof(o));
        o.PackageBytes = sizeof(o.Package); o.AppBytes = sizeof(o.App);
        status = XpcQueryPackageClaimsWithProvider(fake_provider, NULL,
            o.Package, &o.PackageBytes, o.App, &o.AppBytes,
            (GUID *)o.Dynamic, (XPC_PACKAGE_CLAIM *)o.Claim, &o.Present);
        switch (m) {
        case UNSUPPORTED: case BAD_CLASS:
            check(status == (NTSTATUS)0xc00000bb && !o.Present &&
                  o.Claim[0] == 0xcc && o.PackageBytes == sizeof(o.Package),
                  "unsupported provider preserves outputs"); break;
        case EMPTY:
            check(status == (NTSTATUS)0xc0000225 && !o.Present &&
                  o.Claim[0] == 0xcc, "no package"); break;
        case WRONG_TYPE: case BAD_POINTER: case BAD_VERSION: case BAD_PKG_TYPE:
        case HUGE_SIZE:
            check(status == (NTSTATUS)0xc000000d && !o.Present &&
                  o.Claim[0] == 0xcc, "malformed provider rejected"); break;
        case RACING_SIZE:
            check(status == (NTSTATUS)0xc0000023 && calls == 4 &&
                  !o.Present, "bounded size race"); break;
        case MISSING_APP:
            check(status == (NTSTATUS)0xc000050c && o.Dynamic[0] == 0xcc &&
                  o.Present == 3 && !wcscmp(o.Package, L"Fixture.Package_1.0_test"),
                  "missing app has partial package output"); break;
        default:
            check(status == 0 && !wcscmp(o.Package, L"Fixture.Package_1.0_test") &&
                  !wcscmp(o.App, L"Fixture.App") && o.Claim[8] == 0xcc &&
                  o.Dynamic[16] == 0xcc, "fixture success and guards");
            check(o.Present == (m == OPTIONAL_PRESENT ? 15u : m == ABSENT_PKG ? 1u : 3u),
                  "presence bits");
            check(((XPC_PACKAGE_CLAIM *)o.Claim)->Flags == (m == ABSENT_PKG ? 0u : 5u) &&
                  ((XPC_PACKAGE_CLAIM *)o.Claim)->Origin == (m == ABSENT_PKG ? 0u : 3u),
                  "packed flags/origin");
            if (m == DYNAMIC_GUID)
                check(((GUID *)o.Dynamic)->Data1 == 0x01234567 &&
                      ((GUID *)o.Dynamic)->Data2 == 0x89ab &&
                      o.Dynamic[15] == 0xef, "real GUID conversion");
            break;
        }
    }
    memset(&o, 0xcc, sizeof(o)); o.PackageBytes = 2048; o.AppBytes = 2048;
    status = XpcQueryPackageClaimsWithProvider(NULL, NULL, o.Package,
        &o.PackageBytes, o.App, &o.AppBytes, (GUID *)o.Dynamic,
        (XPC_PACKAGE_CLAIM *)o.Claim, &o.Present);
    check(status == (NTSTATUS)0xc00000bb && !o.Present &&
          o.PackageBytes == 2048 && o.Dynamic[0] == 0xcc, "missing provider");
    mode = NORMAL;
    status = XpcQueryPackageClaimsWithProvider(fake_provider, NULL, NULL, NULL,
        o.App, NULL, NULL, NULL, NULL);
    check(status == (NTSTATUS)0xc000000d, "safe app-size null deviation");
}

int main(int argc, char **argv)
{
    HMODULE dll;
    XPC_QUERY_CLAIMS native, compat;
    HANDLE current = NULL, xbox = NULL;
    OUTPUT identity;
    NTSTATUS status;
    SIZE_T capacities[14];
    unsigned i, j, omit, n = 0;
    DWORD pid;
    char *end;
    if (argc != 3) {
        puts("Usage: package-claims-test.exe <absolute prototype DLL path> <verified Xbox PID>");
        return 2;
    }
    pid = strtoul(argv[2], &end, 10);
    if (!pid || *end) return 2;
    dll = LoadLibraryA(argv[1]);
    if (!dll) { printf("LoadLibrary error=%lu\n", GetLastError()); return 2; }
    compat = (XPC_QUERY_CLAIMS)(ULONG_PTR)GetProcAddress(dll, "RtlQueryPackageClaims");
    native = (XPC_QUERY_CLAIMS)(ULONG_PTR)GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "RtlQueryPackageClaims");
    if (!native || !compat) { puts("Missing native/prototype export"); FreeLibrary(dll); return 2; }
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &current) ||
        !verified_xbox_token(pid, &xbox)) {
        if (current) CloseHandle(current);
        FreeLibrary(dll); return 2;
    }
    status = invoke(native, xbox, &identity, 2048, 2048, 0);
    if (status != 0) {
        printf("Native Xbox baseline failed: %08lx\n", (unsigned long)status);
        CloseHandle(xbox); CloseHandle(current); FreeLibrary(dll); return 2;
    }
    printf("ABI=%u CLAIM_BYTES=%u native_package_bytes=%llu native_app_bytes=%llu "
           "flags=%lu origin=%lu mask=%llu\n",
           (unsigned)(sizeof(void *) * 8), (unsigned)sizeof(XPC_PACKAGE_CLAIM),
           (unsigned long long)identity.PackageBytes, (unsigned long long)identity.AppBytes,
           ((XPC_PACKAGE_CLAIM *)identity.Claim)->Flags,
           ((XPC_PACKAGE_CLAIM *)identity.Claim)->Origin,
           (unsigned long long)identity.Present);
    capacities[n++] = 0; capacities[n++] = 1; capacities[n++] = 2; capacities[n++] = 3;
    capacities[n++] = identity.AppBytes - 1; capacities[n++] = identity.AppBytes;
    capacities[n++] = identity.AppBytes + 1;
    capacities[n++] = identity.PackageBytes - 1; capacities[n++] = identity.PackageBytes;
    capacities[n++] = identity.PackageBytes + 1; capacities[n++] = 2048;
    for (i = 0; i < n; ++i)
        for (j = 0; j < n; ++j)
            compare(native, compat, xbox, "Xbox", capacities[i], capacities[j], 0);
    /* Exhaust optional outputs, excluding the native access-violation case. */
    for (omit = 0; omit < 128; ++omit) {
        if ((omit & 8) && !(omit & 4)) continue;
        compare(native, compat, xbox, "Xbox optional", 2048, 2048, omit);
        compare(native, compat, current, "unpackaged optional", 2048, 2048, omit);
        compare(native, compat, (HANDLE)(ULONG_PTR)0x1234,
                "invalid optional", 2048, 2048, omit);
    }
    compare(native, compat, current, "unpackaged null query", 0, 0, 5);
    compare(native, compat, xbox, "Xbox null query", 0, 0, 5);
    compare(native, compat, NULL, "null handle", 2048, 2048, 0);
    compare(native, compat, (HANDLE)(LONG_PTR)-4, "current token pseudo-handle",
            2048, 2048, 0);
    fixture_tests();
    CloseHandle(xbox); CloseHandle(current); FreeLibrary(dll);
    printf("RESULT comparisons=%u fixtures=%u failures=%u\n", comparisons, fixtures, failures);
    return failures ? 1 : 0;
}
