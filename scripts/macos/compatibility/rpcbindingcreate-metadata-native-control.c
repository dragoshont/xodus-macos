#define COBJMACROS
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <objbase.h>
#include <stdio.h>

typedef HRESULT (WINAPI *GET_REGISTRATION)(HSTRING, void **);
typedef HRESULT (WINAPI *GET_IIDS)(void *, ULONG *, IID **);
typedef HRESULT (WINAPI *GET_STRING)(void *, HSTRING *);
typedef HRESULT (WINAPI *GET_NUMBER)(void *, DWORD *);

int main(void)
{
    HMODULE module = LoadLibraryW(L"combase.dll");
    GET_REGISTRATION query = (GET_REGISTRATION)(ULONG_PTR)GetProcAddress(module, "RoGetActivatableClassRegistration");
    static const WCHAR *names[] = {L"Windows.Foundation.Collections.PropertySet", L"Windows.Foundation.Uri"};
    unsigned int i;
    if (!query || FAILED(RoInitialize(RO_INIT_MULTITHREADED))) return 2;
    for (i = 0; i < 2; ++i)
    {
        HSTRING name;
        void *object = NULL;
        HRESULT status;
        ULONG j, count = 0;
        IID *iids = NULL;
        void **table;
        if (FAILED(WindowsCreateString(names[i], lstrlenW(names[i]), &name))) return 2;
        status = query(name, &object);
        printf("NATIVE_METADATA class=%ls hr=%08lx object=%p\n", names[i], (unsigned long)status, object);
        if (SUCCEEDED(status) && object)
        {
            table = *(void ***)object;
            for (j = 0; j < 11; ++j)
                printf("OWNED_METADATA_VTABLE slot=%lu module_rva=%llx\n", j,
                       (unsigned long long)((BYTE *)table[j] - (BYTE *)module));
            status = ((GET_IIDS)table[3])(object, &count, &iids);
            printf("METADATA_GETIIDS hr=%08lx count=%lu\n", (unsigned long)status, count);
            for (j = 0; j < count; ++j)
            {
                WCHAR text[64];
                StringFromGUID2(&iids[j], text, 64);
                printf("NATIVE_METADATA_IID=%ls\n", text);
            }
            if (count)
            {
                void *same = NULL;
                status = IUnknown_QueryInterface((IUnknown *)object, &iids[0], &same);
                printf("BASE_METADATA_IID_QI hr=%08lx same_primary=%d\n", (unsigned long)status, same == object);
                if (same) IUnknown_Release((IUnknown *)same);
            }
            CoTaskMemFree(iids);
            {
                HSTRING id = NULL;
                DWORD number = 0;
                status = ((GET_STRING)table[6])(object, &id);
                printf("CLASS_ID hr=%08lx value=%ls\n", (unsigned long)status, WindowsGetStringRawBuffer(id, NULL));
                WindowsDeleteString(id);
                for (j = 7; j < 10; ++j)
                {
                    status = ((GET_NUMBER)table[j])(object, &number);
                    printf("METADATA_NUMBER slot=%lu hr=%08lx value=%lu\n", j, (unsigned long)status, number);
                }
            }
            IUnknown_Release((IUnknown *)object);
        }
        WindowsDeleteString(name);
    }
    RoUninitialize(); FreeLibrary(module);
    return 0;
}
