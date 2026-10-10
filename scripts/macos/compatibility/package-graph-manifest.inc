/* Manifest parser and package graph helpers extracted from the frozen Xbox
 * runtime 97fa66a. Private candidate, under the Wine LGPL-2.1-or-later license. */
/* Package resources contexts.  The current package root is the nearest ancestor of the
 * main module whose AppxManifest.xml Identity matches the WIN://SYSAPPID full name.
 * Property ids, blob layout and errors follow native Windows 11 26100 measurements. */

#define RESCTX_MAX_PROPERTY 6

struct package_resources_context
{
    DWORD magic;
    WCHAR *values[RESCTX_MAX_PROPERTY + 1];
    UINT32 flags[RESCTX_MAX_PROPERTY + 1];
};

struct package_globalization_context
{
    UINT32 tag;
    UINT32 size;
    UINT32 flags;
    UINT32 app_id_bytes;
    UINT32 reserved;
    WCHAR app_id[1];
};

struct package_globalization_entry
{
    struct package_globalization_entry *next;
    struct package_globalization_context context;
};

struct package_resources
{
    WCHAR root[MAX_PATH];
    WCHAR *publisher;
    struct package_resources_context package;
    struct package_resources_context application;
    struct package_globalization_entry *globalization;
};

static struct package_resources *current_package_resources;

struct manifest_tag
{
    const WCHAR *name;
    int name_len;
    const WCHAR *attrs;
    const WCHAR *end;
    const WCHAR *next;
    BOOL closing;
    BOOL empty;
};

static BOOL manifest_next_tag( const WCHAR *p, const WCHAR *end, struct manifest_tag *tag )
{
    const WCHAR *q;
    WCHAR quote;

    while (p < end)
    {
        if (*p != '<') { p++; continue; }
        if (end - p >= 4 && !wcsncmp( p, L"<!--", 4 ))
        {
            for (q = p + 4; end - q >= 3 && wcsncmp( q, L"-->", 3 ); q++);
            if (end - q < 3) return FALSE;
            p = q + 3;
            continue;
        }
        if (end - p >= 9 && !wcsncmp( p, L"<![CDATA[", 9 ))
        {
            for (q = p + 9; end - q >= 3 && wcsncmp( q, L"]]>", 3 ); q++);
            if (end - q < 3) return FALSE;
            p = q + 3;
            continue;
        }
        if (end - p >= 2 && (p[1] == '?' || p[1] == '!'))
        {
            for (q = p + 2; q < end && *q != '>'; q++);
            if (q >= end) return FALSE;
            p = q + 1;
            continue;
        }
        tag->closing = end - p >= 2 && p[1] == '/';
        q = p + 1 + tag->closing;
        tag->name = q;
        while (q < end && *q != '>' && *q != '/' && *q != ' ' && *q != '\t' && *q != '\r' && *q != '\n') q++;
        tag->name_len = q - tag->name;
        tag->attrs = q;
        quote = 0;
        while (q < end && (quote || *q != '>'))
        {
            if (quote) { if (*q == quote) quote = 0; }
            else if (*q == '"' || *q == '\'') quote = *q;
            q++;
        }
        if (q >= end || !tag->name_len) return FALSE;
        tag->empty = q[-1] == '/';
        tag->end = q;
        tag->next = q + 1;
        return TRUE;
    }
    return FALSE;
}

static BOOL manifest_local_name_is( const WCHAR *name, int len, const WCHAR *local )
{
    int i, local_len = wcslen( local );

    for (i = len - 1; i >= 0; i--) if (name[i] == ':') break;
    name += i + 1;
    len -= i + 1;
    return len == local_len && !wcsncmp( name, local, len );
}

static WCHAR *manifest_decode( const WCHAR *s, const WCHAR *end, BOOL trim )
{
    WCHAR *ret, *d;
    const WCHAR *semi;
    unsigned int c;

    if (trim)
    {
        while (s < end && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) s++;
        while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) end--;
    }
    if (!(ret = d = HeapAlloc( GetProcessHeap(), 0, (end - s + 1) * sizeof(WCHAR) ))) return NULL;
    while (s < end)
    {
        if (*s != '&') { *d++ = *s++; continue; }
        for (semi = s + 1; semi < end && semi - s < 12 && *semi != ';'; semi++);
        if (semi >= end || *semi != ';') goto error;
        if (semi - s == 4 && !wcsncmp( s, L"&amp", 4 )) *d++ = '&';
        else if (semi - s == 3 && !wcsncmp( s, L"&lt", 3 )) *d++ = '<';
        else if (semi - s == 3 && !wcsncmp( s, L"&gt", 3 )) *d++ = '>';
        else if (semi - s == 5 && !wcsncmp( s, L"&quot", 5 )) *d++ = '"';
        else if (semi - s == 5 && !wcsncmp( s, L"&apos", 5 )) *d++ = '\'';
        else if (semi - s > 2 && s[1] == '#')
        {
            const WCHAR *p = s + 2;
            int base = 10;
            c = 0;
            if (*p == 'x') { base = 16; p++; }
            if (p == semi) goto error;
            for (; p < semi; p++)
            {
                unsigned int digit;
                if (*p >= '0' && *p <= '9') digit = *p - '0';
                else if (base == 16 && *p >= 'a' && *p <= 'f') digit = *p - 'a' + 10;
                else if (base == 16 && *p >= 'A' && *p <= 'F') digit = *p - 'A' + 10;
                else goto error;
                c = c * base + digit;
                if (c > 0x10ffff) goto error;
            }
            if (!c || (c >= 0xd800 && c <= 0xdfff)) goto error;
            if (c >= 0x10000)
            {
                c -= 0x10000;
                *d++ = 0xd800 | (c >> 10);
                *d++ = 0xdc00 | (c & 0x3ff);
            }
            else *d++ = c;
        }
        else goto error;
        s = semi + 1;
    }
    *d = 0;
    return ret;
