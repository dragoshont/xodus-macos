/*
 * Tests the production class-39 adapter with explicit fixtures and the loaded
 * combined DLL on real own/verified Xbox tokens. Only public package filters
 * are requested on real tokens; no account/token payloads are printed.
 */
#define _WIN32_WINNT 0x0602
#include "token-query-provider.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef struct { ULONG64 Version; UNICODE_STRING Name; } FQBN;
typedef struct { void *Value; ULONG Bytes; } OCTET;
typedef struct {
    XPC_ATTRIBUTES Info;
    XPC_ATTRIBUTE Attr;
    WCHAR Name[32], Text[64];
    union { ULONG64 Number; UNICODE_STRING String; FQBN Fqbn; OCTET Octet; } Value;
    BYTE Raw[16];
} FIXTURE;

enum MODE { EMPTY, UNSUPPORTED, BAD_VERSION, UNVERIFIED_EMPTY, STRING_TYPE, NUMBER, FQBN_TYPE,
            SID_TYPE, BOOLEAN_TYPE, OCTET_TYPE, SIGNED_TYPE, ZERO_VALUES,
            BAD_POINTER, RACING_SIZE };
static enum MODE mode;
static unsigned type_calls, info_calls, tests, failures, real_tests;

static void check(int condition, const char *label)
{
    ++tests;
    if (!condition) { ++failures; printf("FAIL %s\n", label); }
}

static UNICODE_STRING string(WCHAR *text)
{
    UNICODE_STRING s;
    s.Length = (USHORT)(wcslen(text) * sizeof(WCHAR));
    s.MaximumLength = s.Length + sizeof(WCHAR); s.Buffer = text;
    return s;
}

static int unchanged(const BYTE *data, SIZE_T bytes)
{
    SIZE_T i;
    for (i = 0; i < bytes; ++i) if (data[i] != 0xcc) return 0;
    return 1;
}

static NTSTATUS NTAPI fixture_info(HANDLE token, TOKEN_INFORMATION_CLASS cls,
                                  void *buffer, ULONG bytes, ULONG *required)
{
    FIXTURE *f = buffer;
    if (cls == TokenType) {
        ++type_calls;
        if (token == (HANDLE)(ULONG_PTR)0x1234) return (NTSTATUS)0xc0000008;
        if (token == (HANDLE)(ULONG_PTR)3) return (NTSTATUS)0xc0000022;
        if (bytes < sizeof(ULONG)) return (NTSTATUS)0xc0000023;
        *(ULONG *)buffer = TokenPrimary; *required = sizeof(ULONG); return 0;
    }
    ++info_calls;
    if (cls != (TOKEN_INFORMATION_CLASS)39) return (NTSTATUS)0xc0000003;
    if (mode == UNSUPPORTED) return (NTSTATUS)0xc0000002;
    *required = sizeof(*f);
    if (bytes < sizeof(*f)) return (NTSTATUS)0xc0000023;
    if (mode == RACING_SIZE) { *required = bytes + 16; return (NTSTATUS)0xc0000023; }
    memset(f, 0, sizeof(*f));
    f->Info.Version = mode == BAD_VERSION ? 0 : 1;
    if (mode == UNVERIFIED_EMPTY) { *required = 0; return 0; }
    if (mode == EMPTY || mode == BAD_VERSION) return 0;
    f->Info.AttributeCount = 1; f->Info.AttributeV1 = &f->Attr;
    wcscpy(f->Name, L"Fixture.Attribute");
    wcscpy(f->Text, L"Fixture.Value");
    f->Attr.Name = string(f->Name); f->Attr.Flags = 2; f->Attr.ValueCount = 1;
    f->Attr.Values.Pointer = &f->Value;
    switch (mode) {
    case STRING_TYPE:
        f->Attr.ValueType = 3; f->Value.String = string(f->Text); break;
    case NUMBER:
        f->Attr.ValueType = 2; f->Value.Number = 0x1122334455667788ULL; break;
    case FQBN_TYPE:
        f->Attr.ValueType = 4; f->Value.Fqbn.Version = 42;
        f->Value.Fqbn.Name = string(f->Text); break;
    case SID_TYPE: case OCTET_TYPE:
        f->Attr.ValueType = mode == SID_TYPE ? 5 : 16;
        f->Raw[0] = 1; f->Raw[1] = 1; f->Raw[7] = 5; f->Raw[8] = 21;
        f->Value.Octet.Value = f->Raw; f->Value.Octet.Bytes = 12; break;
    case BOOLEAN_TYPE:
        f->Attr.ValueType = 6; f->Value.Number = 1; break;
    case SIGNED_TYPE:
        f->Attr.ValueType = 1; f->Value.Number = 0xffffffffffffffffULL; break;
    case ZERO_VALUES:
        f->Attr.ValueType = 3; f->Attr.ValueCount = 0;
        f->Attr.Values.Pointer = NULL; break;
    case BAD_POINTER:
        f->Attr.ValueType = 3;
        f->Value.String = string(f->Text); f->Value.String.Buffer = (PWSTR)(ULONG_PTR)1; break;
    default: return (NTSTATUS)0xc000000d;
    }
    return 0;
}

