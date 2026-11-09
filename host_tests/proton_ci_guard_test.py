#!/usr/bin/env python3
"""Negative checks for the focused CI lane's source and download boundary."""
import hashlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import ci_proton_graphics as ci


class GuardTest(unittest.TestCase):
    def test_unrebuilt_native_inputs_rejected(self):
        for name in ['thirdparty/wine', 'thirdparty/mesa', 'thirdparty/dxvk',
                     'entry/src/main/cpp/CMakeLists.txt',
                     'entry/src/main/cpp/linux_steam/unknown.cpp',
                     'scripts/build_guest_graphics.sh', 'oh-package-lock.json5']:
            with self.subTest(name=name):
                self.assertFalse(ci.covered_input(name))

    def test_changed_recipe_rejected(self):
        with patch.object(ci.reuse, 'native_recipe', side_effect=['base', 'changed']):
            with self.assertRaisesRegex(ValueError, 'Full native recipe changed'):
                ci.guard()

    def test_dirty_checkout_rejected(self):
        with patch.object(ci.reuse, 'native_recipe', return_value='same'), \
             patch.object(ci.reuse, 'git', side_effect=[b'', b' M entry/src/main/cpp/linux_steam/native_loader.cpp']):
            with self.assertRaisesRegex(ValueError, 'clean checkout'):
                ci.guard()

    def test_corrupt_download_cannot_replace_verified_input(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / 'input.hap'
            target.write_bytes(b'previous')
            with patch.object(ci.urllib.request, 'urlopen', return_value=io.BytesIO(b'corrupt')):
                with self.assertRaisesRegex(ValueError, 'does not match lock'):
                    ci.download('https://example.invalid/input', target, hashlib.sha256(b'valid').hexdigest())
            self.assertEqual(target.read_bytes(), b'previous')


if __name__ == '__main__':
    unittest.main()
