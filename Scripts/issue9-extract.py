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
LC_SYMTAB = 0x2
LC_FILESET_ENTRY = 0x80000035
MH_FILESET = 0xC
IDENTIFIER = "com.apple.iokit.IONVMeFamily"
EXPECTED_SHA256 = "2d83095db2702a949a21a953959b4012a0db8d5333a9d13e495f4707cce72a28"
FUNCTION_LENGTH = 580
CALLS = (("provider", 0x203), ("map", 0x20A),
         ("virtual_address", 0x211), ("mappers", 0x21B))
# Independently hash-checked windows supplied in Issue #9 on 2026-10-18.
HANDLER_SHA256 = {
    "provider": "992ce46646dfdab44b7a1a613b1d2b37d9a18ad9bb4bfa0709955e77899bd876",
    "map": "942bf569fef6f8af3dfdd6aebea696deae15a870c88e93456da61afd8746943a",
    "virtual_address": "6c3bda8f2940de5d999f0e03bd929e07243ccf59bf075dd0ef6537e1bf478d41",
    "mappers": "6e629df8c055a323af5fa86037a7d81b9d239b14de5eb615cbaea9114d0d501e",
}
SYMBOL_WINDOWS = {
    "__ZN16IONVMeController12GetIOMappersEv": (2048, 4),
    "__ZN16IONVMeController5startEP9IOService": (4096, 4),
    "__ZN16IONVMeController14InitializeNVMeEb": (4096, 4),
    "__ZN16IONVMeController21CreateDeviceInterruptEPFvP8OSObjectP22IOInterruptEventSourceiEPFbS1_P28IOFilterInterruptEventSourceEP9IOService": (2048, 4),
    "__ZTV16IONVMeController": (3072, 1),
}


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


def translate(data, module, address, length, protection=4):
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
        if (initprot & protection) != protection:
            raise ValueError("expected an executable segment" if protection == 4
                             else "expected a readable segment")
        if delta + length > vmsize or delta + length > filesize:
            raise ValueError("requested range crosses file-backed segment bytes")
        # Kernel fileset segment file offsets are collection-relative. Do not
        # guess a second interpretation when a malformed input fails validation.
        offset = fileoff + delta
        checked(data, offset, length)
        matches.append((offset, name.split(b"\0", 1)[0].decode("ascii")))
    if len(matches) != 1:
        raise ValueError("virtual range must have exactly one eligible mapping")
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


def code_window(data, module, address, length, protection=4):
    offset, segment = translate(data, module, address, length, protection)
    raw = checked(data, offset, length)
    return dict(address=hex(address), file_offset=hex(offset), segment=segment,
                length=length, bytes=raw.hex(), sha256=hashlib.sha256(raw).hexdigest())


def string_window(data, module, address, limit=512):
    if limit < 1 or limit > 4096:
        raise ValueError("invalid string limit")
    raw = bytearray()
    reason = "limit reached"
    terminated = False
    for index in range(limit):
        try:
            offset, _ = translate(data, module, address + index, 1, 1)
        except ValueError as error:
            reason = str(error)
            break
        value = data[offset]
        raw.append(value)
        if value == 0:
            terminated = True
            reason = "NUL terminator"
            break
    text_bytes = raw[:-1] if terminated else raw
    return dict(address=hex(address), bytes=raw.hex(), terminated=terminated,
                text=text_bytes.decode("utf8", errors="replace"), stop_reason=reason)


def symbol_windows(data, module):
    result = {name: {"status": "not found"} for name in SYMBOL_WINDOWS}
    tables = [(cursor, length) for cursor, cmd, length in commands(data, module)
              if cmd == LC_SYMTAB]
    if not tables:
        return {name: {"status": "no LC_SYMTAB"} for name in SYMBOL_WINDOWS}
    if len(tables) != 1 or tables[0][1] != 24:
        raise ValueError("invalid symbol-table command")
    _, _, symoff, count, stroff, strsize = struct.unpack("<6I", checked(data, tables[0][0], 24))
    if count > 1000000:
        raise ValueError("symbol count exceeds extraction limit")
    if count:
        checked(data, symoff, count * 16)
    if strsize:
        checked(data, stroff, strsize)
    wanted = {name.encode(): name for name in SYMBOL_WINDOWS}
    found = {name: set() for name in SYMBOL_WINDOWS}
    for index in range(count):
        strx, kind, _, _, value = struct.unpack("<IBBHQ", checked(data, symoff + index * 16, 16))
        if kind & 0xE0 or (kind & 0x0E) != 0x0E or not value:
            continue
        if strx >= strsize:
            raise ValueError("invalid symbol string index")
        raw = checked(data, stroff + strx, min(1024, strsize - strx))
        end = raw.find(b"\0")
        if end < 0:
            raise ValueError("unterminated or oversized symbol name")
        name = wanted.get(bytes(raw[:end]))
        if name:
            found[name].add(value)
    for name, addresses in found.items():
        if len(addresses) > 1:
            result[name] = {"status": "ambiguous", "addresses": [hex(v) for v in sorted(addresses)]}
        elif addresses:
            address = next(iter(addresses))
            length, protection = SYMBOL_WINDOWS[name]
            try:
                result[name] = dict(status="captured", **code_window(data, module, address, length, protection))
            except ValueError as error:
                result[name] = {"status": "unmapped or truncated", "address": hex(address), "error": str(error)}
    return result