error:
    HeapFree( GetProcessHeap(), 0, ret );
    return NULL;
}

/* Returns TRUE and sets *value (possibly NULL on decode failure) when the attribute exists. */
static BOOL manifest_attribute( const struct manifest_tag *tag, const WCHAR *attr, WCHAR **value )
{
    const WCHAR *p = tag->attrs, *name, *start;
    int attr_len = wcslen( attr );
    WCHAR quote;

    for (;;)
    {
        while (p < tag->end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == '/')) p++;
        if (p >= tag->end) return FALSE;
        name = p;
        while (p < tag->end && *p != '=' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
        if (p - name == attr_len && !wcsncmp( name, attr, attr_len )) start = name;
        else start = NULL;
        while (p < tag->end && *p != '=') p++;
        p++;
        while (p < tag->end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
        if (p >= tag->end || (*p != '"' && *p != '\'')) return FALSE;
        quote = *p++;
        name = p;
        while (p < tag->end && *p != quote) p++;
        if (p >= tag->end) return FALSE;
        if (start)
        {
            *value = manifest_decode( name, p, FALSE );
            return TRUE;
        }
        p++;
    }
}

static void free_package_resources( struct package_resources *res )
{
    struct package_globalization_entry *entry, *next;
    unsigned int i;

    if (!res) return;
    HeapFree( GetProcessHeap(), 0, res->publisher );
    for (i = 0; i <= RESCTX_MAX_PROPERTY; i++)
    {
        HeapFree( GetProcessHeap(), 0, res->package.values[i] );
        HeapFree( GetProcessHeap(), 0, res->application.values[i] );
    }
    for (entry = res->globalization; entry; entry = next)
    {
        next = entry->next;
        HeapFree( GetProcessHeap(), 0, entry );
    }
    HeapFree( GetProcessHeap(), 0, res );
}

static WCHAR *read_manifest( const WCHAR *path, const WCHAR **end )
{
    LARGE_INTEGER size;
    DWORD read;
    BYTE *data;
    WCHAR *text = NULL;
    HANDLE file;
    int len;

    file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL );
    if (file == INVALID_HANDLE_VALUE) return NULL;
    if (!GetFileSizeEx( file, &size ) || !size.QuadPart || size.QuadPart > 8 * 1024 * 1024 ||
        !(data = HeapAlloc( GetProcessHeap(), 0, size.QuadPart )))
    {
        CloseHandle( file );
        return NULL;
    }
    if (ReadFile( file, data, size.LowPart, &read, NULL ) && read == size.LowPart)
    {
        if (read >= 2 && data[0] == 0xff && data[1] == 0xfe && !(read % 2))
        {
            len = read / 2 - 1;
            if ((text = HeapAlloc( GetProcessHeap(), 0, (len + 1) * sizeof(WCHAR) )))
                memcpy( text, data + 2, len * sizeof(WCHAR) );
        }
        else
        {
            const BYTE *utf8 = data;
            DWORD utf8_len = read;
            if (read >= 3 && data[0] == 0xef && data[1] == 0xbb && data[2] == 0xbf) { utf8 += 3; utf8_len -= 3; }
            len = MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)utf8, utf8_len, NULL, 0 );
            if (len > 0 && (text = HeapAlloc( GetProcessHeap(), 0, (len + 1) * sizeof(WCHAR) )))
                MultiByteToWideChar( CP_UTF8, 0, (const char *)utf8, utf8_len, text, len );
        }
        if (text) { text[len] = 0; *end = text + len; }
    }
    HeapFree( GetProcessHeap(), 0, data );
    CloseHandle( file );
    return text;
}

static BOOL full_name_field_is( const WCHAR *full_name, unsigned int field, const WCHAR *value )
{
    const WCHAR *start = full_name, *p;
    unsigned int i;

    for (i = 0; i < field; i++)
    {
        if (!(start = wcschr( start, '_' ))) return FALSE;
        start++;
    }
    if (!(p = wcschr( start, '_' ))) return FALSE;
    return p - start == wcslen( value ) && !wcsnicmp( start, value, p - start );
}

static WCHAR *manifest_resolve_file( const WCHAR *root, WCHAR *value )
{
    WCHAR *path;
    SIZE_T len;

    if (!value) return NULL;
    len = wcslen( root ) + 1 + wcslen( value ) + 1;
    if ((path = HeapAlloc( GetProcessHeap(), 0, len * sizeof(WCHAR) )))
    {
        wcscpy( path, root );
        wcscat( path, L"\\" );
        wcscat( path, value );
    }
    HeapFree( GetProcessHeap(), 0, value );
    return path;
}

/* Parses the manifest under root if its Identity matches the package full name.
 * Returns ERROR_SUCCESS, ERROR_NOT_FOUND for a non-matching manifest, or an error. */
static LONG parse_package_manifest( const WCHAR *root, const WCHAR *full_name, const WCHAR *app_id,
                                    struct package_resources *res )
{
    static const WCHAR *package_elements[] = { NULL, L"DisplayName", L"PublisherDisplayName", L"Description", L"Logo" };
    static const WCHAR *application_attributes[] = { NULL, L"DisplayName", NULL, L"Description",
                                                     L"Square150x150Logo", L"Square44x44Logo" };
    struct { const WCHAR *name; int len; } stack[8];
    struct manifest_tag tag;
    const WCHAR *text, *end, *p;
    WCHAR path[MAX_PATH], *data, *value;
    BOOL identity = FALSE, app_match = FALSE, app_found = FALSE;
    unsigned int depth = 0, i;
    LONG ret = ERROR_NOT_FOUND;

