/* Paired probe: how genuine twinapi.appcore builds LaunchActivatedEventArgs.
 *
 * The LaunchActivatedEventArgs factory's ActivateInstance is E_NOTIMPL by
 * design; the activation pipeline materialises arguments through the cloaked
 * IValueUnmarshalByPropertySet::UnmarshalObjectFromPropertySet(IPropertySet*).
 * This probe hands that method a logging IMap<HSTRING, IInspectable> whose
 * values are genuine Windows.Foundation.PropertyValue objects, and records
 * every key the native code asks for, so the identical binary can be run on
 * Windows and under Wine.
 *
 * usage: args-propset-probe.exe [key=type:value ...]
 *   type: s string, b boolean (0/1), i int32, u uint32, l int64, q uint64,
 *         g guid ({...}), n present with a null value; an empty value after
 *         "s:" is an empty HSTRING. */
#define COBJMACROS
#include "launch-args-propset.h"

typedef HRESULT (WINAPI *fn_get_int)(void *, INT32 *);
typedef HRESULT (WINAPI *fn_get_hstring)(void *, HSTRING *);

int main(int argc, char **argv)
{
    static const WCHAR args_class[] = L"Windows.ApplicationModel.Activation.LaunchActivatedEventArgs";
    static struct map m = { propset_vtbl, map_vtbl, 1 };
    HSTRING s = NULL;
    IUnknown *result = NULL;
    void **statics = NULL, **iface = NULL;
    INT32 kind;
    HRESULT hr;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    hr = RoInitialize(RO_INIT_MULTITHREADED);
    printf("RoInitialize hr=0x%08lx\n", hr);
    hr = get_property_value_statics((void **)&statics);
    printf("PropertyValue statics hr=0x%08lx\n", hr);
    if (FAILED(hr)) return 2;
    for (i = 1; i < argc; ++i)
    {
        hr = add_value(&m, statics, argv[i]);
        printf("value %s hr=0x%08lx\n", argv[i], hr);
        if (FAILED(hr)) return 2;
    }

    hr = unmarshal_args_from_map(args_class, &m, &result);
    if (FAILED(hr)) return 5;

    if (SUCCEEDED(IUnknown_QueryInterface(result, &IID_IActivatedEventArgs_, (void **)&iface)))
    {
        hr = ((fn_get_int)(*(void ***)iface)[6])(iface, &kind);
        printf("IActivatedEventArgs.Kind hr=0x%08lx kind=%d\n", hr, kind);
        hr = ((fn_get_int)(*(void ***)iface)[7])(iface, &kind);
        printf("IActivatedEventArgs.PreviousExecutionState hr=0x%08lx state=%d\n", hr, kind);
        IUnknown_Release((IUnknown *)iface);
    }
    if (SUCCEEDED(IUnknown_QueryInterface(result, &IID_ILaunchActivatedEventArgs_, (void **)&iface)))
    {
        hr = ((fn_get_hstring)(*(void ***)iface)[6])(iface, &s);
        printf("ILaunchActivatedEventArgs.Arguments hr=0x%08lx \"%ls\"\n", hr, WindowsGetStringRawBuffer(s, NULL));
        WindowsDeleteString(s); s = NULL;
        hr = ((fn_get_hstring)(*(void ***)iface)[7])(iface, &s);
        printf("ILaunchActivatedEventArgs.TileId hr=0x%08lx \"%ls\"\n", hr, WindowsGetStringRawBuffer(s, NULL));
        WindowsDeleteString(s);
        IUnknown_Release((IUnknown *)iface);
    }
    IUnknown_Release(result);
    printf("done\n");
    return 0;
}