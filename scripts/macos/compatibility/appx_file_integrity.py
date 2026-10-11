"""Verify extracted AppX plaintext against its signature-bound BlockMap."""

import base64
import hashlib

BLOCK = "{http://schemas.microsoft.com/appx/2010/blockmap}Block"
FILE_HASH = "{http://schemas.microsoft.com/appx/2015/blockmap}FileHash"


def decode_hash(value):
    result = base64.b64decode(value, validate=True)
    if len(result) != 32:
        raise ValueError("BlockMap SHA256 must contain 32 bytes")
    return result


def verify_plaintext(path, item):
    if path.stat().st_size != int(item.attrib["Size"]):
        raise ValueError("BlockMap file size mismatch: " + item.attrib["Name"])
    encrypted = item.get("Encrypted", "false")
    if encrypted not in ("true", "false", "1", "0"):
        raise ValueError("invalid BlockMap encryption flag")
    file_hashes = item.findall(FILE_HASH)
    if len(file_hashes) > 1:
        raise ValueError("duplicate BlockMap plaintext FileHash")
    if encrypted in ("true", "1") and path.stat().st_size and not file_hashes:
        raise ValueError("encrypted file lacks signed plaintext FileHash")
    plaintext_hash = hashlib.sha256()
    with path.open("rb") as stream:
        if encrypted in ("true", "1"):
            # EAppx Block hashes bind ciphertext, not the extracted plaintext.
            for chunk in iter(lambda: stream.read(65536), b""):
                plaintext_hash.update(chunk)
        else:
            for block in item.findall(BLOCK):
                chunk = stream.read(65536)
                if hashlib.sha256(chunk).digest() != decode_hash(block.attrib["Hash"]):
                    raise ValueError("BlockMap plaintext block mismatch")
                plaintext_hash.update(chunk)
            if stream.read(1):
                raise ValueError("BlockMap omitted plaintext bytes")
    if file_hashes and plaintext_hash.digest() != decode_hash(file_hashes[0].attrib["Hash"]):
        raise ValueError("BlockMap plaintext FileHash mismatch")