    if (wcslen( root ) + ARRAY_SIZE(L"\\AppxManifest.xml") > ARRAY_SIZE(path)) return ERROR_NOT_FOUND;
    wcscpy( path, root );
    wcscat( path, L"\\AppxManifest.xml" );
    if (!(data = read_manifest( path, &end ))) return ERROR_NOT_FOUND;
    text = data;
    p = text;
    while (manifest_next_tag( p, end, &tag ))
    {
        p = tag.next;
        if (tag.closing)
        {
            if (!depth) { ret = ERROR_BAD_FORMAT; goto done; }
            depth--;
            if (depth == 2) app_match = FALSE;
            continue;
        }
        if (!depth && !manifest_local_name_is( tag.name, tag.name_len, L"Package" )) goto done;
        if (depth == 1 && manifest_local_name_is( tag.name, tag.name_len, L"Identity" ) && !identity)
        {
            static const WCHAR *fields[] = { L"Name", L"Version", L"ProcessorArchitecture" };
            for (i = 0; i < ARRAY_SIZE(fields); i++)
            {
                value = NULL;
                if (!manifest_attribute( &tag, fields[i], &value ))
                {
                    if (i != 2) goto done;
                    value = HeapAlloc( GetProcessHeap(), 0, sizeof(L"neutral") );
                    if (value) wcscpy( value, L"neutral" );
                }
                if (!value || !full_name_field_is( full_name, i, value ))
                {
                    HeapFree( GetProcessHeap(), 0, value );
                    goto done;
                }
                HeapFree( GetProcessHeap(), 0, value );
            }
            if (!manifest_attribute( &tag, L"Publisher", &res->publisher ) || !res->publisher) goto done;
            identity = TRUE;
        }
        else if (depth == 2 && stack[1].name && manifest_local_name_is( stack[1].name, stack[1].len, L"Properties" ) &&
                 !tag.empty)
        {
            for (i = 1; i < ARRAY_SIZE(package_elements); i++)
            {
                const WCHAR *content_end;
                if (!manifest_local_name_is( tag.name, tag.name_len, package_elements[i] ) ||
                    res->package.values[i]) continue;
                for (content_end = tag.next; content_end < end && *content_end != '<'; content_end++);
                if (!(value = manifest_decode( tag.next, content_end, TRUE ))) { ret = ERROR_BAD_FORMAT; goto done; }
                if (i == 4)
                {
                    if (!(value = manifest_resolve_file( root, value ))) { ret = ERROR_NOT_ENOUGH_MEMORY; goto done; }
                    res->package.flags[i] = 1;
                }
                res->package.values[i] = value;
            }
        }
        else if (depth == 2 && stack[1].name && manifest_local_name_is( stack[1].name, stack[1].len, L"Applications" ) &&
                 manifest_local_name_is( tag.name, tag.name_len, L"Application" ))
        {
            value = NULL;
            if (manifest_attribute( &tag, L"Id", &value ) && value && !app_found && !wcscmp( value, app_id ))
                app_match = app_found = TRUE;
            HeapFree( GetProcessHeap(), 0, value );
        }
        else if (depth == 3 && app_match && manifest_local_name_is( tag.name, tag.name_len, L"VisualElements" ))
        {
            for (i = 1; i < ARRAY_SIZE(application_attributes); i++)
            {
                if (!application_attributes[i] || res->application.values[i]) continue;
                value = NULL;
                if (!manifest_attribute( &tag, application_attributes[i], &value )) continue;
                if (!value) { ret = ERROR_BAD_FORMAT; goto done; }
                if (i >= 4)
                {
                    if (!(value = manifest_resolve_file( root, value ))) { ret = ERROR_NOT_ENOUGH_MEMORY; goto done; }
                    res->application.flags[i] = 1;
                }
                res->application.values[i] = value;
            }
        }
        if (!tag.empty)
        {
            if (depth >= 64) { ret = ERROR_BAD_FORMAT; goto done; }
            if (depth < ARRAY_SIZE(stack))
            {
                stack[depth].name = tag.name;
                stack[depth].len = tag.name_len;
            }
            depth++;
        }
    }
    if (identity && app_found)
    {
        struct package_globalization_entry **tail = &res->globalization;
        depth = 0;
        p = text;
        while (manifest_next_tag( p, end, &tag ))
        {
            p = tag.next;
            if (tag.closing) { depth--; continue; }
            if (depth == 2 && manifest_local_name_is( stack[1].name, stack[1].len, L"Applications" ) &&
                manifest_local_name_is( tag.name, tag.name_len, L"Application" ))
            {
                struct package_globalization_entry *entry;
                SIZE_T bytes;
                value = NULL;
                if (!manifest_attribute( &tag, L"Id", &value ) || !value || !value[0])
                {
                    HeapFree( GetProcessHeap(), 0, value );
                    ret = ERROR_BAD_FORMAT;
                    goto done;
                }
                bytes = (wcslen(value) + 1) * sizeof(WCHAR);
                if (!(entry = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY,
                        offsetof(struct package_globalization_entry, context.app_id) + bytes )))
                {
                    HeapFree( GetProcessHeap(), 0, value );
                    ret = ERROR_NOT_ENOUGH_MEMORY;
                    goto done;
                }
                entry->context.tag = 0x424f4c47;
                entry->context.size = offsetof(struct package_globalization_context, app_id) + bytes;
                entry->context.app_id_bytes = bytes;
                memcpy( entry->context.app_id, value, bytes );
                HeapFree( GetProcessHeap(), 0, value );
                *tail = entry;
                tail = &entry->next;
            }
            if (!tag.empty)
            {
                if (depth < ARRAY_SIZE(stack))
                {
                    stack[depth].name = tag.name;
                    stack[depth].len = tag.name_len;
                }
                depth++;
            }
        }
        ret = ERROR_SUCCESS;
    }
    else if (identity) WARN( "Application %s not found in %s\n", debugstr_w(app_id), debugstr_w(path) );
