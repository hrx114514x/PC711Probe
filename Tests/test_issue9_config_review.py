"""Synthetic file tests: input preservation, privacy filtering and path boundaries."""
import importlib.util
import json
from pathlib import Path
import plistlib
import tempfile
import unittest
from unittest.mock import patch
import zipfile

SCRIPT = Path(__file__).resolve().parents[1] / 'Scripts/issue9-config-review.py'
SPEC = importlib.util.spec_from_file_location('config_review', SCRIPT)
MOD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MOD)


class ConfigReviewTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.oc = self.root / 'EFI/OC'
        (self.oc / 'ACPI').mkdir(parents=True)
        self.output = self.root / 'review.zip'
        self.config = {
            'ACPI': {'Add': [{'Enabled': True, 'Path': 'SSDT-TEST.aml', 'Comment': 'local note'}],
                     'Patch': [{'Enabled': True, 'Find': b'_CRS', 'Replace': b'XCRS'}]},
            'Booter': {'Quirks': {'DevirtualiseMmio': True}, 'MmioWhitelist': [{'Address': 0x8C100000, 'Enabled': True}]},
            'DeviceProperties': {'Add': {'PciRoot(0x0)/Pci(0x6,0x0)': {'device-id': b'\xca\x7e\x00\x00', 'ranges': b'RANGE', 'serial-number': 'PRIVATE', 'ROM': b'PRIVATE'}}},
            'Kernel': {'Add': [{'BundlePath': 'PM9C1aReadOnly.kext', 'Enabled': True}], 'Patch': []},
            'UEFI': {'Drivers': [{'Path': 'OpenRuntime.efi', 'Enabled': True, 'Arguments': 'private-driver-argument'}]},
            'PlatformInfo': {'Generic': {'SystemSerialNumber': 'PRIVATE', 'SystemUUID': 'PRIVATE'}},
            'Misc': {'Security': {'PasswordHash': b'PRIVATE'}},
            'NVRAM': {'Add': {MOD.APPLE_GUID: {'boot-args': 'npci=0x2000 -pm9c1adiag', 'unrelated': b'PRIVATE'}, 'OTHER': {'secret': 'PRIVATE'}}},
        }
        (self.oc / 'ACPI/SSDT-TEST.aml').write_bytes(b'SSDT synthetic fixture')
        self.save()

    def save(self):
        (self.oc / 'config.plist').write_bytes(plistlib.dumps(self.config))

    def test_review_fields_and_source_preservation(self):
        before = {p.relative_to(self.oc): p.read_bytes() for p in self.oc.rglob('*') if p.is_file()}
        manifest = MOD.export(self.oc, self.output)
        with zipfile.ZipFile(self.output) as z:
            data = plistlib.loads(z.read('REVIEW_ONLY.plist'))
            self.assertEqual(data['ACPI']['Patch'][0]['Find'], b'_CRS')
            self.assertTrue(data['Booter']['Quirks']['DevirtualiseMmio'])
            self.assertEqual(data['NVRAM']['Add'][MOD.APPLE_GUID], {'boot-args': 'npci=0x2000 -pm9c1adiag'})
            self.assertNotIn('PlatformInfo', data)
            self.assertNotIn('Misc', data)
            self.assertNotIn(b'PRIVATE', z.read('REVIEW_ONLY.plist'))
            self.assertNotIn('Arguments', data['UEFI']['Drivers'][0])
            self.assertEqual(z.read('ACPI/SSDT-TEST.aml'), before[Path('ACPI/SSDT-TEST.aml')])
            self.assertEqual(json.loads(z.read('manifest.json'))['schema'], 1)
        self.assertFalse(manifest['live_boot_args_verified'])
        after = {p.relative_to(self.oc): p.read_bytes() for p in self.oc.rglob('*') if p.is_file()}
        self.assertEqual(before, after)

    def test_missing_and_disabled_tables_are_explicit(self):
        self.config['ACPI']['Add'] += [{'Enabled': True, 'Path': 'missing.aml'}, {'Enabled': False, 'Path': '../not-copied.aml'}]
        self.save()
        result = MOD.export(self.oc, self.output)
        self.assertEqual([x['status'] for x in result['acpi_tables']], ['copied', 'missing'])
        with zipfile.ZipFile(self.output) as z:
            self.assertNotIn('ACPI/missing.aml', z.namelist())

    def test_boot_args_delete_and_persistence_controls_only(self):
        self.config['NVRAM']['Delete'] = {MOD.APPLE_GUID: ['boot-args', 'private-variable'], 'OTHER': ['secret']}
        self.config['NVRAM']['WriteFlash'] = True
        self.save()
        MOD.export(self.oc, self.output)
        with zipfile.ZipFile(self.output) as z:
            nvram = plistlib.loads(z.read('REVIEW_ONLY.plist'))['NVRAM']
        self.assertEqual(nvram['Delete'], {MOD.APPLE_GUID: ['boot-args']})
        self.assertTrue(nvram['WriteFlash'])

    def test_licensing_table_contents_not_exported(self):
        (self.oc / 'ACPI/licensing.aml').write_bytes(b'MSDM PRIVATE LICENSE DATA')
        self.config['ACPI']['Add'].append({'Enabled': True, 'Path': 'licensing.aml'})
        self.save()
        result = MOD.export(self.oc, self.output)
        self.assertEqual(result['acpi_tables'][-1]['status'], 'omitted_license_table')
        with zipfile.ZipFile(self.output) as z:
            self.assertNotIn('ACPI/licensing.aml', z.namelist())
            self.assertTrue(all(b'PRIVATE LICENSE DATA' not in z.read(name) for name in z.namelist()))

    def test_traversal_and_absolute_paths_rejected_before_output(self):
        for name in ('../outside.aml', '..\\outside.aml', '/outside.aml', 'C:\\outside.aml'):
            with self.subTest(name=name):
                self.config['ACPI']['Add'][0]['Path'] = name
                self.save()
                with self.assertRaises(ValueError):
                    MOD.export(self.oc, self.output)
                self.assertFalse(self.output.exists())

    def test_symlink_escape_rejected(self):
        other = self.root / 'outside.aml'
        other.write_bytes(b'not authorized by ACPI path')
        link = self.oc / 'ACPI/link.aml'
        try:
            link.symlink_to(other)
        except (OSError, NotImplementedError):
            self.skipTest('Symlink creation unavailable on this host')
        self.config['ACPI']['Add'][0]['Path'] = 'link.aml'
        self.save()
        with self.assertRaises(ValueError):
            MOD.export(self.oc, self.output)
        self.assertFalse(self.output.exists())

    def test_output_must_be_new_and_outside_efi(self):
        self.output.write_bytes(b'keep existing')
        with self.assertRaises(ValueError):
            MOD.export(self.oc, self.output)
        self.assertEqual(self.output.read_bytes(), b'keep existing')
        for p in (self.oc / 'review.zip', self.oc.parent / 'review.zip'):
            with self.assertRaises(ValueError):
                MOD.export(self.oc, p)
            self.assertFalse(p.exists())

    def test_duplicate_enabled_paths_rejected(self):
        self.config['ACPI']['Add'].append({'Enabled': True, 'Path': 'ssdt-test.AML'})
        self.save()
        with self.assertRaises(ValueError):
            MOD.export(self.oc, self.output)
        self.assertFalse(self.output.exists())

    def test_size_limit_rejects_without_output(self):
        with patch.object(MOD, 'MAX_TABLE', 4):
            with self.assertRaises(ValueError):
                MOD.export(self.oc, self.output)
        self.assertFalse(self.output.exists())

    def test_malformed_enabled_and_duplicate_guid_rejected(self):
        self.config['ACPI']['Add'][0]['Enabled'] = 'true'
        self.save()
        with self.assertRaises(ValueError):
            MOD.export(self.oc, self.output)
        self.config['ACPI']['Add'][0]['Enabled'] = True
        self.config['NVRAM']['Add'][MOD.APPLE_GUID.lower()] = {'boot-args': 'different'}
        self.save()
        with self.assertRaises(ValueError):
            MOD.export(self.oc, self.output)
        self.assertFalse(self.output.exists())


if __name__ == '__main__':
    unittest.main()
