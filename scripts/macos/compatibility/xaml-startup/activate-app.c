/* Activator experiment: call the original app's published Microsoft.Xbox.AppL
 * factory through IActivatableApplication::Activate, the documented-by-PDB
 * contract twinapi.appcore exposes for an external activator.
 *
 * usage: activate-app.exe <package-full-name> <app-id> <class> <a1> <a2> <a3> <a4>
 *        [aam-id] [options] [wait-seconds]
 * The Xodus combase publishes factories in the ROT under
 * "!Xodus.WinRT.Factory.v1|pkg|app|class"; this helper looks them up directly.
 * Arguments are passed as given ("-" means an empty HSTRING). The activated
 * event args are a genuine twinapi.appcore LaunchActivatedEventArgs built the
 * measured native way: UnmarshalObjectFromPropertySet over the property set
 * recorded by args-propset-probe.c (activation-20261007/launch-args-*.txt). */
#define COBJMACROS
#include "launch-args-propset.h"

static const GUID IID_IActivatableApplication =
    {0x92696c00, 0x7578, 0x48e1, {0xac, 0x1a, 0x2c, 0xa9, 0x09, 0xe2, 0xc8, 0xcf}};
static const GUID IID_IActivationFactory_ =
    {0x00000035, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

typedef struct activatable activatable;
typedef struct
{
    HRESULT (WINAPI *QueryInterface)(activatable *, REFIID, void **);
    ULONG (WINAPI *AddRef)(activatable *);
    ULONG (WINAPI *Release)(activatable *);
    HRESULT (WINAPI *GetIids)(activatable *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(activatable *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(activatable *, int *);
    HRESULT (WINAPI *Activate)(activatable *, HSTRING, HSTRING, HSTRING, HSTRING, IUnknown *, UINT64, UINT32);
} activatable_vtbl;
struct activatable { const activatable_vtbl *lpVtbl; };

static HSTRING make_string(const char *text)
{
    WCHAR buffer[512];
    HSTRING out = NULL;
    HRESULT hr;
    if (!strcmp(text, "-")) return NULL;
    if (!MultiByteToWideChar(CP_UTF8, 0, text, -1, buffer, ARRAYSIZE(buffer)))
    {
        printf("make_string: cannot convert \"%s\" (error %lu); passing NULL\n", text, GetLastError());
        return NULL;
    }
    if (FAILED(hr = WindowsCreateString(buffer, lstrlenW(buffer), &out)))
    {
        printf("make_string: WindowsCreateString -> %#lx; passing NULL\n", hr);
        return NULL;
    }
    return out;
}

static HRESULT make_launch_args(const char *tile_id, const char *arguments, UINT64 aam, IUnknown **out)
{
    static const WCHAR name[] = L"Windows.ApplicationModel.Activation.LaunchActivatedEventArgs";
    static struct map m = { propset_vtbl, map_vtbl, 1 };
    char spec[640], activity[64];
    WCHAR activity_w[40];
    void **statics = NULL;
    GUID activity_id;
    HRESULT hr;
    int i;
    const char *fixed[] =
    {
        "ActivationKind=i:0", "PreviousExecutionState=i:0", "UserContext=q:0",
        "IsForeground=b:1", "IsHolographic=b:0", "IsInitialized=b:0",
        "CurrentlyShownApplicationViewId=i:0", "IsApplicationMultiviewActivationPolicyEnabled=b:0",
        "PrelaunchActivated=b:0",
    };

    *out = NULL;
    hr = get_property_value_statics((void **)&statics);
    printf("launch-args PropertyValue statics hr=0x%08lx\n", hr);
    if (FAILED(hr)) return hr;
    for (i = 0; SUCCEEDED(hr) && i < ARRAYSIZE(fixed); ++i) hr = add_value(&m, statics, fixed[i]);
    if (SUCCEEDED(hr)) hr = CoCreateGuid(&activity_id);
    if (SUCCEEDED(hr))
    {
        StringFromGUID2(&activity_id, activity_w, ARRAYSIZE(activity_w));
        WideCharToMultiByte(CP_UTF8, 0, activity_w, -1, activity, sizeof(activity), NULL, NULL);
        snprintf(spec, sizeof(spec), "AamActivityId=g:%s", activity);
        hr = add_value(&m, statics, spec);
    }
    if (SUCCEEDED(hr)) { snprintf(spec, sizeof(spec), "AamActivationId=q:%llu", aam); hr = add_value(&m, statics, spec); }
    if (SUCCEEDED(hr)) { snprintf(spec, sizeof(spec), "MultiView:AamActivationId=q:%llu", aam); hr = add_value(&m, statics, spec); }
    if (SUCCEEDED(hr)) { snprintf(spec, sizeof(spec), "Arguments=s:%s", strcmp(arguments, "-") ? arguments : ""); hr = add_value(&m, statics, spec); }
    if (SUCCEEDED(hr)) { snprintf(spec, sizeof(spec), "TileId=s:%s", tile_id); hr = add_value(&m, statics, spec); }
    printf("launch-args property set (%u entries) hr=0x%08lx\n", m.count, hr);
    if (SUCCEEDED(hr)) hr = unmarshal_args_from_map(name, &m, out);
    IUnknown_Release((IUnknown *)statics);
    return hr;
}
int main(int argc, char **argv)
{
    WCHAR item[1024];
    IRunningObjectTable *rot = NULL;
    IMoniker *moniker = NULL;
    IUnknown *object = NULL, *args = NULL;
    IClassFactory *factory = NULL;
    activatable *app = NULL;
    HSTRING s[4];
    UINT64 aam;
    UINT32 options;
    DWORD wait, i;
    HRESULT hr;

    if (argc < 8)
    {
        fprintf(stderr, "usage: %s pkg app class a1 a2 a3 a4 [aam] [options] [wait]\n", argv[0]);
        return 2;
    }
    aam = argc > 8 ? _strtoui64(argv[8], NULL, 0) : 0;
    options = argc > 9 ? strtoul(argv[9], NULL, 0) : 0;
    wait = argc > 10 ? strtoul(argv[10], NULL, 0) : 60;
    setvbuf(stdout, NULL, _IONBF, 0);

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    printf("CoInitializeEx hr=0x%08lx\n", hr);
    swprintf(item, ARRAYSIZE(item), L"Xodus.WinRT.Factory.v1|%hs|%hs|%hs", argv[1], argv[2], argv[3]);
    hr = GetRunningObjectTable(0, &rot);
    if (SUCCEEDED(hr)) hr = CreateItemMoniker(L"!", item, &moniker);
    printf("ROT/moniker hr=0x%08lx item=%ls\n", hr, item);
    for (i = 0; SUCCEEDED(hr) && i < wait * 4; ++i)
    {
        if (SUCCEEDED(IRunningObjectTable_GetObject(rot, moniker, &object))) break;
        Sleep(250);
    }
    if (!object) { printf("factory not published within %lu s\n", wait); return 3; }
    printf("factory published after %lu ms\n", i * 250);
    hr = IUnknown_QueryInterface(object, &IID_IClassFactory, (void **)&factory);
    printf("QI IClassFactory hr=0x%08lx\n", hr);
    if (SUCCEEDED(hr)) hr = IClassFactory_CreateInstance(factory, NULL, &IID_IActivatableApplication, (void **)&app);
    printf("CreateInstance IActivatableApplication hr=0x%08lx\n", hr);
    if (FAILED(hr)) return 4;

    {
        IUnknown *object_args = NULL;
        hr = make_launch_args(argv[2], argv[7], aam, &object_args);
        if (SUCCEEDED(hr)) hr = IUnknown_QueryInterface(object_args, &IID_IActivatedEventArgs_, (void **)&args);
        printf("launch-args QI IActivatedEventArgs hr=0x%08lx\n", hr);
        if (object_args) IUnknown_Release(object_args);
        if (FAILED(hr)) return 6;
    }
    for (i = 0; i < 4; ++i) s[i] = make_string(argv[4 + i]);
    printf("Activate(%s, %s, %s, %s, args=%p, aam=%llu, options=%#x) ...\n",
           argv[4], argv[5], argv[6], argv[7], args, aam, options);
    hr = app->lpVtbl->Activate(app, s[0], s[1], s[2], s[3], args, aam, options);
    printf("Activate hr=0x%08lx\n", hr);

    Sleep(wait * 1000);
    for (i = 0; i < 4; ++i) WindowsDeleteString(s[i]);
    if (args) IUnknown_Release(args);
    app->lpVtbl->Release(app);
    IClassFactory_Release(factory);
    IUnknown_Release(object);
    IMoniker_Release(moniker);
    IRunningObjectTable_Release(rot);
    CoUninitialize();
    return SUCCEEDED(hr) ? 0 : 5;
}