done:
    if (ret == ERROR_BAD_FORMAT) WARN( "Malformed package manifest %s\n", debugstr_w(path) );
    HeapFree( GetProcessHeap(), 0, data );
    return ret;
}

static LONG get_current_package_resources( struct package_resources **out )
{
    WCHAR full_name[PACKAGE_FULL_NAME_MAX_LENGTH + 1], app_id[APPLICATION_USER_MODEL_ID_MAX_LENGTH];
    struct package_resources *res;
    UINT32 len;
    WCHAR *slash;
    LONG ret;

    len = ARRAY_SIZE(full_name);
    if ((ret = get_current_package_identity_string( 0, &len, full_name ))) return ret;
    len = ARRAY_SIZE(app_id);
    if ((ret = get_current_package_identity_string( 1, &len, app_id ))) return ret;
    if ((res = InterlockedCompareExchangePointer( (void **)&current_package_resources, NULL, NULL )))
    {
        *out = res;
        return ERROR_SUCCESS;
    }
    if (!(res = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*res) ))) return ERROR_NOT_ENOUGH_MEMORY;
    res->package.magic = res->application.magic = 0x58534552;
    len = GetModuleFileNameW( NULL, res->root, ARRAY_SIZE(res->root) );
    if (!len || len >= ARRAY_SIZE(res->root))
    {
        free_package_resources( res );
        return ERROR_NOT_FOUND;
    }
    ret = ERROR_NOT_FOUND;
    while ((slash = wcsrchr( res->root, '\\' )) && slash > res->root && slash[-1] != ':')
    {
        *slash = 0;
        ret = parse_package_manifest( res->root, full_name, app_id, res );
        if (ret != ERROR_NOT_FOUND) break;
    }
    if (ret)
    {
        WARN( "No manifest for %s above the main module, error %ld\n", debugstr_w(full_name), ret );
        free_package_resources( res );
        return ret;
    }
    TRACE( "package %s root %s\n", debugstr_w(full_name), debugstr_w(res->root) );
    if (InterlockedCompareExchangePointer( (void **)&current_package_resources, res, NULL ))
    {
        free_package_resources( res );
        res = current_package_resources;
    }
    *out = res;
    return ERROR_SUCCESS;
}

#define MAX_PACKAGE_DEPENDENCIES 32

struct package_node
{
    WCHAR root[MAX_PATH];
    WCHAR name[PACKAGE_FULL_NAME_MAX_LENGTH + 1];
    WCHAR publisher[512];
    WCHAR arch[16];
    PACKAGE_VERSION version;
    PACKAGE_VERSION max_tested;
    BOOL framework;
    BOOL has_max_tested;
};

struct package_dependency
{
    WCHAR name[PACKAGE_FULL_NAME_MAX_LENGTH + 1];
    WCHAR publisher[512];
    PACKAGE_VERSION min_version;
};

static BOOL parse_package_version( const WCHAR *s, PACKAGE_VERSION *version )
{
    unsigned long part[4];
    WCHAR *e = NULL;
    unsigned int i;

    for (i = 0; i < 4; i++)
    {
        part[i] = wcstoul( s, &e, 10 );
        if (e == s || part[i] > 0xffff || (i < 3 && *e != '.')) return FALSE;
        s = e + 1;
    }
    if (*e) return FALSE;
    version->Major = part[0];
    version->Minor = part[1];
    version->Build = part[2];
    version->Revision = part[3];
    return TRUE;
}

static BOOL copy_manifest_attribute( const struct manifest_tag *tag, const WCHAR *attr, WCHAR *out, SIZE_T size )
{
    WCHAR *value = NULL;
    BOOL ret = manifest_attribute( tag, attr, &value ) && value && wcslen( value ) < size;

    if (ret) wcscpy( out, value );
    HeapFree( GetProcessHeap(), 0, value );
    return ret;
}

/* Reads Identity, Properties/Framework and Dependencies/PackageDependency from root\AppxManifest.xml. */
static LONG read_package_node( const WCHAR *root, struct package_node *node,
                               struct package_dependency *deps, unsigned int *dep_count )
{
    struct { const WCHAR *name; int len; } stack[8];
    struct manifest_tag tag;
    const WCHAR *end, *p, *content_end;
    WCHAR path[MAX_PATH], version[64], *data, *value;
    unsigned int depth = 0;
    BOOL identity = FALSE;
    LONG ret = ERROR_BAD_FORMAT;

