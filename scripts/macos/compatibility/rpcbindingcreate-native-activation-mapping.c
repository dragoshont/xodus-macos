#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static const WCHAR target[] = L"Microsoft.Xbox.AppL.AppXhg7mrtrf7ayze6gb4syw3zwjgyr2xv4r.mca";

static void inspect(HKEY key, const WCHAR *path, unsigned int depth)
{
    WCHAR name[512], childpath[2048];
    BYTE data[4096];
    DWORD index, length, bytes, type;
    LONG status;
    int match = wcsstr(path, target) != NULL || wcsstr(path, L"XboxPcApp.App") != NULL;
    if (depth > 32) return;
    for (index = 0; ; ++index)
    {
        length = 512;
        bytes = sizeof(data) - sizeof(WCHAR);
        status = RegEnumValueW(key, index, name, &length, NULL, &type, data, &bytes);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status) { printf("VALUE_ERROR=%ld\n", status); break; }
        data[bytes] = data[bytes + 1] = 0;
        if ((type == REG_SZ || type == REG_EXPAND_SZ) &&
            (match || wcsstr((WCHAR *)data, target) || wcsstr((WCHAR *)data, L"XboxPcApp.App")))
            printf("MAPPING_STRING path=%ls name=%ls value=%ls\n", path, name, (WCHAR *)data);
        else if (match && type == REG_DWORD && bytes == sizeof(DWORD))
            printf("MAPPING_DWORD path=%ls name=%ls value=%lu\n", path, name, *(DWORD *)data);
    }
    for (index = 0; ; ++index)
    {
        HKEY child;
        length = 512;
        status = RegEnumKeyExW(key, index, name, &length, NULL, NULL, NULL, NULL);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status) { printf("KEY_ERROR=%ld\n", status); break; }
        if (_snwprintf(childpath, 2048, L"%ls\\%ls", path, name) < 0) continue;
        if (match || wcsstr(name, target) || wcsstr(name, L"XboxPcApp.App"))
            printf("MAPPING_KEY=%ls\n", childpath);
        status = RegOpenKeyExW(key, name, 0, KEY_READ, &child);
        if (!status)
        {
            inspect(child, childpath, depth + 1);
            RegCloseKey(child);
        }
    }
}

int main(void)
{
    HKEY key;
    LONG status = RegLoadAppKeyW(
        L"C:\\ProgramData\\Microsoft\\Windows\\AppRepository\\Packages\\"
        L"Microsoft.GamingApp_2609.1001.16.0_x64__8wekyb3d8bbwe\\ActivationStore.dat",
        &key, KEY_READ, REG_PROCESS_APPKEY, 0);
    printf("READONLY_ACTIVATION_HIVE_OPEN=%ld\n", status);
    if (status) return 1;
    inspect(key, L"", 0);
    RegCloseKey(key);
    return 0;
}
