import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("issue9_extract", ROOT / "Scripts/issue9-extract.py")
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)
ADDRESS = 0xFFFFFF80025F5AA4
VM = 0xFFFFFF80025F0000
FILE = 0x1000
BODY = bytes.fromhex((ROOT / "Tests/fixtures/InitializePCI-580.hex").read_text())
HANDLERS = json.loads((ROOT / "Tests/fixtures/Issue9-handlers-20261018.json").read_text())
STRING_ADDRESSES = (0xFFFFFF8002614C61, 0xFFFFFF8002614C79, 0xFFFFFF8002615E85,
                    0xFFFFFF8002616578, 0xFFFFFF80026166AD, 0xFFFFFF80026166C3,
                    0xFFFFFF80026166CE, 0xFFFFFF80026166E4, 0xFFFFFF800261675B)


def image(fileset=True, extra_segment=False):
    data = bytearray(0x22000)
    module = 0x400 if fileset else 0
    segment = struct.pack("<II16sQQQQiiII", 0x19, 72, b"__TEXT_EXEC", VM, 0x20000,
                          FILE, 0x20000, 5, 5, 0, 0)
    commands = segment + struct.pack("<II16s", 0x1B, 24, bytes(range(16)))
    if extra_segment:
        commands += segment
    head = struct.pack("<8I", tool.MAGIC, tool.CPU_X86_64, 3, 0xB,
                       3 if extra_segment else 2, len(commands), 0, 0)
    data[module:module + 32 + len(commands)] = head + commands
    if fileset:
        name = tool.IDENTIFIER.encode() + b"\0"
        size = (32 + len(name) + 7) // 8 * 8
        cmd = struct.pack("<IIQQII", tool.LC_FILESET_ENTRY, size, VM, module, 32, 0)
        cmd = (cmd + name).ljust(size, b"\0")
        data[:32 + size] = struct.pack("<8I", tool.MAGIC, tool.CPU_X86_64, 3,
                                     tool.MH_FILESET, 1, size, 0, 0) + cmd
    pos = FILE + ADDRESS - VM
    data[pos:pos + len(BODY)] = BODY
    return data


def details_image():
    """Real supplied code windows in a synthetic image with synthetic strings."""
    data = image()
    data.extend(bytes(0x40000 - len(data)))
    for window in HANDLERS["windows"]:
        pos = FILE + int(window["address"], 16) - VM
        raw = bytes.fromhex(window["bytes"])
        data[pos:pos + len(raw)] = raw
    # Separate readable, non-executable segment proves strings are not forced
    # through the executable-only code mapping path.
    segment = struct.pack("<II16sQQQQiiII", 0x19, 72, b"__CONST", VM + 0x20000,
                          0x10000, 0x23000, 0x10000, 1, 1, 0, 0)
    data[0x480:0x4C8] = segment
    struct.pack_into("<II", data, 0x400 + 16, 3, 168)
    for index, address in enumerate(STRING_ADDRESSES):
        pos = 0x23000 + address - (VM + 0x20000)
        text = f"T{index}".encode() + b"\0"
        data[pos:pos + len(text)] = text
    return data


def add_symbols(data, symbols):
    strings = bytearray(b"\0")
    records = bytearray()
    for name, value in symbols:
        index = len(strings)
        strings += name.encode() + b"\0"
        records += struct.pack("<IBBHQ", index, 0x0F, 1, 0, value)
    data[0x35000:0x35000 + len(records)] = records
    data[0x36000:0x36000 + len(strings)] = strings
    data[0x4C8:0x4E0] = struct.pack("<6I", 2, 24, 0x35000, len(symbols), 0x36000, len(strings))
    struct.pack_into("<II", data, 0x400 + 16, 4, 192)
    return data


