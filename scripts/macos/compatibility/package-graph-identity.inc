/* Private compatibility candidate; metadata follows a catalog-activated token
 * and actual package manifests. This grants no capability or Store entitlement. */
#include "bcrypt.h"

#ifndef PACKAGE_FULL_NAME_MAX_LENGTH
#define PACKAGE_FULL_NAME_MAX_LENGTH 127
#define PACKAGE_FAMILY_NAME_MAX_LENGTH 64
#endif
#define PACKAGE_PROPERTY_FRAMEWORK 0x00000001
#define PACKAGE_FILTER_HEAD 0x00000010
#define PACKAGE_FILTER_DIRECT 0x00000020
#define PACKAGE_FILTER_STATIC 0x00080000

typedef enum
{
    PackagePathType_Install,
    PackagePathType_Mutable,
    PackagePathType_Effective
} PackagePathType;

typedef struct
{
    UINT32 reserved, flags;
    WCHAR *path, *packageFullName, *packageFamilyName;
    PACKAGE_ID packageId;
} PACKAGE_INFO;

static BOOL package_identity_character( WCHAR value, BOOL punctuation )
{
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') || (punctuation && (value == '.' || value == '-'));
}

LONG WINAPI PackageFamilyNameFromFullName( const WCHAR *full_name, UINT32 *length, WCHAR *family )
{
    const WCHAR *parts[5], *end, *cursor;
    union
    {
        PACKAGE_ID id;
        BYTE bytes[sizeof(PACKAGE_ID) + (51 + 31 + 14) * sizeof(WCHAR)];
    } parsed;
    UINT32 bytes = sizeof(parsed), required, name_length, publisher_length;
    unsigned int i, component, number;
    LONG status;

    TRACE( "(%s %p %p)\n", debugstr_w(full_name), length, family );
    if (!full_name || !length || (*length && !family)) return ERROR_INVALID_PARAMETER;
    parts[0] = full_name;
    for (i = 1; i < ARRAY_SIZE(parts); i++)
    {
        if (!(cursor = wcschr( parts[i - 1], '_' ))) return ERROR_INVALID_PARAMETER;
        parts[i] = cursor + 1;
    }
    name_length = parts[1] - parts[0] - 1;
    publisher_length = wcslen(parts[4]);
    if (name_length < 3 || name_length > 50 || parts[4] - parts[3] - 1 > 30 ||
        publisher_length != 13) return ERROR_INVALID_PARAMETER;
    for (i = 0; i < ARRAY_SIZE(parts); i++)
    {
        if (i == 1 || i == 2) continue;
        end = i == 4 ? parts[i] + publisher_length : parts[i + 1] - 1;
        for (cursor = parts[i]; cursor < end; cursor++)
            if (!package_identity_character( *cursor, i != 4 )) return ERROR_INVALID_PARAMETER;
    }
    cursor = parts[1];
    end = parts[2] - 1;
    for (component = 0; component < 4; component++)
    {
        if (cursor == end || *cursor < '0' || *cursor > '9') return ERROR_INVALID_PARAMETER;
        number = 0;
        while (cursor < end && *cursor >= '0' && *cursor <= '9')
        {
            number = number * 10 + *cursor++ - '0';
            if (number > 65535) return ERROR_INVALID_PARAMETER;
        }
        if (component == 3) { if (cursor != end) return ERROR_INVALID_PARAMETER; }
        else if (cursor == end || *cursor++ != '.') return ERROR_INVALID_PARAMETER;
    }
    if ((status = PackageIdFromFullName( full_name, 0, &bytes, parsed.bytes ))) return status;
    required = name_length + publisher_length + 2;
    if (*length < required) { *length = required; return ERROR_INSUFFICIENT_BUFFER; }
    memcpy( family, parsed.id.name, name_length * sizeof(WCHAR) );
    family[name_length] = '_';
    memcpy( family + name_length + 1, parsed.id.publisherId, (publisher_length + 1) * sizeof(WCHAR) );
    *length = required;
    return ERROR_SUCCESS;
}

