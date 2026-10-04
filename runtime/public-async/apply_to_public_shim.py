# SPDX-License-Identifier: GPL-3.0-only
"""Replace public async stubs only after the existing, exact RPS overlay is present."""

import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import sys


COMMIT = "44d97de1e084be61243323062ed49e307d28728c"
BLOBS = {
    "xthreading.c": "6d62fe1084639954311f347785787ffa64aaad42",
    "xuser.c": "679127a722557eea6c01e90b62b48e963772576c",
    "Makefile.in": "e78342f99f3d8ec7b3d46cf839ad70a89ea2c664",
}


def replace_once(text, before, after):
    if text.count(before) != 1:
        raise ValueError("Pinned public replacement is not unique.")
    return text.replace(before, after, 1)


def forward(match):
    result, name, signature, body = match.groups()
    if "stub!" not in body or not name.startswith(("XAsync", "XTaskQueue")):
        raise ValueError("Refusing a non-stub threading implementation.")
    parameters = signature.split(", ")
    if parameters[0] != "IXThreadingImpl *iface":
        raise ValueError("Unexpected public COM receiver.")
    types, names = [], []
    for parameter in parameters[1:]:
        parsed = re.fullmatch(r"(.+?)([a-zA-Z_]\w*)", parameter.strip())
        if not parsed:
            raise ValueError("Unexpected pinned parameter declaration.")
        types.append(parsed.group(1).strip())
        names.append(parsed.group(2))
    failure = {"HRESULT": "return hr;", "void": "return;", "BOOLEAN": "return FALSE;"}[result]
    invocation = f"function({', '.join(names)})"
    statement = invocation + ";" if result == "void" else "return " + invocation + ";"
    return (
        f"static {result} WINAPI x_threading_{name}( {signature} )\n"
        "{\n"
        f"    typedef {result} (WINAPI *entry)({', '.join(types)});\n"
        "    entry function;\n"
        '    _Static_assert(sizeof(function) == sizeof(FARPROC), "Unexpected Windows function representation");\n'
        "    FARPROC procedure;\n"
        "    HRESULT hr;\n\n"
        "    (void)iface;\n"
        f'    if (FAILED(hr = xodus_async_get_proc("{name}", &procedure))) {failure}\n'
        "    memcpy(&function, &procedure, sizeof(function));\n"
        f"    {statement}\n"
        "}"
    )


