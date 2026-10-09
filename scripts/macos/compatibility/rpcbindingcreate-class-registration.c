/* IActivatableClassRegistration ABI: matching native public PDB and live controls. */
static const IID iid_class_registration =
    {0x9bbcae23, 0x3dd6, 0x49c3, {0xb6, 0x3c, 0x1c, 0x58, 0x7e, 0x7a, 0x6a, 0x67}};

struct xodus_class_registration;
struct xodus_class_registration_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct xodus_class_registration *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct xodus_class_registration *);
    ULONG (WINAPI *Release)(struct xodus_class_registration *);
    HRESULT (WINAPI *GetIids)(struct xodus_class_registration *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct xodus_class_registration *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct xodus_class_registration *, TrustLevel *);
    HRESULT (WINAPI *get_ActivatableClassId)(struct xodus_class_registration *, HSTRING *);
    HRESULT (WINAPI *get_ActivationType)(struct xodus_class_registration *, DWORD *);
    HRESULT (WINAPI *get_RegistrationScope)(struct xodus_class_registration *, DWORD *);
    HRESULT (WINAPI *get_RegisteredTrustLevel)(struct xodus_class_registration *, DWORD *);
    HRESULT (WINAPI *get_Attributes)(struct xodus_class_registration *, void **);
};
struct xodus_class_registration
{
    const struct xodus_class_registration_vtbl *lpVtbl;
    LONG refs;
    HSTRING name;
    WCHAR *metadata_key;
    DWORD activation_type;
};

static ULONG WINAPI class_registration_addref(struct xodus_class_registration *object)
{
    return InterlockedIncrement(&object->refs);
}
static ULONG WINAPI class_registration_release(struct xodus_class_registration *object)
{
    ULONG refs = InterlockedDecrement(&object->refs);
    if (!refs) { WindowsDeleteString(object->name); free(object->metadata_key); free(object); }
    return refs;
}
static HRESULT WINAPI class_registration_query(struct xodus_class_registration *object, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IInspectable) &&
        !IsEqualIID(iid, &iid_class_registration)) return E_NOINTERFACE;
    *out = object;
    class_registration_addref(object);
    return S_OK;
}
static HRESULT WINAPI class_registration_iids(struct xodus_class_registration *object, ULONG *count, IID **out)
{
    if (!out || !count) return E_POINTER;
    *out = NULL; *count = 0;
    if (!(*out = CoTaskMemAlloc(sizeof(**out)))) return E_OUTOFMEMORY;
    **out = iid_class_registration; *count = 1;
    return S_OK;
}
static HRESULT WINAPI class_registration_runtime_name(struct xodus_class_registration *object, HSTRING *out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    return E_NOTIMPL;
}
static HRESULT WINAPI class_registration_trust(struct xodus_class_registration *object, TrustLevel *out)
{
    if (!out) return E_POINTER;
    return E_NOTIMPL;
}
static HRESULT WINAPI class_registration_name(struct xodus_class_registration *object, HSTRING *out)
{
    return WindowsDuplicateString(object->name, out);
}
static HRESULT WINAPI class_registration_activation(struct xodus_class_registration *object, DWORD *out)
{
    if (!out) return E_POINTER;
    *out = object->activation_type;
    return S_OK;
}
static HRESULT WINAPI class_registration_unsupported_number(struct xodus_class_registration *object, DWORD *out)
{
    if (!out) return E_POINTER;
    return E_NOTIMPL;
}
static HRESULT WINAPI class_registration_attributes(struct xodus_class_registration *object, void **out)
{
    WCHAR path[1024], entry[1024];
    HKEY key;
    DWORD type, bytes, aliased, value_count;
    HSTRING entry_point;
    LONG status;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    if (swprintf(path, ARRAY_SIZE(path), L"%ls\\CustomAttributes", object->metadata_key) < 0) return E_INVALIDARG;
    if ((status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ | KEY_WOW64_64KEY, &key)))
        return HRESULT_FROM_WIN32(status);
    status = RegQueryInfoKeyW(key, NULL, NULL, NULL, NULL, NULL, NULL, &value_count, NULL, NULL, NULL, NULL);
    if (status || value_count != 2)
    { RegCloseKey(key); return status ? HRESULT_FROM_WIN32(status) : E_NOTIMPL; }
    bytes = sizeof(aliased);
    status = RegQueryValueExW(key, L"AppObject.Aliased", NULL, &type, (BYTE *)&aliased, &bytes);
    if (status || type != REG_DWORD || bytes != sizeof(aliased))
    { RegCloseKey(key); return status ? HRESULT_FROM_WIN32(status) : E_INVALIDARG; }
    bytes = sizeof(entry);
    status = RegQueryValueExW(key, L"AppObject.EntryPoint", NULL, &type, (BYTE *)entry, &bytes);
    RegCloseKey(key);
    if (status || type != REG_SZ || bytes < 2 || bytes % 2 || bytes > sizeof(entry) || entry[bytes / 2 - 1] ||
        wcslen(entry) != bytes / 2 - 1)
        return status ? HRESULT_FROM_WIN32(status) : E_INVALIDARG;
    if (FAILED(hr = WindowsCreateString(entry, bytes / 2 - 1, &entry_point))) return hr;
    hr = attributes_create(aliased, entry_point, (IMapView_HSTRING_IInspectable **)out);
    WindowsDeleteString(entry_point);
    return hr;
}
static const struct xodus_class_registration_vtbl class_registration_vtbl =
{
    class_registration_query, class_registration_addref, class_registration_release,
    class_registration_iids, class_registration_runtime_name, class_registration_trust,
    class_registration_name, class_registration_activation, class_registration_unsupported_number,
    class_registration_unsupported_number, class_registration_attributes
};