def extract_details(data, module, windows):
    arguments = []
    addresses = set()
    reporters = set()
    for window in windows:
        label = window["candidate"]
        raw = bytes.fromhex(window["bytes"])
        if hashlib.sha256(raw).hexdigest() != HANDLER_SHA256[label]:
            raise ValueError(f"{label} handler fingerprint mismatch; no detailed extraction")
        address = int(window["address"], 16)
        extra = 1 if label == "mappers" else 0
        refs = {}
        for reg, offset, prefix in (("rdi", 12, b"\x48\x8d\x3d"),
                                    ("rsi", 19, b"\x48\x8d\x35"),
                                    ("rdx", 26, b"\x48\x8d\x15"),
                                    ("r8", 33, b"\x4c\x8d\x05")):
            offset += extra
            if raw[offset:offset + 3] != prefix:
                raise ValueError("unexpected RIP-relative argument instruction")
            target = address + offset + 7 + struct.unpack("<i", raw[offset + 3:offset + 7])[0]
            refs[reg] = hex(target)
            addresses.add(target)
        lineoff = 40 + extra
        if raw[lineoff:lineoff + 2] != b"\x41\xb9":
            raise ValueError("unexpected r9d immediate instruction")
        immediate = struct.unpack("<I", raw[lineoff + 2:lineoff + 6])[0]
        calloff = 48 + extra
        if raw[calloff] != 0xE8:
            raise ValueError("unexpected reporting call")
        reporter = address + calloff + 5 + struct.unpack("<i", raw[calloff + 1:calloff + 5])[0]
        reporters.add(reporter)
        arguments.append(dict(candidate=label, rip_arguments=refs, r9d=immediate,
                              common_callee=hex(reporter)))
    if len(reporters) != 1:
        raise ValueError("handler callees disagree")
    try:
        symbols = symbol_windows(data, module)
    except ValueError as error:
        symbols = {"error": str(error)}
    return dict(handler_arguments=arguments,
                referenced_strings=[string_window(data, module, value) for value in sorted(addresses)],
                common_callee_window=code_window(data, module, next(iter(reporters)), 1024),
                optional_symbol_windows=symbols,
                limitations="r9d is a raw immediate, not yet a proven source line. Optional windows are not function extents. No runtime failing branch is inferred.")


def extract(data, address, details=False):
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
    result = dict(schema=2 if details else 1, input_sha256=hashlib.sha256(data).hexdigest(),
                module_file_offset=hex(module), module_uuid=uuid(data, module),
                uuid_scope=("IONVMeFamily fileset entry" if header(data, 0)[3] == MH_FILESET
                            else "root Mach-O; not necessarily the NVMe bundle UUID"),
                initialize_pci_address=hex(address), initialize_pci_file_offset=hex(offset),
                initialize_pci_segment=segment, initialize_pci_sha256=digest,
                window_length=192, windows=windows,
                limitations="Bounded byte windows, not proven function extents. Candidate names follow the supplied report. Does not identify the branch taken at runtime.")
    if details:
        result["details"] = extract_details(data, module, windows)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--address", type=lambda x: int(x, 0), required=True,
                        help="unslid InitializePCI virtual address from the same image")
    parser.add_argument("--out", type=Path, required=True,
                        help="new output directory; existing directories are refused")
    parser.add_argument("--details", action="store_true",
                        help="verify the supplied handler fingerprints, then collect referenced strings, common callee, and optional symbol windows")
    args = parser.parse_args()
    with args.input.open("rb") as stream:
        if stream.seek(0, 2) == 0:
            raise ValueError("empty input")
        stream.seek(0)
        with mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
            result = extract(data, args.address, args.details)
    args.out.mkdir(parents=False, exist_ok=False)
    filename = "issue9-handler-details.json" if args.details else "issue9-handler-evidence.json"
    (args.out / filename).write_text(
        json.dumps(result, indent=2) + "\n", encoding="utf8")
    print(f"Saved {filename} to {args.out}; no input files modified.")


if __name__ == "__main__":
    main()
