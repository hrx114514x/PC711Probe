#!/usr/bin/env python3
"""Extract bounded Issue 9 error-handler evidence from an exact matching Mach-O.

Reads an uncompressed x86_64 Mach-O or Apple kernel fileset. Never changes the
input or executes extracted code. The supplied 580-byte InitializePCI excerpt
must match exactly before any error-handler addresses are derived.
"""
import argparse
import hashlib
import json
import mmap
from pathlib import Path
import struct

MAGIC = 0xFEEDFACF
CPU_X86_64 = 0x01000007
LC_SEGMENT_64 = 0x19
LC_UUID = 0x1B
LC_FILESET_ENTRY = 0x80000035
MH_FILESET = 0xC
IDENTIFIER = "com.apple.iokit.IONVMeFamily"
EXPECTED_SHA256 = "2d83095db2702a949a21a953959b4012a0db8d5333a9d13e495f4707cce72a28"
FUNCTION_LENGTH = 580
CALLS = (("provider", 0x203), ("map", 0x20A),
         ("virtual_address", 0x211), ("mappers", 0x21B))


def checked(data, offset, length):
    if offset < 0 or length <= 0 or offset > len(data) - length:
        raise ValueError("byte range is outside the input")
    return data[offset:offset + length]


def header(data, offset):
    fields = struct.unpack("<8I", checked(data, offset, 32))
    if fields[0] != MAGIC or fields[1] != CPU_X86_64:
        raise ValueError("requires an uncompressed little-endian x86_64 Mach-O")
    return fields


def commands(data, offset):
    fields = header(data, offset)
    ncmds, size = fields[4:6]
    cursor = offset + 32
    if ncmds > size // 8 or size > len(data) - cursor:
        raise ValueError("invalid load-command bounds")
    end = cursor + size
    result = []
    for _ in range(ncmds):
        if cursor + 8 > end:
            raise ValueError("truncated load-command header")
        cmd, length = struct.unpack("<II", checked(data, cursor, 8))
        if length < 8 or length % 8 or cursor + length > end:
            raise ValueError("invalid load-command size")
        result.append((cursor, cmd, length))
        cursor += length
    if cursor != end:
        raise ValueError("load-command count and size disagree")
    return result


def entry(data):
    if header(data, 0)[3] != MH_FILESET:
        # Standalone input identity is established by the exact function hash.
        commands(data, 0)
        return 0
    matches = []
    for cursor, cmd, length in commands(data, 0):
        if cmd != LC_FILESET_ENTRY:
            continue
        if length < 32:
            raise ValueError("short fileset command")
        _, _, _, fileoff, nameoff, _ = struct.unpack("<IIQQII", checked(data, cursor, 32))
        if nameoff < 32 or nameoff >= length:
            raise ValueError("invalid fileset name offset")
        raw = checked(data, cursor + nameoff, length - nameoff)
        if b"\0" not in raw:
            raise ValueError("unterminated fileset name")
        name = raw.split(b"\0", 1)[0].decode("utf8")
        if name == IDENTIFIER:
            commands(data, fileoff)
            matches.append(fileoff)
    if len(matches) != 1:
        raise ValueError("expected exactly one IONVMeFamily fileset entry")
    return matches[0]


def translate(data, module, address, length):
    if address < 0 or address > 0xFFFFFFFFFFFFFFFF or length <= 0:
        raise ValueError("invalid virtual byte range")
    matches = []
    for cursor, cmd, size in commands(data, module):
        if cmd != LC_SEGMENT_64:
            continue
        if size < 72:
            raise ValueError("short segment command")
        fields = struct.unpack("<II16sQQQQiiII", checked(data, cursor, 72))
        _, _, name, vmaddr, vmsize, fileoff, filesize, _, initprot, sections, _ = fields
        if size != 72 + sections * 80 or filesize > vmsize:
            raise ValueError("invalid segment layout")
        delta = address - vmaddr
        if delta < 0 or delta >= vmsize:
            continue
        if not (initprot & 4):
            raise ValueError("expected an executable segment")
        if delta + length > vmsize or delta + length > filesize:
            raise ValueError("requested range crosses file-backed segment bytes")
        # Kernel fileset segment file offsets are collection-relative. Do not
        # guess a second interpretation when a malformed input fails validation.
        offset = fileoff + delta
        checked(data, offset, length)
        matches.append((offset, name.split(b"\0", 1)[0].decode("ascii")))
    if len(matches) != 1:
        raise ValueError("virtual range must have exactly one executable mapping")
    return matches[0]


def uuid(data, module):
    values = []
    for cursor, cmd, length in commands(data, module):
        if cmd == LC_UUID:
            if length != 24:
                raise ValueError("invalid UUID command")
            raw = checked(data, cursor + 8, 16).hex()
            values.append(f"{raw[:8]}-{raw[8:12]}-{raw[12:16]}-{raw[16:20]}-{raw[20:]}")
    if len(values) > 1:
        raise ValueError("multiple UUID commands")
    return values[0] if values else None


def extract(data, address):
    module = entry(data)
    offset, segment = translate(data, module, address, FUNCTION_LENGTH)
    body = checked(data, offset, FUNCTION_LENGTH)
    digest = hashlib.sha256(body).hexdigest()
    if digest != EXPECTED_SHA256:
        raise ValueError(f"InitializePCI fingerprint mismatch: {digest}; no handler extraction")
    windows = []
    for label, relative in CALLS:
        instruction = body[relative:relative + 5]
        if len(instruction) != 5 or instruction[0] != 0xE8:
            raise ValueError("expected an x86 direct relative call")
        target = address + relative + 5 + struct.unpack("<i", instruction[1:])[0]
        target_offset, target_segment = translate(data, module, target, 192)
        window = checked(data, target_offset, 192)
        windows.append(dict(candidate=label, call_offset=hex(relative),
                            address=hex(target), file_offset=hex(target_offset),
                            segment=target_segment, bytes=window.hex(),
                            sha256=hashlib.sha256(window).hexdigest()))
    return dict(schema=1, input_sha256=hashlib.sha256(data).hexdigest(),
                module_file_offset=hex(module), module_uuid=uuid(data, module),
                uuid_scope=("IONVMeFamily fileset entry" if header(data, 0)[3] == MH_FILESET
                            else "root Mach-O; not necessarily the NVMe bundle UUID"),
                initialize_pci_address=hex(address), initialize_pci_file_offset=hex(offset),
                initialize_pci_segment=segment, initialize_pci_sha256=digest,
                window_length=192, windows=windows,
                limitations="Bounded byte windows, not proven function extents. Candidate names follow the supplied report. Does not identify the branch taken at runtime.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--address", type=lambda x: int(x, 0), required=True,
                        help="unslid InitializePCI virtual address from the same image")
    parser.add_argument("--out", type=Path, required=True,
                        help="new output directory; existing directories are refused")
    args = parser.parse_args()
    with args.input.open("rb") as stream:
        if stream.seek(0, 2) == 0:
            raise ValueError("empty input")
        stream.seek(0)
        with mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
            result = extract(data, args.address)
    args.out.mkdir(parents=False, exist_ok=False)
    (args.out / "issue9-handler-evidence.json").write_text(
        json.dumps(result, indent=2) + "\n", encoding="utf8")
    print(f"Saved four bounded windows to {args.out}; no input files modified.")


if __name__ == "__main__":
    main()