HRESULT WINAPI RoGetActivatableClassRegistration(HSTRING name, void **out)
{
    WCHAR package[256], application[256], key_name[768], server[257];
    HKEY root, key;
    DWORD index, chars, bytes, type, activation;
    HSTRING server_name, *classes;
    DWORD count, i;
    HRESULT hr;
    LONG status;
    struct xodus_class_registration *object;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!name || !WindowsGetStringLen(name) || WindowsGetStringLen(name) > 256 ||
        WindowsGetStringLen(name) != wcslen(WindowsGetStringRawBuffer(name, NULL))) return E_INVALIDARG;
    if (FAILED(hr = xodus_server_identity(package, application))) return hr;
    swprintf(key_name, ARRAY_SIZE(key_name), L"Software\\Wine\\Xodus\\ActivationMetadata\\%ls\\%ls", package, application);
    if ((status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, key_name, 0, KEY_READ | KEY_WOW64_64KEY, &root)))
        return HRESULT_FROM_WIN32(status);
    hr = REGDB_E_CLASSNOTREG;
    for (index = 0; ; ++index)
    {
        chars = ARRAY_SIZE(server);
        status = RegEnumKeyExW(root, index, server, &chars, NULL, NULL, NULL, NULL);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status) { hr = HRESULT_FROM_WIN32(status); break; }
        if (FAILED(hr = WindowsCreateString(server, chars, &server_name))) break;
        hr = xodus_get_server_classes(server_name, &classes, &count);
        WindowsDeleteString(server_name);
        if (FAILED(hr)) break;
        for (i = 0; i < count; ++i)
            if (!wcscmp(WindowsGetStringRawBuffer(classes[i], NULL), WindowsGetStringRawBuffer(name, NULL))) break;
        {
            BOOL found = i != count;
            for (i = 0; i < count; ++i) WindowsDeleteString(classes[i]);
            CoTaskMemFree(classes);
            if (!found) { hr = REGDB_E_CLASSNOTREG; continue; }
        }
        swprintf(key_name, ARRAY_SIZE(key_name), L"%ls\\Classes\\%ls", server, WindowsGetStringRawBuffer(name, NULL));
        status = RegOpenKeyExW(root, key_name, 0, KEY_READ, &key);
        if (status) { hr = HRESULT_FROM_WIN32(status); break; }
        bytes = sizeof(activation);
        status = RegQueryValueExW(key, L"ActivationType", NULL, &type, (BYTE *)&activation, &bytes);
        RegCloseKey(key);
        if (status || type != REG_DWORD || bytes != sizeof(activation))
        { hr = status ? HRESULT_FROM_WIN32(status) : E_INVALIDARG; break; }
        if (!(object = calloc(1, sizeof(*object)))) { hr = E_OUTOFMEMORY; break; }
        object->lpVtbl = &class_registration_vtbl; object->refs = 1; object->activation_type = activation;
        {
            WCHAR full_key[1024];
            if (swprintf(full_key, ARRAY_SIZE(full_key),
                L"Software\\Wine\\Xodus\\ActivationMetadata\\%ls\\%ls\\%ls", package, application, key_name) < 0 ||
                !(object->metadata_key = wcsdup(full_key)))
            { free(object); hr = E_OUTOFMEMORY; break; }
        }
        hr = WindowsDuplicateString(name, &object->name);
        if (FAILED(hr)) { free(object->metadata_key); free(object); break; }
        *out = object;
        TRACE("Resolved actual class metadata %s activation=%lu.\n", debugstr_hstring(name), activation);
        hr = S_OK;
        break;
    }
    RegCloseKey(root);
    return hr;
}
