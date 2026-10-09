/* Private diagnostic seam only; this does not implement the native WinRT broker. */
struct rot_factory
{
    IClassFactory iface;
    LONG refs, revoked, calls;
    HSTRING class_name;
    PFNGETACTIVATIONFACTORY callback;
    HMODULE module;
};
static struct rot_factory *rot_factory_from(IClassFactory *iface)
{ return CONTAINING_RECORD(iface, struct rot_factory, iface); }
static ULONG WINAPI rot_factory_addref(IClassFactory *iface)
{ return InterlockedIncrement(&rot_factory_from(iface)->refs); }
static ULONG WINAPI rot_factory_release(IClassFactory *iface)
{
    struct rot_factory *object = rot_factory_from(iface);
    ULONG refs = InterlockedDecrement(&object->refs);
    if (!refs) { WindowsDeleteString(object->class_name); if (object->module) FreeLibrary(object->module); free(object); }
    return refs;
}
static HRESULT WINAPI rot_factory_query(IClassFactory *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IClassFactory)) return E_NOINTERFACE;
    *out = iface; rot_factory_addref(iface); return S_OK;
}
static HRESULT WINAPI rot_factory_create(IClassFactory *iface, IUnknown *outer, REFIID iid, void **out)
{
    struct rot_factory *object = rot_factory_from(iface);
    IActivationFactory *factory = NULL;
    APTTYPE apartment;
    APTTYPEQUALIFIER qualifier;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    if (InterlockedCompareExchange(&object->revoked, 0, 0)) return CO_E_OBJNOTCONNECTED;
    CoGetApartmentType(&apartment, &qualifier);
    InterlockedIncrement(&object->calls);
    hr = object->callback(object->class_name, &factory);
    ERR("ROT_REAL_CALLBACK pid=%lu tid=%lu apartment=%d hr=%#lx actual_factory=%p calls=%ld.\n",
        GetCurrentProcessId(), GetCurrentThreadId(), apartment, hr, factory, object->calls);
    if (SUCCEEDED(hr) && !factory) hr = E_UNEXPECTED;
    if (SUCCEEDED(hr)) hr = IActivationFactory_QueryInterface(factory, iid, out);
    ERR("ROT_ACTUAL_FACTORY_QI iid=%s hr=%#lx actual_interface=%p.\n", debugstr_guid(iid), hr, *out);
    if (factory)
    {
        HSTRING class_name = NULL;
        HRESULT name_hr = IActivationFactory_GetRuntimeClassName(factory, &class_name);
        ERR("ROT_LOCAL_RUNTIMECLASSNAME hr=%#lx value=%s.\n", name_hr, debugstr_hstring(class_name));
        WindowsDeleteString(class_name);
    }
    if (factory && GetEnvironmentVariableW(L"XODUS_NULL_STUB_LIFETIME_DIAGNOSTIC", NULL, 0))
    {
        CLSID proxy;
        IPSFactoryBuffer *ps = NULL;
        IRpcStubBuffer *stub = NULL;
        HRESULT stub_hr = CoGetPSClsid(&IID_IActivationFactory, &proxy);
        if (SUCCEEDED(stub_hr)) stub_hr = CoGetClassObject(&proxy, CLSCTX_INPROC_SERVER, NULL,
            &IID_IPSFactoryBuffer, (void **)&ps);
        if (SUCCEEDED(stub_hr)) stub_hr = IPSFactoryBuffer_CreateStub(ps, &IID_IActivationFactory, NULL, &stub);
        ERR("NULL_STUB_REAL_FACTORY_CREATE hr=%#lx stub=%p.\n", stub_hr, stub);
        if (SUCCEEDED(stub_hr) && stub)
        {
            HRESULT connect_hr = IRpcStubBuffer_Connect(stub, (IUnknown *)factory);
            ERR("NULL_STUB_REAL_FACTORY_CONNECT hr=%#lx supported=%p refs=%lu.\n", connect_hr,
                IRpcStubBuffer_IsIIDSupported(stub, &IID_IActivationFactory), IRpcStubBuffer_CountRefs(stub));
            IRpcStubBuffer_Disconnect(stub);
            IRpcStubBuffer_Disconnect(stub);
            ERR("NULL_STUB_REAL_FACTORY_RELEASE refs=%lu.\n", IRpcStubBuffer_Release(stub));
        }
        if (ps) IPSFactoryBuffer_Release(ps);
    }
    if (factory) IActivationFactory_Release(factory);
    return hr;
}
static HRESULT WINAPI rot_factory_lock(IClassFactory *iface, BOOL lock)
{
    return E_NOTIMPL;
}
static const IClassFactoryVtbl rot_factory_vtbl =
{ rot_factory_query, rot_factory_addref, rot_factory_release, rot_factory_create, rot_factory_lock };