    if (dep_count) *dep_count = 0;
    if (wcslen( root ) + ARRAY_SIZE(L"\\AppxManifest.xml") > ARRAY_SIZE(path)) return ERROR_NOT_FOUND;
    memset( node, 0, sizeof(*node) );
    wcscpy( node->root, root );
    wcscpy( path, root );
    wcscat( path, L"\\AppxManifest.xml" );
    if (!(data = read_manifest( path, &end ))) return ERROR_NOT_FOUND;
    p = data;
    while (manifest_next_tag( p, end, &tag ))
    {
        p = tag.next;
        if (tag.closing)
        {
            if (!depth) goto done;
            depth--;
            continue;
        }
        if (!depth && !manifest_local_name_is( tag.name, tag.name_len, L"Package" )) goto done;
        if (depth == 1 && !identity && manifest_local_name_is( tag.name, tag.name_len, L"Identity" ))
        {
            if (!copy_manifest_attribute( &tag, L"Name", node->name, ARRAY_SIZE(node->name) ) ||
                !copy_manifest_attribute( &tag, L"Publisher", node->publisher, ARRAY_SIZE(node->publisher) ) ||
                !copy_manifest_attribute( &tag, L"Version", version, ARRAY_SIZE(version) ) ||
                !parse_package_version( version, &node->version )) goto done;
            if (!copy_manifest_attribute( &tag, L"ProcessorArchitecture", node->arch, ARRAY_SIZE(node->arch) ))
                wcscpy( node->arch, L"neutral" );
            identity = TRUE;
        }
        else if (depth == 2 && !tag.empty && manifest_local_name_is( stack[1].name, stack[1].len, L"Properties" ) &&
                 manifest_local_name_is( tag.name, tag.name_len, L"Framework" ))
        {
            for (content_end = tag.next; content_end < end && *content_end != '<'; content_end++);
            if (!(value = manifest_decode( tag.next, content_end, TRUE ))) goto done;
            node->framework = !wcsicmp( value, L"true" );
            HeapFree( GetProcessHeap(), 0, value );
        }
        else if (depth == 2 && !node->has_max_tested &&
                 manifest_local_name_is( stack[1].name, stack[1].len, L"Dependencies" ) &&
                 manifest_local_name_is( tag.name, tag.name_len, L"TargetDeviceFamily" ))
        {
            if (!copy_manifest_attribute( &tag, L"MaxVersionTested", version, ARRAY_SIZE(version) ) ||
                !parse_package_version( version, &node->max_tested )) goto done;
            node->has_max_tested = TRUE;
        }
        else if (depth == 2 && dep_count && manifest_local_name_is( stack[1].name, stack[1].len, L"Dependencies" ) &&
                 manifest_local_name_is( tag.name, tag.name_len, L"PackageDependency" ))
        {
            struct package_dependency *dep;

            if (*dep_count >= MAX_PACKAGE_DEPENDENCIES) { ret = ERROR_NOT_SUPPORTED; goto done; }
            dep = &deps[*dep_count];
            if (!copy_manifest_attribute( &tag, L"Name", dep->name, ARRAY_SIZE(dep->name) ) ||
                !copy_manifest_attribute( &tag, L"Publisher", dep->publisher, ARRAY_SIZE(dep->publisher) ) ||
                !copy_manifest_attribute( &tag, L"MinVersion", version, ARRAY_SIZE(version) ) ||
                !parse_package_version( version, &dep->min_version )) goto done;
            ++*dep_count;
        }
        if (!tag.empty)
        {
            if (depth >= 64) goto done;
            if (depth < ARRAY_SIZE(stack))
            {
                stack[depth].name = tag.name;
                stack[depth].len = tag.name_len;
            }
            depth++;
        }
    }
    if (identity) ret = ERROR_SUCCESS;
done:
    if (ret == ERROR_BAD_FORMAT) WARN( "Malformed package manifest %s\n", debugstr_w(path) );
    HeapFree( GetProcessHeap(), 0, data );
    return ret;
}

/* Wine stages packages as sibling directories of the caller's package root; pick the highest
 * matching version like the deployment resolver does for a static PackageDependency. */
static LONG resolve_package_dependency( const WCHAR *repository, const struct package_node *head,
                                        const struct package_dependency *dep, struct package_node *out )
{
    WIN32_FIND_DATAW data;
    WCHAR pattern[MAX_PATH], root[MAX_PATH];
    struct package_node node;
    BOOL found = FALSE;
    HANDLE find;

    if (wcslen( repository ) + 3 > ARRAY_SIZE(pattern)) return APPMODEL_ERROR_PACKAGE_NOT_AVAILABLE;
    wcscpy( pattern, repository );
    wcscat( pattern, L"\\*" );
    if ((find = FindFirstFileW( pattern, &data )) == INVALID_HANDLE_VALUE) return APPMODEL_ERROR_PACKAGE_NOT_AVAILABLE;
    do
    {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || data.cFileName[0] == '.') continue;
        if (wcslen( repository ) + 1 + wcslen( data.cFileName ) >= ARRAY_SIZE(root)) continue;
        wcscpy( root, repository );
        wcscat( root, L"\\" );
        wcscat( root, data.cFileName );
        if (read_package_node( root, &node, NULL, NULL )) continue;
        if (wcsicmp( node.name, dep->name ) || wcscmp( node.publisher, dep->publisher )) continue;
        if (wcsicmp( node.arch, head->arch ) && wcsicmp( node.arch, L"neutral" )) continue;
        if (node.version.Version < dep->min_version.Version) continue;
        if (found && node.version.Version <= out->version.Version) continue;
        *out = node;
        found = TRUE;
    } while (FindNextFileW( find, &data ));
    FindClose( find );
    return found ? ERROR_SUCCESS : APPMODEL_ERROR_PACKAGE_NOT_AVAILABLE;
}

static BOOL format_package_names( const struct package_node *node, const WCHAR *publisher_id,
                                  WCHAR *full, WCHAR *family )
{
    WCHAR arch[16];
    unsigned int i;

    for (i = 0; node->arch[i] && i < ARRAY_SIZE(arch) - 1; i++)
        arch[i] = node->arch[i] >= 'A' && node->arch[i] <= 'Z' ? node->arch[i] + 32 : node->arch[i];
    arch[i] = 0;
    if (full && _snwprintf( full, PACKAGE_FULL_NAME_MAX_LENGTH, L"%s_%u.%u.%u.%u_%s__%s", node->name,
                            node->version.Major, node->version.Minor, node->version.Build,
                            node->version.Revision, arch, publisher_id ) < 0) return FALSE;
    if (family && _snwprintf( family, PACKAGE_FAMILY_NAME_MAX_LENGTH, L"%s_%s", node->name, publisher_id ) < 0)
        return FALSE;
    return TRUE;
}

