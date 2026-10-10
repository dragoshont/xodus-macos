#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <inspectable.h>
#include <stdio.h>

DEFINE_GUID(display_statics_iid, 0xc6a02a6c, 0xd452, 0x44dc,
            0xba, 0x07, 0x96, 0xf3, 0xc6, 0xad, 0xf9, 0xd1);
DEFINE_GUID(display_info_iid, 0xbed112ae, 0xadc3, 0x4dc9,
            0xae, 0x65, 0x85, 0x1f, 0x4d, 0x7d, 0x47, 0x99);

typedef struct display_statics display_statics;
typedef struct display_info display_info;

typedef struct display_statics_vtable
{
    HRESULT (WINAPI *QueryInterface)(display_statics *, REFIID, void **);
    ULONG (WINAPI *AddRef)(display_statics *);
    ULONG (WINAPI *Release)(display_statics *);
    HRESULT (WINAPI *GetIids)(display_statics *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(display_statics *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(display_statics *, TrustLevel *);
    HRESULT (WINAPI *GetForCurrentView)(display_statics *, IInspectable **);
} display_statics_vtable;

struct display_statics { const display_statics_vtable *lpVtbl; };

typedef struct display_info_vtable
{
    HRESULT (WINAPI *QueryInterface)(display_info *, REFIID, void **);
    ULONG (WINAPI *AddRef)(display_info *);
    ULONG (WINAPI *Release)(display_info *);
    HRESULT (WINAPI *GetIids)(display_info *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(display_info *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(display_info *, TrustLevel *);
    HRESULT (WINAPI *get_CurrentOrientation)(display_info *, int *);
    HRESULT (WINAPI *get_NativeOrientation)(display_info *, int *);
    HRESULT (WINAPI *add_OrientationChanged)(display_info *, IUnknown *, INT64 *);
    HRESULT (WINAPI *remove_OrientationChanged)(display_info *, INT64);
    HRESULT (WINAPI *get_ResolutionScale)(display_info *, int *);
    HRESULT (WINAPI *get_LogicalDpi)(display_info *, float *);
    HRESULT (WINAPI *get_RawDpiX)(display_info *, float *);
    HRESULT (WINAPI *get_RawDpiY)(display_info *, float *);
} display_info_vtable;

struct display_info { const display_info_vtable *lpVtbl; };

int main(void)
{
    const WCHAR name[] = L"Windows.Graphics.Display.DisplayInformation";
    display_statics *statics = NULL;
    display_info *info = NULL;
    IInspectable *view = NULL;
    HSTRING string;
    HRESULT result = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(result)) return 2;
    result = WindowsCreateString(name, sizeof(name) / sizeof(name[0]) - 1, &string);
    if (SUCCEEDED(result))
    {
        result = RoGetActivationFactory(string, &display_statics_iid, (void **)&statics);
        printf("display: factory hr=%08lx object=%p\n", (unsigned long)result, (void *)statics);
        if (SUCCEEDED(result))
        {
            result = statics->lpVtbl->GetForCurrentView(statics, &view);
            printf("display: current-view hr=%08lx object=%p\n", (unsigned long)result, (void *)view);
            if (SUCCEEDED(result) && view)
            {
                result = IInspectable_QueryInterface(view, &display_info_iid, (void **)&info);
                if (SUCCEEDED(result))
                {
                    float dpi = 0;
                    int orientation = 0, scale = 0;
                    HRESULT dpi_result = info->lpVtbl->get_LogicalDpi(info, &dpi);
                    HRESULT orientation_result = info->lpVtbl->get_CurrentOrientation(info, &orientation);
                    HRESULT scale_result = info->lpVtbl->get_ResolutionScale(info, &scale);
                    printf("display: dpi hr=%08lx value=%g orientation hr=%08lx value=%d scale hr=%08lx value=%d\n",
                           (unsigned long)dpi_result, dpi, (unsigned long)orientation_result,
                           orientation, (unsigned long)scale_result, scale);
                    result = FAILED(dpi_result) ? dpi_result :
                             FAILED(orientation_result) ? orientation_result : scale_result;
                    info->lpVtbl->Release(info);
                }
                IInspectable_Release(view);
            }
            statics->lpVtbl->Release(statics);
        }
        WindowsDeleteString(string);
    }
    RoUninitialize();
    return FAILED(result) ? 1 : 0;
}
