/* Trusted isolated-prefix Wine interop, not an adversarial package-security boundary. */
typedef HRESULT (WINAPI *winrt_get_rot_fn)(DWORD, IRunningObjectTable **);
typedef HRESULT (WINAPI *winrt_item_moniker_fn)(const WCHAR *, const WCHAR *, IMoniker **);

struct winrt_registration_factory
{
    IClassFactory iface;
    LONG refs, revoked;
    HSTRING name;
    PFNGETACTIVATIONFACTORY callback;
    HMODULE module;
    struct winrt_registration *registration;
};

struct winrt_registration_entry
{
    struct winrt_registration_factory *factory;
    DWORD rot_cookie;
};

struct winrt_registration
{
    IApartmentShutdown shutdown;
    LONG refs;
    LONG cancelled;
    BOOL published;
    ULONG_PTR cookie;
    APARTMENT_SHUTDOWN_REGISTRATION_COOKIE shutdown_cookie;
    IRunningObjectTable *rot;
    HMODULE ole32;
    UINT32 count;
    struct winrt_registration_entry *entries;
    struct winrt_registration *next;
};

static SRWLOCK winrt_registration_lock = SRWLOCK_INIT;
static struct winrt_registration *winrt_registrations;
static LONG64 winrt_next_cookie;
static ULONG WINAPI winrt_registration_release(IApartmentShutdown *iface);

static struct winrt_registration_factory *winrt_factory_from(IClassFactory *iface)
{
    return CONTAINING_RECORD(iface, struct winrt_registration_factory, iface);
}

static ULONG WINAPI winrt_factory_addref(IClassFactory *iface)
{
    return InterlockedIncrement(&winrt_factory_from(iface)->refs);
}

static ULONG WINAPI winrt_factory_release(IClassFactory *iface)
{
    struct winrt_registration_factory *factory = winrt_factory_from(iface);
    ULONG refs = InterlockedDecrement(&factory->refs);
    if (!refs)
    {
        WindowsDeleteString(factory->name);
        if (factory->module) FreeLibrary(factory->module);
        if (factory->registration) winrt_registration_release(&factory->registration->shutdown);
        free(factory);
    }
    return refs;
}

static HRESULT WINAPI winrt_factory_query(IClassFactory *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IClassFactory))
        return E_NOINTERFACE;
    *out = iface;
    winrt_factory_addref(iface);
    return S_OK;
}

static HRESULT WINAPI winrt_factory_create(IClassFactory *iface, IUnknown *outer, REFIID iid, void **out)
{
    struct winrt_registration_factory *object = winrt_factory_from(iface);
    IActivationFactory *factory = NULL;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    if (InterlockedCompareExchange(&object->revoked, 0, 0) ||
        InterlockedCompareExchange(&object->registration->cancelled, 0, 0))
        return CO_E_OBJNOTCONNECTED;
    hr = object->callback(object->name, &factory);
    if (SUCCEEDED(hr) && !factory) hr = E_UNEXPECTED;
    if (SUCCEEDED(hr)) hr = IActivationFactory_QueryInterface(factory, iid, out);
    if (factory) IActivationFactory_Release(factory);
    return hr;
}

static HRESULT WINAPI winrt_factory_lock_server(IClassFactory *iface, BOOL lock)
{
    return E_NOTIMPL;
}

static const IClassFactoryVtbl winrt_registration_factory_vtbl =
{
    winrt_factory_query, winrt_factory_addref, winrt_factory_release,
    winrt_factory_create, winrt_factory_lock_server
};