static void fixture_tests(void)
{
    union { ULONG64 align; BYTE data[4096]; } output;
    WCHAR text[] = L"fixture.attribute", absent[] = L"Fixture.Missing";
    UNICODE_STRING names[2] = {string(text), string(absent)};
    enum MODE m;
    ULONG required, n;
    NTSTATUS status;
    for (m = EMPTY; m <= RACING_SIZE; ++m) {
        mode = m; type_calls = info_calls = 0;
        memset(output.data, 0xcc, sizeof(output.data)); required = 0xcccccccc;
        status = XpcQueryAttributesFromInformation(fixture_info, (HANDLE)(ULONG_PTR)1,
            NULL, 0, output.data, sizeof(output.data), &required);
        if (m == UNSUPPORTED || m == BAD_VERSION || m == BAD_POINTER || m == UNVERIFIED_EMPTY) {
            check(status == (NTSTATUS)0xc00000bb && required == 0xcccccccc &&
                  unchanged(output.data, sizeof(output.data)), "unsupported/malformed is not empty success");
        } else if (m == RACING_SIZE) {
            check(status == (NTSTATUS)0xc0000023 && info_calls == 4 &&
                  unchanged(output.data, sizeof(output.data)), "bounded class39 size race");
        } else if (m == EMPTY) {
            XPC_ATTRIBUTES *a = (XPC_ATTRIBUTES *)output.data;
            check(!status && required == sizeof(*a) && a->Version == 1 &&
                  !a->AttributeCount && !a->AttributeV1 &&
                  unchanged(output.data + required, sizeof(output.data) - required),
                  "empty result only from successful class39 store");
            required = 0xcccccccc; memset(output.data, 0xcc, sizeof(output.data));
            status = XpcQueryAttributesFromInformation(fixture_info, (HANDLE)(ULONG_PTR)1,
                names, 1, output.data, sizeof(output.data), &required);
            check(status == (NTSTATUS)0xc0000225 && !required &&
                  unchanged(output.data, sizeof(output.data)), "empty store package filter missing");
        } else {
            XPC_ATTRIBUTES *a = (XPC_ATTRIBUTES *)output.data;
            XPC_ATTRIBUTE *at = a->AttributeV1;
            check(!status && a->Version == 1 && a->AttributeCount == 1 &&
                  at->Flags == 2 && at->ValueCount == (m == ZERO_VALUES ? 0u : 1u) &&
                  unchanged(output.data + required, sizeof(output.data) - required),
                  "typed native marshalling and guard");
            if (m == STRING_TYPE) check(!wcscmp(at->Values.String[0].Buffer, L"Fixture.Value"),
                                    "STRING payload");
            if (m == NUMBER) check(at->Values.Uint64[0] == 0x1122334455667788ULL, "UINT64 payload");
            if (m == SIGNED_TYPE) check(at->Values.Uint64[0] == 0xffffffffffffffffULL, "INT64 payload");
            if (m == ZERO_VALUES) check(!at->Values.Pointer, "zero values have no fabricated payload");
            if (m == FQBN_TYPE) {
                FQBN *v = at->Values.Pointer;
                check(v->Version == 42 && !wcscmp(v->Name.Buffer, L"Fixture.Value"), "FQBN payload");
            }
            if (m == BOOLEAN_TYPE) check(at->Values.Uint64[0] == 1, "BOOLEAN payload");
            if (m == SID_TYPE || m == OCTET_TYPE) {
                OCTET *v = at->Values.Pointer;
                check(v->Bytes == 12 && ((BYTE *)v->Value)[8] == 21, "SID/OCTET payload");
            }
            n = required; memset(output.data, 0xcc, sizeof(output.data));
            status = XpcQueryAttributesFromInformation(fixture_info, (HANDLE)(ULONG_PTR)1,
                NULL, 0, output.data, n - 1, &required);
            check(status == (NTSTATUS)0xc0000023 && required == n &&
                  unchanged(output.data, sizeof(output.data)), "short buffer untouched");
            status = XpcQueryAttributesFromInformation(fixture_info, (HANDLE)(ULONG_PTR)1,
                names, 1, output.data, sizeof(output.data), &required);
            check(!status && ((XPC_ATTRIBUTES *)output.data)->AttributeCount == 1,
                  "case-insensitive actual RtlEqualUnicodeString filter");
            memset(output.data, 0xcc, sizeof(output.data));
            status = XpcQueryAttributesFromInformation(fixture_info, (HANDLE)(ULONG_PTR)1,
                names, 2, output.data, sizeof(output.data), &required);
            check(status == (NTSTATUS)0xc0000225 && !required &&
                  unchanged(output.data, sizeof(output.data)), "missing filter no partial output");
        }
        check(type_calls && info_calls, "class39 queried only behind TokenType gate");
    }
    mode = UNSUPPORTED; info_calls = 0;
    memset(output.data, 0xcc, sizeof(output.data)); required = 0xcccccccc;
    status = XpcQueryAttributesFromInformation(fixture_info, (HANDLE)(ULONG_PTR)0x1234,
        NULL, 0, output.data, sizeof(output.data), &required);
    check(status == (NTSTATUS)0xc0000008 && !info_calls && required == 0xcccccccc &&
          unchanged(output.data, sizeof(output.data)), "invalid handle before unsupported class39");
    status = XpcQueryAttributesFromInformation(fixture_info, (HANDLE)(ULONG_PTR)3,
        NULL, 0, output.data, sizeof(output.data), &required);
    check(status == (NTSTATUS)0xc0000022 && !info_calls && required == 0xcccccccc,
          "TOKEN_QUERY denial before unsupported class39");
    status = XpcQueryAttributesFromInformation(NULL, NULL, NULL, 0,
        output.data, sizeof(output.data), &required);
    check(status == (NTSTATUS)0xc00000bb && required == 0xcccccccc, "missing information provider");
}