def prepare(source):
    supplied = source.absolute()
    if any(path.is_symlink() for path in (supplied, *supplied.parents)):
        raise ValueError("Select a non-aliased public shim checkout.")
    source = supplied.resolve(strict=True)
    head = subprocess.run(
        ["git", "-C", str(source), "rev-parse", "HEAD"],
        check=True, capture_output=True, text=True,
    ).stdout.strip()
    if head != COMMIT:
        raise ValueError("Refusing a different public shim revision.")
    files = {}
    for name, expected in BLOBS.items():
        path = source / name
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"Missing or aliased public source: {name}")
        data = path.read_bytes()
        blob = hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()
        if blob != expected:
            raise ValueError(f"Expected the exact public/RPS-overlay source: {name}")
        files[name] = data.decode("utf-8")
    for name in ("xodus_async_bridge.c", "xodus_async_bridge.h"):
        if (source / name).exists() or (source / name).is_symlink():
            raise ValueError(f"Refusing an existing bridge file: {name}")
    pattern = (
        r"static (HRESULT|void|BOOLEAN) WINAPI x_threading_(X(?:Async|TaskQueue)\w+)"
        r"\( (.*?) \)\n\{\n(.*?)\n\}"
    )
    threading, count = re.subn(pattern, forward, files["xthreading.c"], flags=re.S)
    if count != 23:
        raise ValueError("The pinned threading stub count changed.")
    files["xthreading.c"] = replace_once(
        threading, '#include "private.h"',
        '#include "private.h"\n#include <string.h>\n#include "xodus_async_bridge.h"',
    )
    before = (
        '    if (FAILED(hr = http_request( L"GET", L"https://title.mgt.xboxlive.com/titles/default/endpoints?type=1", NULL, NULL, ACCEPT_JSON, &defaultBuffer, &size ))) return hr;\n'
        '    if (FAILED(hr = load_endpoints( impl, defaultBuffer, size ))) goto cleanup;\n'
        '    if (FAILED(hr = get_rps_tickets( options & XUserAddOptions_AddDefaultUserAllowingUI, &userTicket, &deviceTicket ))) goto cleanup;'
    )
    after = (
        '    if (FAILED(hr = get_rps_tickets( options & XUserAddOptions_AddDefaultUserAllowingUI, &userTicket, &deviceTicket ))) goto cleanup;\n'
        '    if (FAILED(hr = http_request( L"GET", L"https://title.mgt.xboxlive.com/titles/default/endpoints?type=1", NULL, NULL, ACCEPT_JSON, &defaultBuffer, &size ))) goto cleanup;\n'
        '    if (FAILED(hr = load_endpoints( impl, defaultBuffer, size ))) goto cleanup;'
    )
    files["xuser.c"] = replace_once(files["xuser.c"], before, after)
    files["xuser.c"] = replace_once(
        files["xuser.c"],
        "            free( impl->endpoints );\n        }\n    }\n    return ref;",
        "            free( impl->endpoints );\n        }\n        free( impl );\n    }\n    return ref;",
    )
    files["xuser.c"] = replace_once(
        files["xuser.c"],
        "            memcpy( data->buffer, &context->user, sizeof(XUserHandle) );\n            break;",
        "            memcpy( data->buffer, &context->user, sizeof(XUserHandle) );\n"
        "            context->user = NULL;\n            break;",
    )
    files["xuser.c"] = replace_once(
        files["xuser.c"],
        "            IXThreadingImpl_XAsyncComplete( xthreading, data->async, hr, SUCCEEDED(hr) ? sizeof(XUserHandle) : 0 );\n"
        "            if (FAILED(hr) && context->user) IUser_Release( &context->user->IUser_iface );",
        "            if (FAILED(hr) && context->user)\n"
        "            {\n"
        "                IUser_Release( &context->user->IUser_iface );\n"
        "                context->user = NULL;\n"
        "            }\n"
        "            IXThreadingImpl_XAsyncComplete( xthreading, data->async, hr, SUCCEEDED(hr) ? sizeof(XUserHandle) : 0 );",
    )
    files["xuser.c"] = replace_once(
        files["xuser.c"],
        "        case XAsyncOp_Cleanup:\n            free( context );",
        "        case XAsyncOp_Cleanup:\n"
        "            if (context->user) IUser_Release( &context->user->IUser_iface );\n"
        "            free( context );",
    )
    files["Makefile.in"] = replace_once(
        files["Makefile.in"], "\txodus_rps.c \\", "\txodus_async_bridge.c \\\n\txodus_rps.c \\",
    )
    result = {name: text.encode("utf-8") for name, text in files.items()}
    for name in ("xodus_async_bridge.c", "xodus_async_bridge.h"):
        result[name] = (Path(__file__).parent / name).read_bytes()
    return result


def check_header(threading):
    pattern = (
        r"static (HRESULT|void|BOOLEAN) WINAPI x_threading_(X(?:Async|TaskQueue)\w+)"
        r"\( (.*?) \)\n\{\n(.*?)\n\}"
    )
    wrappers = []
    for result, name, signature, _ in re.findall(pattern, threading, flags=re.S):
        parameters = signature.split(", ")[1:]
        names = [re.fullmatch(r"(.+?)([a-zA-Z_]\w*)", value.strip()).group(2)
                 for value in parameters]
        prefix = "" if result == "void" else "return "
        wrappers.append(
            f"[[maybe_unused]] static {result} WINAPI {name}({', '.join(parameters)})\n"
            "{\n"
            f"    {prefix}IXThreadingImpl_{name}(threading, {', '.join(names)});\n"
            "}\n"
        )
    if len(wrappers) != 23:
        raise ValueError("Unexpected pinned public COM method count.")
    return (
        "/* Generated from the exact public COM declarations; not a replacement scheduler. */\n"
        "#define CINTERFACE\n#define COBJMACROS\n#define __WINESRC__\n"
        "#include <initguid.h>\n#include \"xasyncprovider.h\"\n"
        "static IXThreadingImpl *threading;\n" + "\n".join(wrappers)
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    changes = prepare(args.source)
    if args.apply:
        for name, data in changes.items():
            (args.source / name).write_bytes(data)
    print(f"{'Applied' if args.apply else 'Validated'} public async bridge; no runtime or account operation performed.")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"Public async bridge refused: {error}", file=sys.stderr)
        sys.exit(1)
