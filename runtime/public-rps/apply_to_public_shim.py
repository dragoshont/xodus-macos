# SPDX-License-Identifier: GPL-3.0-only
"""Apply the isolated client to an exact, caller-selected public shim checkout."""

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys


COMMIT = "44d97de1e084be61243323062ed49e307d28728c"
BLOBS = {
    "xuser.c": "4d3d374e8c580b5a82e7c607d83b48dc9cebfa11",
    "util.c": "b405b9bfce563c80602246d58e1f6038fde276f4",
    "Makefile.in": "6f57c75086687d6f981f1fe66e65c4b2b6471769",
}
OLD_RPS = """static HRESULT get_rps_tickets( BOOLEAN allowUi, char **userTicket, char **deviceTicket )
{
    FIXME( "allowUi %d, userTicket %p, deviceTicket %p stub!\\n", allowUi, userTicket, deviceTicket );
    return E_NOTIMPL;
}"""
NEW_RPS = """static HRESULT get_rps_tickets( BOOLEAN allowUi, char **userTicket, char **deviceTicket )
{
    WCHAR configured[108];
    char endpoint[108];
    DWORD length;
    enum xodus_rps_result result;

    if (!userTicket || !deviceTicket || userTicket == deviceTicket) return E_INVALIDARG;
    *userTicket = *deviceTicket = NULL;
    length = GetEnvironmentVariableW( L"XODUS_RUNTIME_SOCKET", configured, ARRAY_SIZE(configured) );
    if (!length || length >= ARRAY_SIZE(configured))
        return HRESULT_FROM_WIN32( ERROR_BAD_CONFIGURATION );
    if (!WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS, configured, -1,
                             endpoint, sizeof(endpoint), NULL, NULL ))
        return HRESULT_FROM_WIN32( GetLastError() );
    result = xodus_rps_get( endpoint, msaAppId, allowUi != FALSE, fullTrust != FALSE,
                           40000, userTicket, deviceTicket );
    switch (result)
    {
    case XODUS_RPS_OK: return S_OK;
    case XODUS_RPS_CONFIGURATION: return HRESULT_FROM_WIN32( ERROR_BAD_CONFIGURATION );
    case XODUS_RPS_TIMEOUT: return HRESULT_FROM_WIN32( ERROR_TIMEOUT );
    case XODUS_RPS_PROTOCOL: return E_UNEXPECTED;
    case XODUS_RPS_MEMORY: return E_OUTOFMEMORY;
    case XODUS_RPS_TRANSPORT: return E_FAIL;
    }
    return E_UNEXPECTED;
}"""


def git_blob(data: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()


def replace_once(source: str, before: str, after: str) -> str:
    if source.count(before) != 1:
        raise ValueError("Pinned source replacement was not unique.")
    return source.replace(before, after, 1)


def prepare(source: Path) -> dict[str, bytes]:
    head = subprocess.run(
        ["git", "-C", str(source), "rev-parse", "HEAD"],
        check=True, text=True, capture_output=True,
    ).stdout.strip()
    if head != COMMIT:
        raise ValueError("Refusing a checkout other than the pinned public development commit.")
    files = {}
    for name, expected in BLOBS.items():
        path = source / name
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"Refusing a missing or symbolic source file: {name}")
        data = path.read_bytes()
        if git_blob(data) != expected:
            raise ValueError(f"Refusing a changed source blob: {name}")
        files[name] = data.decode("utf-8")
    for name in ("xodus_rps.c", "xodus_rps.h"):
        if (source / name).exists() or (source / name).is_symlink():
            raise ValueError(f"Refusing to overwrite an existing client file: {name}")
    files["xuser.c"] = replace_once(files["xuser.c"], '#include "util.h"', '#include "util.h"\n#include "xodus_rps.h"')
    files["xuser.c"] = replace_once(files["xuser.c"], OLD_RPS, NEW_RPS)
    files["xuser.c"] = replace_once(
        files["xuser.c"],
        'TRACE( "json %s, object %p.\\n", debugstr_an( json, jsonLen ), object );',
        'TRACE( "json length %Iu, object %p.\\n", jsonLen, object );',
    )
    files["util.c"] = replace_once(
        files["util.c"],
        'TRACE( "method %s, url %s, data %s, headers %s, accept %p, buffer %p, bufferSize %p.\\n",\n'
        '           debugstr_w( method ), debugstr_w( url ), debugstr_a( data ), debugstr_w( headers ), accept, buffer, bufferSize );',
        'TRACE( "method %s, url %s, data %p, headers %p, accept %p, buffer %p, bufferSize %p.\\n",\n'
        '           debugstr_w( method ), debugstr_w( url ), data, headers, accept, buffer, bufferSize );',
    )
    files["Makefile.in"] = replace_once(
        files["Makefile.in"], "IMPORTS   = bcrypt combase shlwapi winhttp wininet $(XML2_PE_LIBS)",
        "IMPORTS   = bcrypt combase shlwapi winhttp wininet ws2_32 $(XML2_PE_LIBS)",
    )
    files["Makefile.in"] = replace_once(
        files["Makefile.in"], "\txuser.c \\", "\txodus_rps.c \\\n\txuser.c \\",
    )
    result = {name: data.encode("utf-8") for name, data in files.items()}
    for name in ("xodus_rps.c", "xodus_rps.h"):
        result[name] = (Path(__file__).parent / name).read_bytes()
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Explicit disposable public xgameruntime checkout")
    parser.add_argument("--apply", action="store_true", help="Write the verified patch; default is read-only")
    args = parser.parse_args()
    source = args.source.resolve(strict=True)
    changes = prepare(source)
    if args.apply:
        for name, data in changes.items():
            (source / name).write_bytes(data)
    print(f"{'Applied' if args.apply else 'Validated'} isolated client against public {COMMIT}; "
          "no runtime or account operation performed.")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"Public shim patch refused: {error}", file=sys.stderr)
        sys.exit(1)