class ExtractTests(unittest.TestCase):
    def test_fixture_identity(self):
        self.assertEqual(len(BODY), 580)
        self.assertEqual(hashlib.sha256(BODY).hexdigest(), tool.EXPECTED_SHA256)

    def test_fileset_exact_match_and_no_input_mutation(self):
        data = image()
        before = bytes(data)
        result = tool.extract(data, ADDRESS)
        self.assertEqual(data, before)
        self.assertEqual(len(result["windows"]), 4)
        self.assertEqual(result["module_file_offset"], "0x400")
        self.assertEqual(result["module_uuid"], "00010203-0405-0607-0809-0a0b0c0d0e0f")
        self.assertEqual([int(w["address"], 16) - ADDRESS for w in result["windows"]],
                         [0x15B32, 0x15AF7, 0x15ABC, 0x15A65])

    def test_standalone(self):
        result = tool.extract(image(False), ADDRESS)
        self.assertEqual(result["module_file_offset"], "0x0")
        self.assertIn("not necessarily", result["uuid_scope"])

    def test_fingerprint_mismatch(self):
        data = image()
        data[FILE + ADDRESS - VM] ^= 1
        with self.assertRaisesRegex(ValueError, "fingerprint mismatch"):
            tool.extract(data, ADDRESS)

    def test_wrong_address(self):
        with self.assertRaisesRegex(ValueError, "fingerprint mismatch"):
            tool.extract(image(), ADDRESS + 1)

    def test_non_macho(self):
        with self.assertRaises(ValueError):
            tool.extract(bytes(64), ADDRESS)

    def test_wrong_architecture(self):
        data = image()
        struct.pack_into("<I", data, 4, 0x0100000C)
        with self.assertRaises(ValueError):
            tool.extract(data, ADDRESS)

    def test_truncated_header(self):
        with self.assertRaises(ValueError):
            tool.entry(bytes(31))

    def test_truncated_commands(self):
        with self.assertRaises(ValueError):
            tool.entry(image()[:40])

    def test_invalid_command_size(self):
        data = image()
        struct.pack_into("<I", data, 36, 7)
        with self.assertRaises(ValueError):
            tool.entry(data)

    def test_bad_entry_name_offset(self):
        data = image()
        struct.pack_into("<I", data, 32 + 24, 0)
        with self.assertRaises(ValueError):
            tool.entry(data)

    def test_ambiguous_mapping(self):
        with self.assertRaisesRegex(ValueError, "exactly one"):
            tool.extract(image(extra_segment=True), ADDRESS)

    def test_zero_fill_is_not_file_backed(self):
        data = image()
        struct.pack_into("<Q", data, 0x400 + 32 + 48, 0x100)
        with self.assertRaisesRegex(ValueError, "file-backed"):
            tool.extract(data, ADDRESS)

    def test_non_executable_segment(self):
        data = image()
        struct.pack_into("<I", data, 0x400 + 32 + 60, 3)
        with self.assertRaisesRegex(ValueError, "executable"):
            tool.extract(data, ADDRESS)

    def test_boundary_range(self):
        data = image()
        self.assertEqual(tool.translate(data, 0x400, VM + 0x1FFFF, 1)[0], FILE + 0x1FFFF)
        with self.assertRaises(ValueError):
            tool.translate(data, 0x400, VM + 0x1FFFF, 2)

    def test_invalid_lengths(self):
        for length in (0, -1):
            with self.assertRaises(ValueError):
                tool.translate(image(), 0x400, ADDRESS, length)

    def test_cli_writes_new_evidence_only(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "fixture.macho"
            original = image()
            source.write_bytes(original)
            output = root / "evidence"
            command = [sys.executable, str(ROOT / "Scripts/issue9-extract.py"),
                       str(source), "--address", hex(ADDRESS), "--out", str(output)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(source.read_bytes(), original)
            self.assertEqual([p.name for p in output.iterdir()], ["issue9-handler-evidence.json"])
            prior = (output / "issue9-handler-evidence.json").read_bytes()
            again = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(again.returncode, 0)
            self.assertEqual((output / "issue9-handler-evidence.json").read_bytes(), prior)

    def test_cli_mismatch_creates_no_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "fixture.macho"
            data = image()
            data[FILE + ADDRESS - VM] ^= 1
            source.write_bytes(data)
            output = root / "evidence"
            result = subprocess.run([sys.executable, str(ROOT / "Scripts/issue9-extract.py"),
                                     str(source), "--address", hex(ADDRESS), "--out", str(output)],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(output.exists())

    def test_reported_handler_hashes_and_overlaps(self):
        windows = HANDLERS["windows"]
        for a in windows:
            raw = bytes.fromhex(a["bytes"])
            self.assertEqual(len(raw), 192)
            self.assertEqual(hashlib.sha256(raw).hexdigest(), tool.HANDLER_SHA256[a["candidate"]])
            for b in windows:
                aa, bb = int(a["address"], 16), int(b["address"], 16)
                start, end = max(aa, bb), min(aa + 192, bb + 192)
                if start < end:
                    self.assertEqual(raw[start-aa:end-aa], bytes.fromhex(b["bytes"])[start-bb:end-bb])

    def test_details_arguments_strings_and_common_callee(self):
        data = details_image()
        before = bytes(data)
        result = tool.extract(data, ADDRESS, details=True)
        self.assertEqual(result["schema"], 2)
        details = result["details"]
        self.assertEqual([a["r9d"] for a in details["handler_arguments"]], [1007, 1027, 1031, 1081])
        self.assertEqual(details["common_callee_window"]["address"], "0xffffff80026088a0")
        self.assertEqual(details["common_callee_window"]["length"], 1024)
        strings = details["referenced_strings"]
        self.assertEqual([int(s["address"], 16) for s in strings], list(STRING_ADDRESSES))
        self.assertEqual([s["text"] for s in strings], [f"T{i}" for i in range(9)])
        self.assertTrue(all(s["terminated"] for s in strings))
        self.assertEqual(data, before)
        self.assertTrue(all(s["status"] == "no LC_SYMTAB" for s in details["optional_symbol_windows"].values()))

    def test_details_handler_mutation_rejected(self):
        data = details_image()
        data[FILE + 0xFFFFFF800260B5D6 - VM] ^= 1
        with self.assertRaisesRegex(ValueError, "handler fingerprint mismatch"):
            tool.extract(data, ADDRESS, details=True)

    def test_strings_readable_not_executable(self):
        data = details_image()
        with self.assertRaisesRegex(ValueError, "executable"):
            tool.translate(data, 0x400, STRING_ADDRESSES[0], 1)
        self.assertTrue(tool.string_window(data, 0x400, STRING_ADDRESSES[0])["terminated"])

    def test_unterminated_string_has_limit(self):
        data = details_image()
        address = VM + 0x28000
        data[0x2B000:0x2B200] = b"A" * 512
        result = tool.string_window(data, 0x400, address)
        self.assertFalse(result["terminated"])
        self.assertEqual(len(result["text"]), 512)
        self.assertEqual(result["stop_reason"], "limit reached")

    def test_string_unmapped(self):
        result = tool.string_window(details_image(), 0x400, VM + 0x40000)
        self.assertFalse(result["terminated"])
        self.assertEqual(result["bytes"], "")

    def test_string_invalid_limit(self):
        for limit in (0, -1, 4097):
            with self.assertRaises(ValueError):
                tool.string_window(details_image(), 0x400, STRING_ADDRESSES[0], limit)

    def test_symbol_capture_and_readable_vtable(self):
        names = list(tool.SYMBOL_WINDOWS)
        data = add_symbols(details_image(), [(names[0], VM + 0x17000), (names[-1], VM + 0x21000)])
        result = tool.extract(data, ADDRESS, details=True)["details"]["optional_symbol_windows"]
        self.assertEqual(result[names[0]]["status"], "captured")
        self.assertEqual(result[names[0]]["length"], 2048)
        self.assertEqual(result[names[-1]]["status"], "captured")
        self.assertEqual(result[names[-1]]["segment"], "__CONST")
        self.assertEqual(result[names[1]]["status"], "not found")

    def test_ambiguous_symbol(self):
        name = next(iter(tool.SYMBOL_WINDOWS))
        data = add_symbols(details_image(), [(name, VM + 0x17000), (name, VM + 0x18000)])
        self.assertEqual(tool.symbol_windows(data, 0x400)[name]["status"], "ambiguous")

    def test_symbol_window_not_file_backed(self):
        name = next(iter(tool.SYMBOL_WINDOWS))
        data = add_symbols(details_image(), [(name, VM + 0x1FFFF)])
        self.assertEqual(tool.symbol_windows(data, 0x400)[name]["status"], "unmapped or truncated")

    def test_bad_symbol_string_is_reported_without_discarding_other_details(self):
        name = next(iter(tool.SYMBOL_WINDOWS))
        data = add_symbols(details_image(), [(name, VM + 0x17000)])
        struct.pack_into("<I", data, 0x35000, 0xFFFFFFFF)
        result = tool.extract(data, ADDRESS, details=True)["details"]
        self.assertIn("error", result["optional_symbol_windows"])
        self.assertEqual(len(result["referenced_strings"]), 9)

    def test_cli_details_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, output = root / "fixture.macho", root / "details"
            source.write_bytes(details_image())
            result = subprocess.run([sys.executable, str(ROOT / "Scripts/issue9-extract.py"),
                                     str(source), "--address", hex(ADDRESS), "--out", str(output), "--details"],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            payload = json.loads((output / "issue9-handler-details.json").read_text())
            self.assertEqual(payload["schema"], 2)


if __name__ == "__main__":
    unittest.main()
