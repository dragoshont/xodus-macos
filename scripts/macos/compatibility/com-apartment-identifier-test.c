#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <roapi.h>
#include <stdio.h>

typedef HRESULT (WINAPI *APARTMENT_ID)(UINT64 *);

int main(void)
{
    HMODULE module = LoadLibraryW(L"combase.dll");
    APARTMENT_ID query = module ?
        (APARTMENT_ID)(ULONG_PTR)GetProcAddress(module, "RoGetApartmentIdentifier") : NULL;
    UINT64 first = 0, again = 0, sta = 0;
    HRESULT hr;
    if (!query) return 2;
    hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(hr)) return 3;
    hr = query(&first);
    if (FAILED(hr) || FAILED(query(&again)) || !first || first != again || first == 0xdeadbeef)
        return 4;
    RoUninitialize();
    hr = RoInitialize(RO_INIT_SINGLETHREADED);
    if (FAILED(hr)) return 5;
    hr = query(&sta);
    RoUninitialize();
    printf("REAL_APARTMENT_IDENTIFIER mta=%llx repeated=%llx sta=%llx distinct=%d\n",
           (unsigned long long)first, (unsigned long long)again,
           (unsigned long long)sta, sta != first);
    FreeLibrary(module);
    return SUCCEEDED(hr) && sta && sta != first && sta != 0xdeadbeef ? 0 : 6;
}