static LONG get_current_package_identity_string( unsigned int index, UINT32 *length, WCHAR *value )
{
    SIZE_T bytes;
    HANDLE token;
    NTSTATUS status;

    if (!length || (*length && !value) || index > 1) return ERROR_INVALID_PARAMETER;
    bytes = (SIZE_T)*length * sizeof(WCHAR);
    if ((status = NtOpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token )))
        return RtlNtStatusToDosError( status );
    if (index)
        status = RtlQueryPackageIdentity( token, NULL, NULL, value, &bytes, NULL );
    else
        status = RtlQueryPackageIdentity( token, value, &bytes, NULL, NULL, NULL );
    NtClose( token );
    if (status == STATUS_NOT_FOUND) return APPMODEL_ERROR_NO_PACKAGE;
    if (status == STATUS_SUCCESS || status == STATUS_BUFFER_TOO_SMALL)
        *length = bytes / sizeof(WCHAR);
    return RtlNtStatusToDosError( status );
}

static LONG package_publisher_id( const WCHAR *publisher, WCHAR output[14] )
{
    static const WCHAR alphabet[] = L"0123456789abcdefghjkmnpqrstvwxyz";
    typedef NTSTATUS (WINAPI *OPEN)(BCRYPT_ALG_HANDLE *, const WCHAR *, const WCHAR *, ULONG);
    typedef NTSTATUS (WINAPI *CREATE)(BCRYPT_ALG_HANDLE, BCRYPT_HASH_HANDLE *, UCHAR *, ULONG,
                                      UCHAR *, ULONG, ULONG);
    typedef NTSTATUS (WINAPI *DATA)(BCRYPT_HASH_HANDLE, UCHAR *, ULONG, ULONG);
    typedef NTSTATUS (WINAPI *FINISH)(BCRYPT_HASH_HANDLE, UCHAR *, ULONG, ULONG);
    typedef NTSTATUS (WINAPI *DESTROY)(BCRYPT_HASH_HANDLE);
    typedef NTSTATUS (WINAPI *CLOSE)(BCRYPT_ALG_HANDLE, ULONG);
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    UCHAR digest[32];
    HMODULE module;
    OPEN open;
    CREATE create;
    DATA data;
    FINISH finish;
    DESTROY destroy;
    CLOSE close;
    NTSTATUS status;
    unsigned int group, bit;

    if (!(module = LoadLibraryW( L"bcrypt.dll" ))) return GetLastError();
    open = (OPEN)(ULONG_PTR)GetProcAddress( module, "BCryptOpenAlgorithmProvider" );
    create = (CREATE)(ULONG_PTR)GetProcAddress( module, "BCryptCreateHash" );
    data = (DATA)(ULONG_PTR)GetProcAddress( module, "BCryptHashData" );
    finish = (FINISH)(ULONG_PTR)GetProcAddress( module, "BCryptFinishHash" );
    destroy = (DESTROY)(ULONG_PTR)GetProcAddress( module, "BCryptDestroyHash" );
    close = (CLOSE)(ULONG_PTR)GetProcAddress( module, "BCryptCloseAlgorithmProvider" );
    if (!open || !create || !data || !finish || !destroy || !close)
    {
        FreeLibrary( module );
        return ERROR_PROC_NOT_FOUND;
    }
    status = open( &algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0 );
    if (!status) status = create( algorithm, &hash, NULL, 0, NULL, 0, 0 );
    if (!status) status = data( hash, (UCHAR *)publisher, wcslen(publisher) * sizeof(WCHAR), 0 );
    if (!status) status = finish( hash, digest, sizeof(digest), 0 );
    if (hash) destroy( hash );
    if (algorithm) close( algorithm, 0 );
    FreeLibrary( module );
    if (status) return RtlNtStatusToDosError( status );
    for (group = 0; group < 13; group++)
    {
        unsigned int value = 0;
        for (bit = group * 5; bit < group * 5 + 5; bit++)
            value = (value << 1) | (bit < 64 ? ((digest[bit / 8] >> (7 - bit % 8)) & 1) : 0);
        output[group] = alphabet[value];
    }
    output[13] = 0;
    return ERROR_SUCCESS;
}
