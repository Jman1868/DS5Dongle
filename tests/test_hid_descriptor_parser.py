#!/usr/bin/env python3
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "pi"))

from parse_hid_descriptor import parse_descriptor


class HidDescriptorParserTest(unittest.TestCase):
    def test_firmware_descriptors_have_declared_lengths(self):
        source = ROOT / "src" / "usb_descriptors.cpp"
        self.assertEqual(len(parse_descriptor(source, "desc_hid_report_ds")), 337)
        self.assertEqual(len(parse_descriptor(source, "desc_hid_report_dse")), 453)


if __name__ == "__main__":
    unittest.main()
