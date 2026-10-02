#!/usr/bin/env python3
"""Check generated cmap coverage against the current UI, including a missing glyph."""

import importlib.util
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("font_generator", ROOT / "tools/generate_workout_fonts.py")
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


def coverage(path: Path) -> set[int]:
    source = path.read_text(encoding="utf-8")
    arrays = {}
    for name, body in re.findall(r"static const uint16_t (unicode_list_\d+)\[\] = \{(.*?)\};", source, re.S):
        arrays[name] = [int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]+)", body)]
    for name, body in re.findall(r"static const uint(?:8|16)_t (glyph_id_ofs_list_\d+)\[\] = \{(.*?)\};", source, re.S):
        arrays[name] = [int(value, 0) for value in re.findall(r"0x[0-9a-fA-F]+|\d+", body)]
    result = set()
    match = re.search(r"cmaps\[\]\s*=\s*\{(.*?)\n\};", source, re.S)
    assert match
    for block in re.findall(r"\{(.*?)\}", match.group(1), re.S):
        start = int(re.search(r"\.range_start\s*=\s*(\d+)", block).group(1))
        length = int(re.search(r"\.range_length\s*=\s*(\d+)", block).group(1))
        if "CMAP_FORMAT0_TINY" in block:
            result.update(range(start, start + length))
        elif "CMAP_FORMAT0_FULL" in block:
            name = re.search(r"\.glyph_id_ofs_list\s*=\s*(glyph_id_ofs_list_\d+)", block).group(1)
            result.update(start + index for index, offset in enumerate(arrays[name]) if index == 0 or offset)
        else:
            name = re.search(r"\.unicode_list\s*=\s*(unicode_list_\d+)", block).group(1)
            result.update(start + offset for offset in arrays[name])
    return result


class FontCoverageTest(unittest.TestCase):
    def test_current_inventory_and_each_font(self):
        expected = generator.symbols()
        actual = (ROOT / "assets/fonts/workout_symbols.txt").read_text(encoding="utf-8").rstrip("\n")
        self.assertEqual(expected, actual, "Regenerate fonts after changing UI text")
        for size in (12, 16, 20):
            codepoints = coverage(ROOT / f"assets/fonts/workout_font_{size}.c")
            self.assertTrue(set(map(ord, expected)) <= codepoints)
            self.assertNotIn(0x9F98, codepoints)

    def test_pixel_digits(self):
        codepoints = coverage(ROOT / "assets/fonts/workout_digits_35.c")
        self.assertEqual(codepoints, set(map(ord, "0123456789.")))

    def test_dynamic_network_font(self):
        codepoints = coverage(ROOT / "assets/fonts/workout_network_font_16.c")
        self.assertTrue(set(range(32, 127)) <= codepoints)
        self.assertTrue(set(map(ord, "家庭网络办公室无线龘")) <= codepoints)
        self.assertNotIn(0x1F600, codepoints)


if __name__ == "__main__":
    unittest.main()