static WCHAR *put_package_string( BYTE **cursor, const WCHAR *s )
{
    SIZE_T bytes = (wcslen( s ) + 1) * sizeof(WCHAR);
    WCHAR *ret = (WCHAR *)*cursor;

    memcpy( ret, s, bytes );
    *cursor += bytes;
    return ret;
}

/***********************************************************************
 *         GetCurrentPackageInfo2   (kernelbase.@)
 *
 * Measured on Windows 11 26100 for Microsoft.GamingApp with four framework dependencies:
 * the PACKAGE_INFO array is followed, per package and without padding, by path, name,
 * publisherId, publisher, full name and family name; an empty resourceId is NULL. The head has
 * flags 0x10, static framework dependencies 0x21. Filter 0, HEAD, DIRECT or STATIC select
 * packages; other filters succeed with an empty result. Install and Effective paths are the
 * install directory; Mutable fails with APPMODEL_ERROR_NO_MUTABLE_DIRECTORY. A short buffer
 * fails with ERROR_INSUFFICIENT_BUFFER after setting the length and count.
 */
LONG WINAPI GetCurrentPackageInfo2( const UINT32 flags, PackagePathType type, UINT32 *buffer_length,
                                    BYTE *buffer, UINT32 *count )
{
    struct package_graph
    {
        struct package_node nodes[MAX_PACKAGE_DEPENDENCIES + 1];
        struct package_dependency deps[MAX_PACKAGE_DEPENDENCIES];
        WCHAR full[MAX_PACKAGE_DEPENDENCIES + 1][PACKAGE_FULL_NAME_MAX_LENGTH + 1];
        WCHAR family[MAX_PACKAGE_DEPENDENCIES + 1][PACKAGE_FAMILY_NAME_MAX_LENGTH + 1];
        WCHAR publisher_ids[MAX_PACKAGE_DEPENDENCIES + 1][14];
    } *graph;
    WCHAR full_name[PACKAGE_FULL_NAME_MAX_LENGTH + 1], repository[MAX_PATH], *slash;
    BYTE basic[sizeof(PACKAGE_ID) + sizeof(full_name)];
    UINT32 name_len = ARRAY_SIZE(full_name), basic_len = sizeof(basic), size, available, first, last, i;
    PACKAGE_ID *head_id = (PACKAGE_ID *)basic;
    struct package_resources *res;
    unsigned int dep_count = 0;
    PACKAGE_INFO *info;
    BOOL head, direct;
    BYTE *cursor;
    LONG ret;

    TRACE( "(%#x %d %p %p %p)\n", flags, type, buffer_length, buffer, count );

    if (!buffer_length || !count || (*buffer_length && !buffer)) return ERROR_INVALID_PARAMETER;
    if (type != PackagePathType_Install && type != PackagePathType_Mutable && type != PackagePathType_Effective)
        return ERROR_INVALID_PARAMETER;
    if ((ret = get_current_package_identity_string( 0, &name_len, full_name ))) return ret;
    if ((ret = PackageIdFromFullName( full_name, 0, &basic_len, basic ))) return ret;
    if ((ret = get_current_package_resources( &res ))) return ret;

    head = !flags || (flags & (PACKAGE_FILTER_HEAD | PACKAGE_FILTER_STATIC));
    direct = !flags || (flags & (PACKAGE_FILTER_DIRECT | PACKAGE_FILTER_STATIC));
    if (!(graph = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*graph) ))) return ERROR_NOT_ENOUGH_MEMORY;
    if ((ret = read_package_node( res->root, &graph->nodes[0], graph->deps, &dep_count ))) goto done;
    if ((ret = package_publisher_id( graph->nodes[0].publisher, graph->publisher_ids[0] ))) goto done;
    if (wcscmp( head_id->publisherId, graph->publisher_ids[0] ))
    {
        ret = ERROR_INVALID_DATA;
        goto done;
    }
    wcscpy( graph->full[0], full_name );
    if (wcslen( graph->nodes[0].name ) + 1 + wcslen( head_id->publisherId ) >= ARRAY_SIZE(graph->family[0]))
    {
        ret = ERROR_INVALID_DATA;
        goto done;
    }
    wcscpy( graph->family[0], graph->nodes[0].name );
    wcscat( graph->family[0], L"_" );
    wcscat( graph->family[0], head_id->publisherId );

    if (!direct) dep_count = 0;
    if (dep_count)
    {
        wcscpy( repository, res->root );
        if (!(slash = wcsrchr( repository, '\\' )))
        {
            ret = APPMODEL_ERROR_PACKAGE_NOT_AVAILABLE;
            goto done;
        }
        *slash = 0;
    }
    for (i = 0; i < dep_count; i++)
    {
        struct package_node *node = &graph->nodes[i + 1];

        if ((ret = resolve_package_dependency( repository, &graph->nodes[0], &graph->deps[i], node )))
        {
            WARN( "Dependency %s of %s is not staged\n", debugstr_w(graph->deps[i].name), debugstr_w(full_name) );
            goto done;
        }
        if ((ret = package_publisher_id( node->publisher, graph->publisher_ids[i + 1] ))) goto done;
        if (!format_package_names( node, graph->publisher_ids[i + 1], graph->full[i + 1], graph->family[i + 1] ))
        {
            ret = ERROR_INVALID_DATA;
            goto done;
        }
    }

    first = head ? 0 : 1;
    last = dep_count + 1;
    if (first >= last)
    {
        *buffer_length = 0;
        *count = 0;
        ret = ERROR_SUCCESS;
        goto done;
    }
    if (type == PackagePathType_Mutable)
    {
        ret = APPMODEL_ERROR_NO_MUTABLE_DIRECTORY;
        goto done;
    }

    size = (last - first) * sizeof(PACKAGE_INFO);
    for (i = first; i < last; i++)
    {
        size += (wcslen( graph->nodes[i].root ) + 1 + wcslen( graph->nodes[i].name ) + 1 +
                 wcslen( graph->publisher_ids[i] ) + 1 + wcslen( graph->nodes[i].publisher ) + 1 +
                 wcslen( graph->full[i] ) + 1 + wcslen( graph->family[i] ) + 1) * sizeof(WCHAR);
        if (!i && head_id->resourceId && head_id->resourceId[0])
            size += (wcslen( head_id->resourceId ) + 1) * sizeof(WCHAR);
    }
    available = *buffer_length;
    *buffer_length = size;
    *count = last - first;
    if (available < size)
    {
        ret = ERROR_INSUFFICIENT_BUFFER;
        goto done;
    }

    info = (PACKAGE_INFO *)buffer;
    memset( info, 0, (last - first) * sizeof(PACKAGE_INFO) );
    cursor = buffer + (last - first) * sizeof(PACKAGE_INFO);
    for (i = first; i < last; i++, info++)
    {
        const struct package_node *node = &graph->nodes[i];

        info->flags = (i ? PACKAGE_FILTER_DIRECT : PACKAGE_FILTER_HEAD) |
                      (node->framework ? PACKAGE_PROPERTY_FRAMEWORK : 0);
        info->packageId.processorArchitecture = i ? processor_arch_from_string( node->arch, wcslen( node->arch ) )
                                                  : head_id->processorArchitecture;
        info->packageId.version = node->version;
        info->path = put_package_string( &cursor, node->root );
        info->packageId.name = put_package_string( &cursor, node->name );
        if (!i && head_id->resourceId && head_id->resourceId[0])
            info->packageId.resourceId = put_package_string( &cursor, head_id->resourceId );
        info->packageId.publisherId = put_package_string( &cursor, graph->publisher_ids[i] );
        info->packageId.publisher = put_package_string( &cursor, node->publisher );
        info->packageFullName = put_package_string( &cursor, graph->full[i] );
        info->packageFamilyName = put_package_string( &cursor, graph->family[i] );
    }
    ret = ERROR_SUCCESS;
