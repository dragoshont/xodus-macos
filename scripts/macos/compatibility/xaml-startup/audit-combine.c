#define COBJMACROS
#include <windows.h>
#include <urlmon.h>
#include <stdio.h>
#include <shlwapi.h>

typedef HRESULT (WINAPI *fn_comb)(IUri *, IUri *, DWORD, IUri **, DWORD_PTR);
static fn_comb pc;

static void one(const WCHAR *b, const WCHAR *r, DWORD flags, DWORD_PTR extra)
{
    IUri *bu = NULL, *ru = NULL, *out = (IUri *)0x1;
    BSTR s = NULL;
    HRESULT hr;
    CreateUri(b, Uri_CREATE_ALLOW_RELATIVE, 0, &bu);
    CreateUri(r, Uri_CREATE_ALLOW_RELATIVE, 0, &ru);
    hr = pc(bu, ru, flags, &out, extra);
    if (SUCCEEDED(hr) && out) { IUri_GetAbsoluteUri(out, &s); if (!s) IUri_GetDisplayUri(out, &s); }
    printf("%ls + %ls f=%lx x=%lx -> %08lx %ls\n", b, r, flags, (DWORD)extra, hr, s ? s : L"(null)");
    if (s) SysFreeString(s);
    if (SUCCEEDED(hr) && out) IUri_Release(out);
    if (bu) IUri_Release(bu);
    if (ru) IUri_Release(ru);
}

int main(int argc, char **argv)
{
    IUri *u = NULL, *out = (IUri *)0x1;
    HRESULT hr;
    int k = argc > 1 ? atoi(argv[1]) : -1;
    HMODULE h;
    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    h = LoadLibraryA("iertutil.dll");
    pc = (fn_comb)GetProcAddress(h, "PrivateCoInternetCombineIUri");
    if (argc > 2 && argv[2][0] == 'u') pc = (fn_comb)GetProcAddress(LoadLibraryA("urlmon.dll"), "CoInternetCombineIUri");
    if (k < 0) printf("byname=%d\n", pc != NULL);
    if (!pc) return 1;
    if (argc > 2 && argv[2][0] == 'b')
    {
        fn_comb priv = pc;
        pc = (fn_comb)GetProcAddress(LoadLibraryA("urlmon.dll"), "CoInternetCombineIUri");
        one(L"http://www.example.com/a/b/c", L"d", 0, 0);
        pc = priv;
    }
    switch (k)
    {
    case 0: printf("nullout=%08lx\n", pc(NULL, NULL, 0, NULL, 0)); break;
    case 1: hr = pc(NULL, NULL, 0, &out, 0); printf("nullargs=%08lx out=%p\n", hr, out); break;
    case 2: one(L"ms-appx:///Views/Main.xaml", L"Assets/logo.png", 0, 0); break;
    case 3: one(L"ms-appx:///Views/Main.xaml", L"/Assets/logo.png", 0, 0); break;
    case 4: one(L"ms-appx:///Views/Main.xaml", L"ms-appx-web:///x.html", 0, 0); break;
    case 5: one(L"http://www.example.com/a/b/c?q=1#f", L"../d/./e?z", 0, 0); break;
    case 6: one(L"http://www.example.com/a/b/c", L"../d/./e", URL_DONT_SIMPLIFY, 0); break;
    case 7: one(L"http://www.example.com/a/b/c", L"http://other.example/%7Ex/../y", URL_DONT_SIMPLIFY, 0); break;
    case 8: one(L"http://www.example.com/a/b/c", L"http://other.example/%7Ex/../y", URL_DONT_SIMPLIFY, 1); break;
    case 9: one(L"file:///C:/dir/file.txt", L"/other.txt", 0, 0); break;
    case 10: one(L"res://x.dll/a/b", L"c", 0, 0); break;
    case 11: one(L"mk:@MSITStore:C:\\a.chm::/b/c.htm", L"/d.htm", 0, 0); break;
    case 12: CreateUri(L"http://www.example.com/", 0, 0, &u); hr = pc(u, NULL, 0, &out, 0); printf("nullrel=%08lx out=%p\n", hr, out); break;
    case 13: one(L"http://www.example.com/a/b/c", L"d", 0, 0); break;
    }
    return 0;
}