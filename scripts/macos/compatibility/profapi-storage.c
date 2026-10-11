/* Experimental ordinal 114 compatibility, based on native Windows probes.
 * Opens existing registry state only; creates no AppContainer or package state. */
#include <windows.h>
#include <wchar.h>

HRESULT WINAPI ProfApiOpenAppContainerStorageKeyW( const WCHAR *package, const WCHAR *child,
                                                  const WCHAR *subkey, REGSAM access, HKEY *result )
{
    static const WCHAR prefix[] =
        L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\"
        L"CurrentVersion\\AppContainer\\Storage\\";
    static const WCHAR children[] = L"\\Children\\";
    HKEY user;
    WCHAR *path;
    SIZE_T length;
    LONG status;

    if (!package) return E_INVALIDARG;
    length = wcslen(prefix) + wcslen(package);
    if (child) length += wcslen(children) + wcslen(child);
    if (subkey) length += 1 + wcslen(subkey);
    if (length >= ~(SIZE_T)0 / sizeof(WCHAR)) return E_OUTOFMEMORY;
    if ((status = RegOpenCurrentUser( KEY_READ, &user ))) return HRESULT_FROM_WIN32( status );
    if (!(path = HeapAlloc( GetProcessHeap(), 0, (length + 1) * sizeof(WCHAR) )))
    {
        RegCloseKey( user );
        return E_OUTOFMEMORY;
    }
    wcscpy( path, prefix );
    wcscat( path, package );
    if (child) { wcscat( path, children ); wcscat( path, child ); }
    if (subkey) { wcscat( path, L"\\" ); wcscat( path, subkey ); }
    status = RegOpenKeyExW( user, path, 0, access, result );
    HeapFree( GetProcessHeap(), 0, path );
    RegCloseKey( user );
    return HRESULT_FROM_WIN32( status );
}