done:
    HeapFree( GetProcessHeap(), 0, graph );
    return ret;
}

static LONG get_current_package_id( UINT32 *length, BYTE *buffer )
{
    UINT32 bytes = 0, count = 0, required, available, size;
    PACKAGE_INFO *info;
    PACKAGE_ID *id = (PACKAGE_ID *)buffer;
    const WCHAR *strings[4];
    WCHAR **fields[4];
    BYTE *graph;
    unsigned int i;
    LONG status;

    if (!length || (*length && !buffer)) return ERROR_INVALID_PARAMETER;
    status = GetCurrentPackageInfo2( PACKAGE_FILTER_HEAD, PackagePathType_Install, &bytes, NULL, &count );
    if (status != ERROR_INSUFFICIENT_BUFFER) return status;
    if (!(graph = HeapAlloc( GetProcessHeap(), 0, bytes ))) return ERROR_NOT_ENOUGH_MEMORY;
    status = GetCurrentPackageInfo2( PACKAGE_FILTER_HEAD, PackagePathType_Install, &bytes, graph, &count );
    if (status) goto done;
    if (count != 1) { status = ERROR_INVALID_DATA; goto done; }
    info = (PACKAGE_INFO *)graph;
    strings[0] = info->packageId.name;
    strings[1] = info->packageId.resourceId;
    strings[2] = info->packageId.publisherId;
    strings[3] = info->packageId.publisher;
    required = sizeof(*id);
    for (i = 0; i < ARRAY_SIZE(strings); i++)
        if (strings[i]) required += (wcslen(strings[i]) + 1) * sizeof(WCHAR);
    available = *length;
    *length = required;
    if (available < required) { status = ERROR_INSUFFICIENT_BUFFER; goto done; }
    *id = info->packageId;
    fields[0] = &id->name;
    fields[1] = &id->resourceId;
    fields[2] = &id->publisherId;
    fields[3] = &id->publisher;
    buffer += sizeof(*id);
    for (i = 0; i < ARRAY_SIZE(strings); i++)
    {
        *fields[i] = NULL;
        if (!strings[i]) continue;
        size = (wcslen(strings[i]) + 1) * sizeof(WCHAR);
        memcpy( buffer, strings[i], size );
        *fields[i] = (WCHAR *)buffer;
        buffer += size;
    }
    status = ERROR_SUCCESS;
done:
    HeapFree( GetProcessHeap(), 0, graph );
    return status;
}

static LONG get_current_package_info( UINT32 flags, UINT32 *length, BYTE *buffer, UINT32 *count )
{
    return GetCurrentPackageInfo2( flags, PackagePathType_Install, length, buffer, count );
}

static LONG get_current_package_path( UINT32 *length, WCHAR *path )
{
    struct package_resources *resources;
    UINT32 required;
    LONG status;

    if (!length || (*length && !path)) return ERROR_INVALID_PARAMETER;
    if ((status = get_current_package_resources( &resources ))) return status;
    required = wcslen(resources->root) + 1;
    if (*length < required) { *length = required; return ERROR_INSUFFICIENT_BUFFER; }
    wcscpy( path, resources->root );
    *length = required;
    return ERROR_SUCCESS;
}

