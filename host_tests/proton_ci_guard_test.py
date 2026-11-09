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
    def test_gateway_timeout_retried_and_validated(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(ci.time, 'sleep') as sleep:
            target = Path(tmp) / 'input.hap'
            failure = ci.urllib.error.HTTPError('https://example.invalid/input', 504, 'timeout', {}, None)
            with patch.object(ci.urllib.request, 'urlopen', side_effect=[failure, io.BytesIO(b'valid')]) as request:
                ci.download('https://example.invalid/input', target, hashlib.sha256(b'valid').hexdigest(), size=5)
            self.assertEqual(target.read_bytes(), b'valid')
            self.assertEqual(request.call_count, 2)
            sleep.assert_called_once_with(2)

    def test_transient_retries_bounded(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(ci.time, 'sleep'):
            target = Path(tmp) / 'input.hap'
            with patch.object(ci.urllib.request, 'urlopen', side_effect=TimeoutError) as request:
                with self.assertRaises(TimeoutError):
                    ci.download('https://example.invalid/input', target, 'unused')
            self.assertEqual(request.call_count, 4)
            self.assertFalse(target.exists())
            self.assertFalse(target.with_suffix('.hap.pending').exists())

    def test_permanent_http_error_not_retried(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(ci.time, 'sleep') as sleep:
            failure = ci.urllib.error.HTTPError('https://example.invalid/input', 404, 'missing', {}, None)
            with patch.object(ci.urllib.request, 'urlopen', side_effect=failure) as request:
                with self.assertRaises(ci.urllib.error.HTTPError):
                    ci.download('https://example.invalid/input', Path(tmp) / 'input.hap', 'unused')
            self.assertEqual(request.call_count, 1)
            sleep.assert_not_called()

    def test_cache_requires_correct_digest(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / 'input.hap'; target.write_bytes(b'valid')
            with patch.object(ci.urllib.request, 'urlopen') as request:
                ci.download('https://example.invalid/input', target, hashlib.sha256(b'valid').hexdigest(), size=5)
                request.assert_not_called()
            target.write_bytes(b'wrong')
            with patch.object(ci.urllib.request, 'urlopen', return_value=io.BytesIO(b'valid')) as request:
                ci.download('https://example.invalid/input', target, hashlib.sha256(b'valid').hexdigest(), size=5)
                request.assert_called_once()
            self.assertEqual(target.read_bytes(), b'valid')

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