static HRESULT winrt_registration_transport(HMODULE *module, winrt_get_rot_fn *get_rot,
                                            winrt_item_moniker_fn *make_moniker)
{
    if (!(*module = LoadLibraryW(L"ole32.dll"))) return HRESULT_FROM_WIN32(GetLastError());
    *get_rot = (winrt_get_rot_fn)GetProcAddress(*module, "GetRunningObjectTable");
    *make_moniker = (winrt_item_moniker_fn)GetProcAddress(*module, "CreateItemMoniker");
    if (!*get_rot || !*make_moniker)
    {
        FreeLibrary(*module);
        *module = NULL;
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    return S_OK;
}

static HRESULT winrt_registration_moniker(winrt_item_moniker_fn make_moniker,
                                         const WCHAR *package, const WCHAR *application,
                                         HSTRING class_name, IMoniker **out)
{
    const WCHAR *name = WindowsGetStringRawBuffer(class_name, NULL);
    WCHAR item[1024];
    int size;
    if (wcschr(package, '|') || wcschr(application, '|') || wcschr(name, '|'))
        return E_INVALIDARG;
    size = swprintf(item, ARRAY_SIZE(item), L"Xodus.WinRT.Factory.v1|%ls|%ls|%ls",
                    package, application, name);
    if (size < 0 || size >= ARRAY_SIZE(item)) return E_INVALIDARG;
    return make_moniker(L"!", item, out);
}

static struct winrt_registration *winrt_registration_from(IApartmentShutdown *iface)
{
    return CONTAINING_RECORD(iface, struct winrt_registration, shutdown);
}

static ULONG WINAPI winrt_registration_addref(IApartmentShutdown *iface)
{
    return InterlockedIncrement(&winrt_registration_from(iface)->refs);
}

static ULONG WINAPI winrt_registration_release(IApartmentShutdown *iface)
{
    struct winrt_registration *registration = winrt_registration_from(iface);
    ULONG refs = InterlockedDecrement(&registration->refs);
    if (!refs)
    {
        if (registration->rot) IRunningObjectTable_Release(registration->rot);
        if (registration->ole32) FreeLibrary(registration->ole32);
        free(registration->entries);
        free(registration);
    }
    return refs;
}

static HRESULT WINAPI winrt_registration_query(IApartmentShutdown *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IApartmentShutdown) &&
        !IsEqualIID(iid, &IID_IAgileObject)) return E_NOINTERFACE;
    *out = iface;
    winrt_registration_addref(iface);
    return S_OK;
}

static void winrt_registration_destroy(struct winrt_registration *registration)
{
    APARTMENT_SHUTDOWN_REGISTRATION_COOKIE cookie;
    UINT32 i;
    InterlockedExchange(&registration->cancelled, 1);
    for (i = 0; i < registration->count; ++i)
        if (registration->entries[i].factory)
            InterlockedExchange(&registration->entries[i].factory->revoked, 1);
    AcquireSRWLockExclusive(&winrt_registration_lock);
    cookie = registration->shutdown_cookie;
    registration->shutdown_cookie = NULL;
    ReleaseSRWLockExclusive(&winrt_registration_lock);
    if (cookie)
    {
        HRESULT hr;
        hr = apartment_unregister_shutdown_callback(cookie);
        if (FAILED(hr)) ERR("WinRT apartment cleanup unsubscribe failed, hr %#lx.\n", hr);
    }
    for (i = 0; i < registration->count; ++i)
    {
        struct winrt_registration_entry *entry = &registration->entries[i];
        if (entry->rot_cookie)
        {
            HRESULT hr = IRunningObjectTable_Revoke(registration->rot, entry->rot_cookie);
            if (FAILED(hr)) ERR("WinRT factory revoke failed, cookie %lu hr %#lx.\n", entry->rot_cookie, hr);
        }
        if (entry->factory) winrt_factory_release(&entry->factory->iface);
    }
    winrt_registration_release(&registration->shutdown);
}

static void winrt_revoke_factories(RO_REGISTRATION_COOKIE cookie)
{
    struct winrt_registration **cursor, *registration = NULL;
    AcquireSRWLockExclusive(&winrt_registration_lock);
    for (cursor = &winrt_registrations; *cursor; cursor = &(*cursor)->next)
        if ((*cursor)->cookie == (ULONG_PTR)cookie)
        {
            registration = *cursor;
            InterlockedExchange(&registration->cancelled, 1);
            *cursor = registration->next;
            break;
        }
    ReleaseSRWLockExclusive(&winrt_registration_lock);
    if (registration) winrt_registration_destroy(registration);
}

