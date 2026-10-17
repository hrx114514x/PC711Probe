import hashlib
import importlib.util
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
        self.assertEqual(tool.extract(image(False), ADDRESS)["module_file_offset"], "0x0")

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


if __name__ == "__main__":
    unittest.main()