static LONG get_current_package_aumid( UINT32 *length, WCHAR *value )
{
    WCHAR family[PACKAGE_FAMILY_NAME_MAX_LENGTH + 1], app[APPLICATION_USER_MODEL_ID_MAX_LENGTH];
    UINT32 size = ARRAY_SIZE(family), app_size = ARRAY_SIZE(app), required;
    LONG status;

    if (!length || (*length && !value)) return ERROR_INVALID_PARAMETER;
    if ((status = GetCurrentPackageFamilyName( &size, family )))
        return status == APPMODEL_ERROR_NO_PACKAGE ? APPMODEL_ERROR_NO_APPLICATION : status;
    if ((status = get_current_package_identity_string( 1, &app_size, app ))) return status;
    required = wcslen(family) + 1 + wcslen(app) + 1;
    if (*length < required) { *length = required; return ERROR_INSUFFICIENT_BUFFER; }
    wcscpy( value, family );
    wcscat( value, L"!" );
    wcscat( value, app );
    *length = required;
    return ERROR_SUCCESS;
}

static LONG find_current_package_node( const WCHAR *full_name, struct package_node *node )
{
    UINT32 bytes = 0, count = 0, i;
    PACKAGE_INFO *info;
    BYTE *graph;
    LONG status;
    LONG result = ERROR_NOT_FOUND;

    status = GetCurrentPackageInfo2( 0, PackagePathType_Install, &bytes, NULL, &count );
    if (status != ERROR_INSUFFICIENT_BUFFER) return status;
    if (!(graph = HeapAlloc( GetProcessHeap(), 0, bytes ))) return ERROR_NOT_ENOUGH_MEMORY;
    status = GetCurrentPackageInfo2( 0, PackagePathType_Install, &bytes, graph, &count );
    if (status) { result = status; goto done; }
    info = (PACKAGE_INFO *)graph;
    for (i = 0; i < count; i++)
    {
        if (wcsicmp( full_name, info[i].packageFullName )) continue;
        result = read_package_node( info[i].path, node, NULL, NULL );
        break;
    }
done:
    HeapFree( GetProcessHeap(), 0, graph );
    return result;
}

HRESULT WINAPI AppXGetOSMaxVersionTested( const WCHAR *full_name, UINT64 *version )
{
    struct package_node node;
    LONG status;

    TRACE( "(%s %p)\n", debugstr_w(full_name), version );
    if (!full_name || !version) return E_INVALIDARG;
    if (!full_name[0]) return HRESULT_FROM_WIN32( ERROR_MORE_DATA );
    status = find_current_package_node( full_name, &node );
    if (status) return HRESULT_FROM_WIN32( status == ERROR_NOT_FOUND ? ERROR_FILE_NOT_FOUND : status );
    if (!node.has_max_tested) return HRESULT_FROM_WIN32( ERROR_NOT_FOUND );
    *version = node.max_tested.Version;
    return S_OK;
}

LONG WINAPI GetStagedPackagePathByFullName2( const WCHAR *full_name, PackagePathType type,
                                           UINT32 *length, WCHAR *path )
{
    struct package_node node;
    UINT32 bytes = 0, required;
    LONG status;

    TRACE( "(%s %u %p %p)\n", debugstr_w(full_name), type, length, path );
    if (!full_name || !length || (*length && !path) || (UINT32)type > 5) return ERROR_INVALID_PARAMETER;
    if (!full_name[0]) return ERROR_MORE_DATA;
    if (PackageIdFromFullName( full_name, 0, &bytes, NULL ) != ERROR_INSUFFICIENT_BUFFER)
        return ERROR_INVALID_PARAMETER;
    if ((status = find_current_package_node( full_name, &node ))) return status;
    if (type == PackagePathType_Mutable) return APPMODEL_ERROR_NO_MUTABLE_DIRECTORY;
    if (type != PackagePathType_Install && type != PackagePathType_Effective) return ERROR_NOT_FOUND;
    required = wcslen(node.root) + 1;
    if (*length < required) { *length = required; return ERROR_INSUFFICIENT_BUFFER; }
    memcpy( path, node.root, required * sizeof(WCHAR) );
    *length = required;
    return ERROR_SUCCESS;
}

/* Default contexts in the native packaged oracle had both property bits clear.
 * The source of non-default bits is not established; this is the default subset,
 * not an implementation of manifest-specific globalization overrides. */
LONG WINAPI GetCurrentPackageGlobalizationContext( UINT32 index, void *reserved, void **context )
{
    struct package_resources *resources;
    struct package_globalization_entry *entry;
    LONG status;

    TRACE( "(%u %p %p)\n", index, reserved, context );
    if (!context) return ERROR_INVALID_PARAMETER;
    if ((status = get_current_package_resources( &resources ))) return status;
    for (entry = resources->globalization; entry && index; entry = entry->next) index--;
    if (!entry) return ERROR_NOT_FOUND;
    *context = &entry->context;
    TRACE( "returning manifest-bound default globalization context\n" );
    return ERROR_SUCCESS;
}

LONG WINAPI GetPackageGlobalizationProperty( void *context, UINT32 property, UINT32 *size, void *buffer )
{
    struct package_resources *resources;
    struct package_globalization_entry *entry;
    UINT32 value;
    LONG status;

    TRACE( "(%p %u %p %p)\n", context, property, size, buffer );
    if (!context || !size || (*size && !buffer) || (property != 1 && property != 2))
        return ERROR_INVALID_PARAMETER;
    if ((status = get_current_package_resources( &resources ))) return status;
    for (entry = resources->globalization; entry; entry = entry->next)
        if (context == &entry->context) break;
    if (!entry)
    {
        WARN( "Unsupported globalization context, not owned by the current manifest\n" );
        return ERROR_INVALID_PARAMETER;
    }
    if (*size < sizeof(value))
    {
        *size = sizeof(value);
        return ERROR_INSUFFICIENT_BUFFER;
    }
    value = (entry->context.flags >> (property - 1)) & 1;
    memset( buffer, 0, *size );
    memcpy( buffer, &value, sizeof(value) );
    *size = sizeof(value);
    return ERROR_SUCCESS;
}