static void WINAPI winrt_registration_shutdown(IApartmentShutdown *iface, UINT64 identifier)
{
    struct winrt_registration *registration = winrt_registration_from(iface);
    BOOL published;
    AcquireSRWLockExclusive(&winrt_registration_lock);
    InterlockedExchange(&registration->cancelled, 1);
    registration->shutdown_cookie = NULL;
    published = registration->published;
    ReleaseSRWLockExclusive(&winrt_registration_lock);
    if (published) winrt_revoke_factories((RO_REGISTRATION_COOKIE)registration->cookie);
}

static const IApartmentShutdownVtbl winrt_registration_shutdown_vtbl =
{
    winrt_registration_query, winrt_registration_addref, winrt_registration_release,
    winrt_registration_shutdown
};

static HRESULT winrt_register_factories(HSTRING *classes, PFNGETACTIVATIONFACTORY *callbacks,
                                       UINT32 count, RO_REGISTRATION_COOKIE *cookie)
{
    WCHAR package[256], application[256];
    struct winrt_registration *registration;
    winrt_get_rot_fn get_rot;
    winrt_item_moniker_fn make_moniker;
    APTTYPE apartment;
    APTTYPEQUALIFIER qualifier;
    UINT64 identifier;
    ULONG_PTR published_cookie;
    UINT32 i, j;
    SIZE_T entries_bytes;
    HRESULT hr;
    if (!cookie) return E_POINTER;
    *cookie = NULL;
    if (FAILED(hr = CoGetApartmentType(&apartment, &qualifier))) return hr;
    if (!classes || !callbacks || !count) return E_INVALIDARG;
    if (FAILED(hr = xodus_server_identity(package, application))) return hr;
    entries_bytes = (SIZE_T)count * sizeof(*registration->entries);
    if (entries_bytes / count != sizeof(*registration->entries)) return E_OUTOFMEMORY;
    if (!(registration = calloc(1, sizeof(*registration)))) return E_OUTOFMEMORY;
    registration->shutdown.lpVtbl = &winrt_registration_shutdown_vtbl;
    registration->refs = 1;
    registration->count = count;
    if (!(registration->entries = calloc(count, sizeof(*registration->entries))))
    { winrt_registration_release(&registration->shutdown); return E_OUTOFMEMORY; }
    hr = winrt_registration_transport(&registration->ole32, &get_rot, &make_moniker);
    if (SUCCEEDED(hr)) hr = get_rot(0, &registration->rot);
    for (i = 0; SUCCEEDED(hr) && i < count; ++i)
    {
        struct winrt_registration_entry *entry = &registration->entries[i];
        struct winrt_registration_factory *factory;
        IMoniker *moniker = NULL;
        IInspectable *metadata = NULL;
        if (!classes[i] || !callbacks[i]) { hr = E_INVALIDARG; break; }
        for (j = 0; j < i; ++j)
            if (!wcscmp(WindowsGetStringRawBuffer(classes[i], NULL),
                        WindowsGetStringRawBuffer(classes[j], NULL))) break;
        if (j != i) { hr = E_INVALIDARG; break; }
        hr = RoGetActivatableClassRegistration(classes[i], (void **)&metadata);
        if (FAILED(hr)) break;
        IInspectable_Release(metadata);
        if (!(factory = calloc(1, sizeof(*factory)))) { hr = E_OUTOFMEMORY; break; }
        factory->iface.lpVtbl = &winrt_registration_factory_vtbl;
        factory->refs = 1;
        factory->callback = callbacks[i];
        factory->registration = registration;
        winrt_registration_addref(&registration->shutdown);
        entry->factory = factory;
        hr = WindowsDuplicateString(classes[i], &factory->name);
        if (SUCCEEDED(hr) && !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            (const WCHAR *)callbacks[i], &factory->module)) hr = HRESULT_FROM_WIN32(GetLastError());
        if (SUCCEEDED(hr)) hr = winrt_registration_moniker(make_moniker, package, application, classes[i], &moniker);
        if (SUCCEEDED(hr))
        {
            HRESULT running = IRunningObjectTable_IsRunning(registration->rot, moniker);
            if (running == S_OK) hr = CO_E_OBJISREG;
            else if (running != S_FALSE) hr = running;
        }
        if (SUCCEEDED(hr)) hr = IRunningObjectTable_Register(registration->rot, ROTFLAGS_REGISTRATIONKEEPSALIVE,
            (IUnknown *)&factory->iface, moniker, &entry->rot_cookie);
        if (hr == MK_S_MONIKERALREADYREGISTERED) hr = CO_E_OBJISREG;
        if (moniker) IMoniker_Release(moniker);
    }
    if (SUCCEEDED(hr))
        hr = apartment_register_shutdown_callback(&registration->shutdown, &identifier,
            (void **)&registration->shutdown_cookie);
    if (FAILED(hr))
    {
        winrt_registration_destroy(registration);
        return hr;
    }
    AcquireSRWLockExclusive(&winrt_registration_lock);
    if (registration->cancelled)
    {
        registration->shutdown_cookie = NULL;
        ReleaseSRWLockExclusive(&winrt_registration_lock);
        winrt_registration_destroy(registration);
        return CO_E_OBJNOTCONNECTED;
    }
    registration->cookie = (ULONG_PTR)InterlockedIncrement64(&winrt_next_cookie);
    published_cookie = registration->cookie;
    registration->published = TRUE;
    winrt_registration_addref(&registration->shutdown);
    registration->next = winrt_registrations;
    winrt_registrations = registration;
    ReleaseSRWLockExclusive(&winrt_registration_lock);
    *cookie = (RO_REGISTRATION_COOKIE)published_cookie;
    TRACE("Registered %u owning Wine-local WinRT factories, cookie %p.\n", count, *cookie);
    winrt_registration_release(&registration->shutdown);
    return S_OK;
}

