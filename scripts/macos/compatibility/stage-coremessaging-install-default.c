#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static BOOL verified_asset(const BYTE *data, DWORD length)
{
    static const BYTE expected[32] = {
        0x9b,0xd0,0x8f,0x9f,0x4b,0x73,0x61,0x43,0xbf,0xfd,0x91,0x01,0x7c,0x87,0xdf,0xfe,
        0xca,0xde,0x60,0x10,0xdc,0xfb,0x2b,0x06,0x52,0x71,0x69,0xbf,0x65,0x9a,0x3c,0xeb
    };
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    BYTE digest[32];
    DWORD bytes = sizeof(digest);
    BOOL result = FALSE;
    if (!CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) ||
        !CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) ||
        !CryptHashData(hash, data, length, 0) ||
        !CryptGetHashParam(hash, HP_HASHVAL, digest, &bytes, 0))
        fprintf(stderr, "Installation asset digest verification failed error=%lu\n", GetLastError());
    else result = bytes == sizeof(expected) && !memcmp(digest, expected, sizeof(expected));
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    return result;
}

int main(int argc, char **argv)
{
    const WCHAR *path =
        L"Software\\Microsoft\\SecurityManager\\TransientObjects\\"
        L"%5C%5C.%5CAlpcPort%5CWM_RegistrarServer";
    HKEY key = NULL;
    BYTE *data = NULL, *observed = NULL;
    DWORD disposition, length, type, existing;
    LONG status;
    FILE *input;
    long size;
    int result = 1;
    if (argc != 2 ||
        !GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtXodusQueryRegisteredPackage"))
    {
        fprintf(stderr, "Provisioning requires this isolated Xbox Wine runtime and one verified installation asset.\n");
        return 2;
    }
    input = fopen(argv[1], "rb");
    if (!input) { fprintf(stderr, "Installation asset open failed\n"); return 2; }
    if (fseek(input, 0, SEEK_END) || (size = ftell(input)) <= 0 || (ULONGLONG)size > MAXDWORD ||
        fseek(input, 0, SEEK_SET) || !(data = malloc(size)))
    {
        fprintf(stderr, "Installation asset sizing/allocation failed\n");
        fclose(input);
        return 2;
    }
    length = size;
    if (fread(data, 1, length, input) != length || !verified_asset(data, length) ||
        !IsValidSecurityDescriptor(data) ||
        GetSecurityDescriptorLength(data) != length)
    {
        fprintf(stderr, "Installation asset read/descriptor validation failed\n");
        goto done;
    }
    status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key);
    if (!status)
    {
        existing = 0;
        status = RegQueryValueExW(key, L"SecurityDescriptor", NULL, &type, NULL, &existing);
        RegCloseKey(key); key = NULL;
        if (status != ERROR_FILE_NOT_FOUND)
        {
            fprintf(stderr, "Refusing existing/unreadable registrar configuration, status=%ld\n", status);
            goto done;
        }
    }
    else if (status != ERROR_FILE_NOT_FOUND && status != ERROR_PATH_NOT_FOUND)
    {
        fprintf(stderr, "Registrar configuration lookup failed status=%ld\n", status);
        goto done;
    }
    status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, NULL, 0,
                            KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY,
                            NULL, &key, &disposition);
    if (status) { fprintf(stderr, "Registrar configuration creation failed status=%ld\n", status); goto done; }
    existing = 0;
    status = RegQueryValueExW(key, L"SecurityDescriptor", NULL, &type, NULL, &existing);
    if (status != ERROR_FILE_NOT_FOUND)
    {
        fprintf(stderr, "Registrar configuration changed before publication, status=%ld\n", status);
        goto done;
    }
    if (!(observed = malloc(length)))
    {
        fprintf(stderr, "Registrar configuration read-back allocation failed\n");
        goto done;
    }
    status = RegSetValueExW(key, L"SecurityDescriptor", 0, REG_BINARY, data, length);
    if (status) { fprintf(stderr, "Registrar configuration publication failed status=%ld\n", status); goto done; }
    existing = length;
    status = RegQueryValueExW(key, L"SecurityDescriptor", NULL, &type, observed, &existing);
    if (status || type != REG_BINARY || existing != length || memcmp(observed, data, length))
    {
        fprintf(stderr, "Registrar configuration read-back mismatch status=%ld\n", status);
        goto done;
    }
    printf("STAGED_MATCHING_COMPONENT_DEFAULT bytes=%lu new_key=%u no_live_policy_import=1\n",
           length, disposition == REG_CREATED_NEW_KEY);
    result = 0;
done:
    if (key) RegCloseKey(key);
    free(data);
    free(observed);
    fclose(input);
    return result;
}
