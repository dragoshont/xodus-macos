/* Bounded read-only query of exported native activation metadata. */
static HRESULT xodus_decode_server_classes(const WCHAR *data, DWORD bytes, HSTRING **classes, DWORD *count)
{
    HSTRING *result;
    const WCHAR *cursor;
    DWORD number = 0, index;
    HRESULT hr;
    if (!classes || !count) return E_POINTER;
    *classes = NULL; *count = 0;
    if (!data || bytes < 4 || bytes % 2 || bytes > 128 * 1024 ||
        data[bytes / 2 - 1] || data[bytes / 2 - 2]) return E_INVALIDARG;
    for (index = 0; index < bytes / 2 - 1; ++index)
        if (!data[index]) ++number;
    if (!number || number > 256) return E_INVALIDARG;
    if (!(result = CoTaskMemAlloc(number * sizeof(*result)))) return E_OUTOFMEMORY;
    memset(result, 0, number * sizeof(*result));
    cursor = data;
    for (index = 0; index < number; ++index)
    {
        if (!*cursor || FAILED(hr = WindowsCreateString(cursor, wcslen(cursor), &result[index])))
        {
            if (!*cursor) hr = E_INVALIDARG;
            while (index) WindowsDeleteString(result[--index]);
            CoTaskMemFree(result);
            return hr;
        }
        cursor += wcslen(cursor) + 1;
    }
    *classes = result; *count = number;
    return S_OK;
}

#ifndef XODUS_SERVER_CATALOG_DECODER_ONLY
struct xodus_package_attribute
{
    UNICODE_STRING name;
    USHORT type, reserved;
    ULONG flags, count;
    UNICODE_STRING *strings;
};
struct xodus_package_attributes
{
    USHORT version, reserved;
    ULONG count;
    struct xodus_package_attribute *attributes;
};

static BOOL xodus_catalog_range(void *base, ULONG size, const void *pointer, SIZE_T bytes)
{
    ULONG_PTR start = (ULONG_PTR)base, address = (ULONG_PTR)pointer;
    return address >= start && address - start <= size && bytes <= size - (address - start);
}

static HRESULT xodus_server_identity(WCHAR *package, WCHAR *application)
{
    struct xodus_package_attributes *info = NULL;
    struct xodus_package_attribute *attribute;
    UNICODE_STRING name, *string;
    HANDLE token;
    NTSTATUS status;
    ULONG bytes = 0, allocated, i;
    HRESULT hr = E_INVALIDARG;
    if ((status = NtOpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)))
        return HRESULT_FROM_WIN32(RtlNtStatusToDosError(status));
    RtlInitUnicodeString(&name, L"WIN://SYSAPPID");
    status = NtQuerySecurityAttributesToken(token, &name, 1, NULL, 0, &bytes);
    if (status == STATUS_NOT_FOUND) hr = REGDB_E_CLASSNOTREG;
    else if (status != STATUS_BUFFER_TOO_SMALL)
        hr = status ? HRESULT_FROM_WIN32(RtlNtStatusToDosError(status)) : E_UNEXPECTED;
    else if (bytes >= sizeof(*info) && bytes <= 1024 * 1024)
    {
        allocated = bytes;
        if (!(info = malloc(allocated))) hr = E_OUTOFMEMORY;
        else if ((status = NtQuerySecurityAttributesToken(token, &name, 1, info, allocated, &bytes)))
            hr = HRESULT_FROM_WIN32(RtlNtStatusToDosError(status));
        else if (info->version == 1 && info->count == 1 &&
                 xodus_catalog_range(info, allocated, info->attributes, sizeof(*attribute)))
        {
            attribute = info->attributes;
            if (attribute->type == 3 && attribute->count >= 2 &&
                xodus_catalog_range(info, allocated, attribute->strings, 2 * sizeof(*string)))
            {
                hr = S_OK;
                for (i = 0; i < 2; ++i)
                {
                    WCHAR *output = i ? application : package;
                    string = &attribute->strings[i];
                    if (!string->Length || string->Length % 2 || string->Length >= 512 ||
                        !xodus_catalog_range(info, allocated, string->Buffer, string->Length))
                    { hr = E_INVALIDARG; break; }
                    memcpy(output, string->Buffer, string->Length);
                    output[string->Length / 2] = 0;
                    if (wcslen(output) != string->Length / 2 || wcschr(output, '\\') || wcschr(output, '/'))
                    { hr = E_INVALIDARG; break; }
                }
            }
        }
    }
    free(info);
    NtClose(token);
    return hr;
}

