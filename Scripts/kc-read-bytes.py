#!/usr/bin/env python3
"""Read bytes at a virtual address from one Mach-O fileset entry."""

import argparse
import struct
import sys


MH_MAGIC_64 = 0xFEEDFACF
LC_SEGMENT_64 = 0x19
LC_FILESET_ENTRY = 0x80000035


def header(data: bytes, offset: int) -> tuple[int, int]:
    magic, _, _, _, ncmds, sizeofcmds, _, _ = struct.unpack_from(
        "<IiiIIIII", data, offset
    )
    if magic != MH_MAGIC_64:
        raise ValueError(f"no 64-bit Mach-O header at file offset 0x{offset:x}")
    return ncmds, sizeofcmds


def commands(data: bytes, offset: int):
    ncmds, sizeofcmds = header(data, offset)
    cursor = offset + 32
    end = cursor + sizeofcmds
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", data, cursor)
        if cmdsize < 8 or cursor + cmdsize > end:
            raise ValueError(f"invalid load command at 0x{cursor:x}")
        yield cursor, cmd, cmdsize
        cursor += cmdsize


def c_string(data: bytes, offset: int, limit: int) -> str:
    end = data.find(b"\0", offset, limit)
    if end < 0:
        raise ValueError("unterminated fileset entry identifier")
    return data[offset:end].decode("utf-8")


def find_entry(data: bytes, identifier: str) -> int:
    for cursor, cmd, cmdsize in commands(data, 0):
        if cmd != LC_FILESET_ENTRY:
            continue
        _, _, _, fileoff, entry_id_offset, _ = struct.unpack_from(
            "<IIQQII", data, cursor
        )
        entry_id = c_string(data, cursor + entry_id_offset, cursor + cmdsize)
        if entry_id == identifier:
            return fileoff
    raise ValueError(f"fileset entry not found: {identifier}")


def virtual_to_file(data: bytes, entry_offset: int, address: int) -> tuple[int, str]:
    for cursor, cmd, _ in commands(data, entry_offset):
        if cmd != LC_SEGMENT_64:
            continue
        _, _, raw_name, vmaddr, vmsize, fileoff, filesize, _, _, _, _ = struct.unpack_from(
            "<II16sQQQQiiII", data, cursor
        )
        if vmaddr <= address < vmaddr + vmsize:
            candidate = fileoff + (address - vmaddr)
            if candidate >= len(data) and entry_offset + candidate < len(data):
                candidate += entry_offset
            if candidate >= len(data):
                raise ValueError("translated file offset is outside the collection")
            return candidate, raw_name.split(b"\0", 1)[0].decode("ascii")
    raise ValueError(f"address 0x{address:x} is not mapped by the entry")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("collection")
    parser.add_argument("identifier")
    parser.add_argument("address", type=lambda value: int(value, 0))
    parser.add_argument("length", type=lambda value: int(value, 0))
    parser.add_argument(
        "--format",
        choices=("hex", "asm"),
        default="hex",
        help="print hexadecimal bytes or an assembler source fragment",
    )
    args = parser.parse_args()

    with open(args.collection, "rb") as stream:
        data = stream.read()

    entry_offset = find_entry(data, args.identifier)
    file_offset, segment = virtual_to_file(data, entry_offset, args.address)
    result = data[file_offset:file_offset + args.length]
    if len(result) != args.length:
        raise ValueError("requested byte range exceeds the collection")

    print(
        f"entry_fileoff=0x{entry_offset:x} segment={segment} "
        f"translated_fileoff=0x{file_offset:x}",
        file=sys.stderr,
    )
    if args.format == "asm":
        print(".text")
        print(".globl _kc_bytes")
        print("_kc_bytes:")
        for offset in range(0, len(result), 16):
            chunk = result[offset:offset + 16]
            print(".byte " + ", ".join(f"0x{byte:02x}" for byte in chunk))
    else:
        print(" ".join(f"0x{byte:02x}" for byte in result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