static void real_filter(XPC_QUERY_ATTRIBUTES native, XPC_QUERY_ATTRIBUTES provider,
                        HANDLE token, const WCHAR *text)
{
    union { ULONG64 align; BYTE data[8192]; } a, b;
    UNICODE_STRING name = string((PWSTR)text);
    ULONG an = 0, bn = 0, i, j;
    NTSTATUS sa, sb;
    memset(a.data, 0xcc, sizeof(a.data)); memset(b.data, 0xcc, sizeof(b.data));
    sa = native(token, &name, 1, a.data, sizeof(a.data), &an);
    sb = provider(token, &name, 1, b.data, sizeof(b.data), &bn);
    ++real_tests;
    check(sa == sb, "real public-package filter status");
    if (sa == (NTSTATUS)0xc0000225) {
        check(!an && !bn && unchanged(b.data, sizeof(b.data)), "real absent package output untouched");
    } else if (!sa && !sb) {
        XPC_ATTRIBUTES *x = (XPC_ATTRIBUTES *)a.data, *y = (XPC_ATTRIBUTES *)b.data;
        check(x->Version == y->Version && x->AttributeCount == y->AttributeCount,
              "real attribute header");
        for (i = 0; i < x->AttributeCount && i < y->AttributeCount; ++i) {
            XPC_ATTRIBUTE *p = &x->AttributeV1[i], *q = &y->AttributeV1[i];
            check(p->Name.Length == q->Name.Length &&
                  !memcmp(p->Name.Buffer, q->Name.Buffer, p->Name.Length) &&
                  p->ValueType == q->ValueType && p->Flags == q->Flags &&
                  p->ValueCount == q->ValueCount, "real attribute metadata");
            if (p->ValueType == 3 && q->ValueType == 3)
                for (j = 0; j < p->ValueCount && j < q->ValueCount; ++j)
                    check(p->Values.String[j].Length == q->Values.String[j].Length &&
                          !memcmp(p->Values.String[j].Buffer, q->Values.String[j].Buffer,
                                  p->Values.String[j].Length), "real public string value");
            if (p->ValueType == 2 && q->ValueType == 2)
                check(!memcmp(p->Values.Uint64, q->Values.Uint64, p->ValueCount * 8),
                      "real public packed integer");
        }
        memset(b.data, 0xcc, sizeof(b.data));
        sb = provider(token, &name, 1, b.data, bn - 1, &bn);
        check(sb == (NTSTATUS)0xc0000023 && unchanged(b.data, sizeof(b.data)),
              "real provider short buffer");
    }
}