static void rot_factory_diagnostic(HSTRING class_name, PFNGETACTIVATIONFACTORY callback)
{
    WCHAR package[256], application[256], item[1024];
    IRunningObjectTable *rot = NULL;
    IMoniker *moniker = NULL;
    struct rot_factory *object;
    DWORD cookie = 0, index;
    HANDLE event = NULL;
    HRESULT hr;
    void *metadata = NULL;
    hr = RoGetActivatableClassRegistration(class_name, &metadata);
    if (FAILED(hr)) return;
    IUnknown_Release((IUnknown *)metadata);
    if (FAILED(hr = xodus_server_identity(package, application))) return;
    if (wcschr(package, '|') || wcschr(application, '|') ||
        wcschr(WindowsGetStringRawBuffer(class_name, NULL), '|')) return;
    if (swprintf(item, ARRAY_SIZE(item), L"Xodus.WinRT.Factory.v1|%ls|%ls|%ls",
        package, application, WindowsGetStringRawBuffer(class_name, NULL)) < 0) return;
    if (!(object = calloc(1, sizeof(*object)))) return;
    object->iface.lpVtbl = &rot_factory_vtbl; object->refs = 1; object->callback = callback;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (const WCHAR *)callback, &object->module))
    { free(object); return; }
    if (FAILED(WindowsDuplicateString(class_name, &object->class_name)))
    { FreeLibrary(object->module); free(object); return; }
    hr = GetRunningObjectTable(0, &rot);
    if (SUCCEEDED(hr)) hr = CreateItemMoniker(L"!", item, &moniker);
    if (SUCCEEDED(hr) && IRunningObjectTable_IsRunning(rot, moniker) == S_OK) hr = CO_E_OBJISREG;
    if (SUCCEEDED(hr)) hr = IRunningObjectTable_Register(rot, ROTFLAGS_REGISTRATIONKEEPSALIVE,
        (IUnknown *)&object->iface, moniker, &cookie);
    if (hr == MK_S_MONIKERALREADYREGISTERED)
    {
        IRunningObjectTable_Revoke(rot, cookie); cookie = 0; hr = CO_E_OBJISREG;
    }
    ERR("ROT_FACTORY_DIAGNOSTIC_REGISTER pid=%lu hr=%#lx cookie=%lu calls=%ld; not native registration.\n",
        GetCurrentProcessId(), hr, cookie, object->calls);
    if (SUCCEEDED(hr) && cookie)
    {
        event = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (event) CoWaitForMultipleHandles(0, 8000, 1, &event, &index);
        InterlockedExchange(&object->revoked, 1);
        hr = IRunningObjectTable_Revoke(rot, cookie);
        ERR("ROT_FACTORY_DIAGNOSTIC_REVOKE hr=%#lx calls=%ld.\n", hr, object->calls);
        if (event) CoWaitForMultipleHandles(0, 2000, 1, &event, &index);
    }
    if (event) CloseHandle(event);
    if (moniker) IMoniker_Release(moniker);
    if (rot) IRunningObjectTable_Release(rot);
    rot_factory_release(&object->iface);
}
