"""Static properties of the actual reporter bytes used by the diagnostic."""
import hashlib
import json
from pathlib import Path
import struct
import unittest

ROOT=Path(__file__).resolve().parents[1]
D=json.loads((ROOT/'Tests/fixtures/Issue9-details-20261018.json').read_text())
W=D['details']['optional_symbol_windows']


class RuntimeEvidence(unittest.TestCase):
    def test_all_window_hashes(self):
        for w in D['windows']+[D['details']['common_callee_window']]+list(W.values()):
            self.assertEqual(hashlib.sha256(bytes.fromhex(w['bytes'])).hexdigest(),w['sha256'])

    def test_get_mappers_normal_return_sets_true_without_test(self):
        code=bytes.fromhex(W['__ZN16IONVMeController12GetIOMappersEv']['bytes'])[:39]
        expected='554889e553504889fb488bbf0801000031f6e82b2051fe48898398020000b0014883c4085b5dc3'
        self.assertEqual(code.hex(),expected)
        self.assertEqual(code[30:32],b'\xb0\x01')

    def test_base_start_call_order(self):
        w=W['__ZN16IONVMeController5startEP9IOService']
        code=bytes.fromhex(w['bytes']);base=int(w['address'],16)
        self.assertEqual(code[0xD0],0xE8)
        helper=base+0xD0+5+struct.unpack_from('<i',code,0xD1)[0]
        self.assertEqual(helper,0xFFFFFF80025F55B6)
        self.assertEqual(code[0xE3:0xE9],bytes.fromhex('ff90500a0000'))
        self.assertEqual(code[0x3B7:0x3BD],bytes.fromhex('ff90380a0000'))
        self.assertLess(0xD0,0xE3)

    def test_encoded_vtable_candidate_offsets_agree(self):
        # This checks the supplied raw target fields; it is not a runtime vtable
        # observation or a substitute for full chained-fixup metadata.
        code=bytes.fromhex(W['__ZTV16IONVMeController']['bytes'])
        for slot,target in [(0xA38,0x24F8D32),(0xA50,0x24F5AA4),(0xA68,0x24F5F1E)]:
            raw=struct.unpack_from('<Q',code,slot+16)[0]
            self.assertEqual(raw & 0x3FFFFFFF,target)
            self.assertEqual((raw >> 30) & 3,0)
            self.assertEqual(raw >> 63,0)

    def test_short_handler_prefixes_are_position_independent(self):
        for w in D['windows']:
            code=bytes.fromhex(w['bytes'])
            if w['candidate']=='mappers':
                self.assertEqual(code[:5],bytes.fromhex('554889e553'))
            else:
                self.assertEqual(code[:8],bytes.fromhex('554889e54883ec10'))
            # A 14-byte trampoline would include a RIP-relative LEA; the
            # diagnostic must use preflighted short routes for these helpers.
            offset=13 if w['candidate']=='mappers' else 12
            self.assertEqual(code[offset:offset+3],bytes.fromhex('488d3d'))

    def test_init_bytes_repeated_in_start_window(self):
        w=W['__ZN16IONVMeController5startEP9IOService'];code=bytes.fromhex(w['bytes'])
        position=int(D['initialize_pci_address'],16)-int(w['address'],16)
        original=bytes.fromhex((ROOT/'Tests/fixtures/InitializePCI-580.hex').read_text())
        self.assertEqual(code[position:position+580],original)


if __name__=='__main__': unittest.main()