static HRESULT winrt_get_registered_factory(HSTRING class_name, REFIID iid, void **out, BOOL *handled)
{
    WCHAR package[256], application[256];
    struct xodus_class_registration *metadata = NULL;
    HMODULE ole32 = NULL;
    winrt_get_rot_fn get_rot;
    winrt_item_moniker_fn make_moniker;
    IRunningObjectTable *rot = NULL;
    IMoniker *moniker = NULL;
    IUnknown *object = NULL;
    IClassFactory *factory = NULL;
    HRESULT hr;
    *handled = FALSE;
    hr = RoGetActivatableClassRegistration(class_name, (void **)&metadata);
    if (FAILED(hr)) return hr;
    if (metadata->activation_type != 1)
    { class_registration_release(metadata); return REGDB_E_CLASSNOTREG; }
    *handled = TRUE;
    class_registration_release(metadata);
    if (FAILED(hr = xodus_server_identity(package, application))) return hr;
    hr = winrt_registration_transport(&ole32, &get_rot, &make_moniker);
    if (SUCCEEDED(hr)) hr = get_rot(0, &rot);
    if (SUCCEEDED(hr)) hr = winrt_registration_moniker(make_moniker, package, application, class_name, &moniker);
    if (SUCCEEDED(hr)) hr = IRunningObjectTable_GetObject(rot, moniker, &object);
    if (SUCCEEDED(hr)) hr = IUnknown_QueryInterface(object, &IID_IClassFactory, (void **)&factory);
    if (SUCCEEDED(hr)) hr = IClassFactory_CreateInstance(factory, NULL, iid, out);
    if (factory) IClassFactory_Release(factory);
    if (object) IUnknown_Release(object);
    if (moniker) IMoniker_Release(moniker);
    if (rot) IRunningObjectTable_Release(rot);
    if (ole32) FreeLibrary(ole32);
    return hr;
}