int main(int argc, char **argv)
{
    HMODULE dll;
    XPC_QUERY_ATTRIBUTES native, provider;
    HANDLE own, xbox, process;
    WCHAR path[2048], *filename;
    DWORD size = sizeof(path) / sizeof(path[0]), pid;
    static const WCHAR prefix[] = L"C:\\Program Files\\WindowsApps\\Microsoft.GamingApp_";
    fixture_tests();
    if (argc != 3) { puts("Usage: test.exe <combined DLL absolute path> <Xbox PID>"); return 2; }
    dll = LoadLibraryA(argv[1]);
    if (!dll) { printf("DLL_LOAD_ERROR=%lu\n", GetLastError()); return 2; }
    provider = (XPC_QUERY_ATTRIBUTES)(ULONG_PTR)GetProcAddress(dll, "NtQuerySecurityAttributesToken");
    native = (XPC_QUERY_ATTRIBUTES)(ULONG_PTR)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),
                                                           "NtQuerySecurityAttributesToken");
    if (!provider || !native || !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &own)) return 2;
    real_filter(native, provider, own, L"WIN://SYSAPPID");
    pid = strtoul(argv[2], NULL, 10);
    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process || !QueryFullProcessImageNameW(process, 0, path, &size)) return 2;
    filename = wcsrchr(path, L'\\');
    if (_wcsnicmp(path, prefix, wcslen(prefix)) || !filename ||
        (_wcsicmp(filename, L"\\XboxPcApp.exe") && _wcsicmp(filename, L"\\XboxPcAppCE.exe"))) return 2;
    if (!OpenProcessToken(process, TOKEN_QUERY, &xbox)) return 2;
    CloseHandle(process);
    real_filter(native, provider, xbox, L"WIN://SYSAPPID");
    real_filter(native, provider, xbox, L"WIN://PKG");
    real_filter(native, provider, xbox, L"WIN://PKGHOSTID");
    real_filter(native, provider, xbox, L"Fixture.Missing");
    CloseHandle(xbox); CloseHandle(own); FreeLibrary(dll);
    printf("RESULT ABI=%u assertions=%u real_filters=%u failures=%u\n",
           (unsigned)(sizeof(void *) * 8), tests, real_tests, failures);
    return failures ? 1 : 0;
}
