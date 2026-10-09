"""Read activation metadata only; never mounts or modifies the registry hive."""
import json
import hashlib
from pathlib import Path
import struct
import sys

SERVER = "Microsoft.Xbox.AppL.AppXhg7mrtrf7ayze6gb4syw3zwjgyr2xv4r.mca"


def query(path):
    data = Path(path).read_bytes()
    if data[:4] != b"regf" or data[4:8] != data[8:12]:
        raise ValueError("not a stable registry-hive snapshot")

    def integer(buffer, offset):
        return struct.unpack_from("<I", buffer, offset)[0]

    def cell(offset):
        address = 4096 + offset
        size = struct.unpack_from("<i", data, address)[0]
        if size >= -4 or address - size > len(data):
            raise ValueError("invalid/unallocated metadata cell")
        return data[address + 4:address - size]

    def subkeys(offset):
        item = cell(offset)
        kind, count = item[:2], struct.unpack_from("<H", item, 2)[0]
        if kind in (b"lf", b"lh"):
            return [integer(item, 4 + i * 8) for i in range(count)]
        if kind == b"li":
            return [integer(item, 4 + i * 4) for i in range(count)]
        if kind == b"ri":
            return [child for i in range(count)
                    for child in subkeys(integer(item, 4 + i * 4))]
        raise ValueError("unknown subkey index")

    def value(offset):
        item = cell(offset)
        if item[:2] != b"vk":
            raise ValueError("invalid value record")
        length = struct.unpack_from("<H", item, 2)[0]
        flags = struct.unpack_from("<H", item, 16)[0]
        name = item[20:20 + length].decode("latin1" if flags & 1 else "utf-16le")
        size, pointer, kind = struct.unpack_from("<III", item, 4)
        if size & 0x80000000:
            contents = item[8:12][:size & 0x7fffffff]
        else:
            contents = cell(pointer)[:size] if size else b""
        if kind in (1, 2, 7):
            contents = contents.decode("utf-16le").rstrip("\0")
        elif kind == 4 and len(contents) == 4:
            contents = integer(contents, 0)
        else:
            contents = None
        return name, contents, kind

    seen, records = set(), []

    def walk(offset, parent, depth):
        if offset in seen or depth > 64:
            raise ValueError("cyclic or oversized key hierarchy")
        seen.add(offset)
        item = cell(offset)
        if item[:2] != b"nk":
            raise ValueError("invalid key record")
        flags = struct.unpack_from("<H", item, 2)[0]
        length = struct.unpack_from("<H", item, 72)[0]
        name = item[76:76 + length].decode("latin1" if flags & 32 else "utf-16le")
        key = parent + "\\" + name
        count = integer(item, 36)
        values = {}
        kinds = {}
        if count:
            entries = cell(integer(item, 40))
            decoded = [value(integer(entries, i * 4)) for i in range(count)]
            values = {name: contents for name, contents, kind in decoded}
            kinds = {name: kind for name, contents, kind in decoded}
        if SERVER in key or "XboxPcApp.App" in key or SERVER in values.values():
            selected = {k: v for k, v in values.items() if isinstance(v, (str, int))}
            records.append({"key": key, "values": selected,
                            "value_types": {name: kinds[name] for name in selected},
                            "all_value_types": kinds})
        if integer(item, 20):
            children = subkeys(integer(item, 28))
            if len(children) != integer(item, 20):
                raise ValueError("subkey count mismatch")
            for child in children:
                walk(child, key, depth + 1)

    walk(integer(data, 36), "", 0)
    return {"server": SERVER, "keys_checked": len(seen),
            "activation_store_sha256": hashlib.sha256(data).hexdigest(),
            "matching_records": records}


if __name__ == "__main__":
    print(json.dumps(query(sys.argv[1]), indent=2))