static HRESULT xodus_get_server_classes(HSTRING name, HSTRING **classes, DWORD *count)
{
    typedef NTSTATUS (WINAPI *query_package)(const UNICODE_STRING *, const UNICODE_STRING *, void *, ULONG *);
    query_package query = (query_package)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtXodusQueryRegisteredPackage");
    WCHAR package[256], application[256], key_name[1024], registered_image[4096], image[4096], *data;
    UNICODE_STRING package_string, application_string;
    HKEY key;
    ULONG image_bytes = sizeof(registered_image);
    DWORD bytes = 0, type, image_length;
    LONG status;
    NTSTATUS query_status;
    HRESULT hr;
    if (!classes || !count) return E_POINTER;
    *classes = NULL; *count = 0;
    if (FAILED(hr = xodus_server_identity(package, application))) return hr;
    if (!query) return E_NOTIMPL;
    RtlInitUnicodeString(&package_string, package);
    RtlInitUnicodeString(&application_string, application);
    if ((query_status = query(&package_string, &application_string, registered_image, &image_bytes)))
        return query_status == STATUS_NOT_FOUND ? REGDB_E_CLASSNOTREG :
            HRESULT_FROM_WIN32(RtlNtStatusToDosError(query_status));
    image_length = GetModuleFileNameW(NULL, image, ARRAY_SIZE(image));
    if (!image_bytes || image_bytes > sizeof(registered_image) || image_bytes % 2 ||
        registered_image[image_bytes / 2 - 1] ||
        !image_length || image_length >= ARRAY_SIZE(image) ||
        wcsicmp(image, !wcsncmp(registered_image, L"\\??\\", 4) ? registered_image + 4 : registered_image))
        return E_ACCESSDENIED;
    if (!name || !WindowsGetStringLen(name) || WindowsGetStringLen(name) > 256 ||
        wcslen(WindowsGetStringRawBuffer(name, NULL)) != WindowsGetStringLen(name) ||
        wcschr(WindowsGetStringRawBuffer(name, NULL), '\\') ||
        wcschr(WindowsGetStringRawBuffer(name, NULL), '/')) return E_INVALIDARG;
    if (swprintf(key_name, ARRAY_SIZE(key_name),
        L"Software\\Wine\\Xodus\\ActivationMetadata\\%ls\\%ls\\%ls",
        package, application, WindowsGetStringRawBuffer(name, NULL)) < 0) return E_INVALIDARG;
    if ((status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, key_name, 0, KEY_READ | KEY_WOW64_64KEY, &key)))
        return status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND ?
            REGDB_E_CLASSNOTREG : HRESULT_FROM_WIN32(status);
    status = RegQueryValueExW(key, L"ActivatableClasses", NULL, &type, NULL, &bytes);
    if (status)
    {
        RegCloseKey(key);
        return status == ERROR_FILE_NOT_FOUND ? E_INVALIDARG : HRESULT_FROM_WIN32(status);
    }
    if (type != REG_MULTI_SZ || bytes < 4 || bytes > 128 * 1024 || bytes % 2)
    { RegCloseKey(key); return E_INVALIDARG; }
    if (!(data = malloc(bytes))) { RegCloseKey(key); return E_OUTOFMEMORY; }
    status = RegQueryValueExW(key, L"ActivatableClasses", NULL, &type, (BYTE *)data, &bytes);
    RegCloseKey(key);
    if (status)
    {
        free(data);
        return status == ERROR_FILE_NOT_FOUND ? E_INVALIDARG : HRESULT_FROM_WIN32(status);
    }
    if (type != REG_MULTI_SZ || bytes < 4 || bytes % 2 ||
        data[bytes / 2 - 1] || data[bytes / 2 - 2])
    { free(data); return E_INVALIDARG; }
    hr = xodus_decode_server_classes(data, bytes, classes, count);
    free(data);
    if (FAILED(hr)) return hr;
    TRACE("XBOX_SERVER_CATALOG package=%s application=%s server=%s actual_classes=%lu\n",
        debugstr_w(package), debugstr_w(application), debugstr_hstring(name), *count);
    return S_OK;
}
#endif
