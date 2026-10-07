#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

typedef struct { COLORREF start, accent; } PREF;
typedef DWORD (WINAPI *fn_col)(const PREF *, UINT, BOOL, UINT);
typedef HRESULT (WINAPI *fn_get)(PREF *, BOOL);

int main(int argc, char **argv)
{
    static const UINT types[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0x20, 0xd1, 0xd2, 0xd3, 0x200, 0x7fffffff };
    PREF prefs[] = {
        { 0, 0 }, { 0xff9e5a00, 0xffd77800 }, { 0xff000000, 0xff3366cc }, { 0xff112233, 0xff445566 },
        { 0x00ffffff, 0x00ffffff }, { 0xff00ff00, 0xff0000ff }, { 0x80123456, 0x40abcdef },
    };
    HMODULE h = LoadLibraryA("uxtheme.dll");
    fn_col pc = (fn_col)GetProcAddress(h, "GetColorFromPreference");
    fn_get pg = (fn_get)GetProcAddress(h, "GetUserColorPreference");
    int mode = argc > 1 ? atoi(argv[1]) : 0;
    unsigned i, j;
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("byname=%d apiset=%d\n", pc != NULL,
           GetProcAddress(LoadLibraryA("ext-ms-win-uxtheme-themes-l1-1-0.dll"), "GetColorFromPreference") != NULL);
    if (!pc) return 1;
    if (mode == 1) { printf("null=%08lx\n", pc(NULL, 1, FALSE, 1)); return 0; }
    if (mode == 2)
    {
        typedef const WCHAR ** (WINAPI *fn_name)(UINT);
        fn_name pn = (fn_name)GetProcAddress(h, MAKEINTRESOURCEA(100));
        UINT n = 0;
        while (pn && pn(n)) n++;
        printf("names=%u\n", n);
        for (i = 0; i < n && pn; i++)
            if (i <= 0x10 || i == 0x20 || (i >= 0xd0 && i <= 0xd4)) printf("%x %ls\n", i, *pn(i));
        return 0;
    }
    if (mode == 4)
    {
        static const BYTE pal[32] = { 1,2,3,4, 0x11,0x12,0x13,0x14, 0x21,0x22,0x23,0x24, 0x31,0x32,0x33,0x34,
                                      0x41,0x42,0x43,0x44, 0x51,0x52,0x53,0x54, 0x61,0x62,0x63,0x64, 0x71,0x72,0x73,0x74 };
        PREF p;
        pg(&p, FALSE);
        printf("before=%08lx\n", pc(&p, 1, FALSE, 1));
        RegSetKeyValueA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent",
                        "AccentPalette", REG_BINARY, pal, sizeof(pal));
        printf("after-write=%08lx\n", pc(&p, 1, FALSE, 1));
        pg(&p, TRUE);
        printf("after-force-gucp=%08lx\n", pc(&p, 1, FALSE, 1));
        printf("mode0=%08lx mode2=%08lx\n", pc(&p, 1, FALSE, 0), pc(&p, 1, FALSE, 2));
        return 0;
    }
    if (mode == 5)
    {
        PREF p = { strtoul(argv[2], NULL, 16), strtoul(argv[3], NULL, 16) };
        for (i = 0; i < 1300; i++) printf("%x %08lx\n", i, pc(&p, i, FALSE, 1));
        return 0;
    }
    if (mode == 3)
    {
        PREF p;
        pg(&p, FALSE);
        printf("pref %08lx/%08lx:", p.start, p.accent);
        for (i = 0; i <= 0xa; i++) printf(" %x=%08lx", i, pc(&p, i, FALSE, 1));
        printf(" 20=%08lx d2=%08lx\n", pc(&p, 0x20, FALSE, 1), pc(&p, 0xd2, FALSE, 1));
        return 0;
    }
    pg(&prefs[0], FALSE);
    printf("real start=%08lx accent=%08lx\n", prefs[0].start, prefs[0].accent);
    for (j = 0; j < ARRAY_SIZE(prefs); j++)
    {
        printf("pref %08lx/%08lx:", prefs[j].start, prefs[j].accent);
        for (i = 0; i < ARRAY_SIZE(types); i++)
        {
            SetLastError(0xdead);
            printf(" %lx=%08lx", (unsigned long)types[i], pc(&prefs[j], types[i], FALSE, 1));
            if (GetLastError() != 0xdead) printf("(gle%lu)", GetLastError());
        }
        printf("\n");
    }
    for (j = 0; j < 2; j++)
        for (i = 0; i < 4; i++)
            printf("real hc=%u mode=%u: 1=%08lx 4=%08lx 7=%08lx d2=%08lx\n", j, i,
                   pc(&prefs[0], 1, j, i), pc(&prefs[0], 4, j, i), pc(&prefs[0], 7, j, i), pc(&prefs[0], 0xd2, j, i));
    return 0;
}
