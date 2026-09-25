"""Regression tests for Shantae's native script-pointer discovery."""
import unittest
from unittest.mock import patch

import gen_annotations as annotations


class NativeRecordsTest(unittest.TestCase):
    def scan(self, records, banks=(0, 1, 2), bank_count=3):
        rom = bytearray([0xC9]) * (bank_count * 0x4000)
        for offset, data in records:
            rom[offset:offset + len(data)] = data
        with patch.object(annotations, "ROM", rom), patch.object(annotations, "CODE_BANKS", banks):
            return annotations.native_record_targets()

    def test_callback_does_not_depend_on_next_opcode(self):
        result = self.scan([(0x4200, bytes.fromhex("2e 02 34 56 5a 64 00"))])
        self.assertIn((2, 0x5634), result)

    def test_native_call_uses_address_then_bank(self):
        result = self.scan([(0x4200, bytes.fromhex("32 34 56 02 1c 00 01"))])
        self.assertIn((2, 0x5634), result)
        self.assertNotIn((0x34, 0x0256), result)

    def test_callback_for_another_object(self):
        result = self.scan([(0x4200, bytes.fromhex("7a 80 cb 00 40 01 02 34 56"))])
        self.assertIn((2, 0x5634), result)

    def test_fixed_bank_pointer_uses_bank_zero(self):
        result = self.scan([(0x4200, bytes.fromhex("32 34 12 02"))])
        self.assertIn((0, 0x1234), result)

    def test_record_may_end_at_bank_boundary(self):
        result = self.scan([(0x7FFC, bytes.fromhex("32 34 56 02"))])
        self.assertIn((2, 0x5634), result)

    def test_record_cannot_cross_bank_boundary(self):
        result = self.scan([(0x7FFE, bytes.fromhex("32 34 56 02"))])
        self.assertNotIn((2, 0x5634), result)

    def test_invalid_bank_and_non_rom_targets(self):
        result = self.scan([(0x4200, bytes.fromhex("32 34 56 03 2e 02 00 c0"))])
        self.assertNotIn((3, 0x5634), result)
        self.assertNotIn((2, 0xC000), result)

    def test_bank_zero_switchable_alias_is_not_seeded(self):
        result = self.scan([(0x4200, bytes.fromhex("32 34 56 00"))])
        self.assertNotIn((0, 0x5634), result)

    def test_erased_padding_is_not_native_code(self):
        result = self.scan([(0x4200, bytes.fromhex("32 34 56 02")),
                            (0x9634, bytes([0xFF]) * 8)])
        self.assertNotIn((2, 0x5634), result)

    def test_all_saved_gameplay_misses_are_found_without_harvest(self):
        result = annotations.native_record_targets()
        for target in ((6, 0x59E9), (6, 0x57E1), (7, 0x5A8D)):
            self.assertIn(target, result)

    def test_rom_opcode_table_matches_documented_handlers(self):
        for opcode, handler in ((0x2E, 0x1A18), (0x32, 0x1A46), (0x7A, 0x16D2)):
            actual = int.from_bytes(annotations.ROM[0x600 + opcode:0x602 + opcode], "little")
            self.assertEqual(actual, handler)

    def test_banked_callee_inline_arguments_resume_after_data(self):
        result = annotations.far_inline_returns()
        self.assertIn((5, 0x651E), result)
        self.assertIn((5, 0x6530), result)


if __name__ == "__main__":
    unittest.main()
