"""Regress the 11.1.0 MSI / 10.4.1 Wine mismatch that blocked first boot."""
import copy
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import wine_mono as mono


class WineMonoTest(unittest.TestCase):
    def setUp(self):
        self.installer = b"MZ\0" + mono.FILENAME.encode("utf-16-le") + mono.SHA256.encode()
        self.manifest = {"status": "bundled", "version": mono.VERSION,
                         "msi": mono.PAYLOAD_PATH, "sha256": mono.SHA256}
        self.payload = {mono.PAYLOAD_PATH: b"reviewed MSI",
                        "bin/aarch64-windows/appwiz.cpl": self.installer,
                        "bin/i386-windows/appwiz.cpl": self.installer}

    def validate(self):
        mono.validate_payload(self.payload.__getitem__, set(self.payload), self.manifest, "aarch64-windows")

    def test_both_installers_are_checked_after_msi_verification(self):
        with patch.object(mono, "check_msi") as check:
            self.validate()
            check.assert_called_once_with(b"reviewed MSI")
            self.payload["bin/i386-windows/appwiz.cpl"] = self.installer.replace(
                mono.FILENAME.encode("utf-16-le"), "wine-mono-11.1.0-x86.msi".encode("utf-16-le"))
            with self.assertRaisesRegex(ValueError, "installer mismatch"):
                self.validate()

    def test_rejects_actual_mixed_version_manifest_before_startup(self):
        self.manifest.update(version="11.1.0", msi="share/wine/mono/wine-mono-11.1.0-x86.msi",
                             sha256="deb0341431f8260b209fff6bc79ddcc5414b97f8e9236ab9fbdca4ce59e0a9b9")
        with self.assertRaisesRegex(ValueError, "version/path"):
            self.validate()

    def test_relabelled_or_corrupt_msi_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "MSI checksum mismatch"):
            self.validate()

    def test_installer_checksum_is_also_paired(self):
        with self.assertRaisesRegex(ValueError, "installer checksum mismatch"):
            mono.check_installer(self.installer.replace(mono.SHA256.encode(), b"0" * 64), "stale.cpl")

    def test_disabled_profile_cannot_sneak_in_interactive_installer_or_msi(self):
        self.manifest.update(status="disabled", sha256="")
        original = copy.copy(self.payload)
        self.payload.clear()
        self.validate()
        for name, data in original.items():
            self.payload = {name: data}
            with self.assertRaisesRegex(ValueError, "present although disabled"):
                self.validate()
        self.payload = {"share/wine/mono/wine-mono-11.1.0-x86.msi": b"old"}
        with self.assertRaises(ValueError):
            self.validate()

    def test_source_drift_is_rejected_before_download(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            addon = root / "dlls/appwiz.cpl/addons.c"
            mscoree = root / "dlls/mscoree/mscoree_private.h"
            addon.parent.mkdir(parents=True)
            mscoree.parent.mkdir(parents=True)
            addon.write_text(f'#define MONO_VERSION "{mono.VERSION}"\n#define MONO_SHA "{mono.SHA256}"\n')
            mscoree.write_text(f'#define WINE_MONO_VERSION "{mono.VERSION}"\n')
            mono.check_source(root)
            mscoree.write_text('#define WINE_MONO_VERSION "11.1.0"\n')
            with self.assertRaisesRegex(ValueError, "source mismatch"):
                mono.check_source(root)

    def test_make_sentinel_tracks_the_same_pin(self):
        # Read make's actual evaluated variable without executing build recipes.
        result = subprocess.run(["make", "--no-print-directory", "-f", "Makefile", "-f", "-",
                                 "print-mono-sentinel"], cwd=ROOT, input=
                                'print-mono-sentinel:\n\t@echo "$(WINE_MONO_SENTINEL)"\n',
                                text=True, capture_output=True, check=True)
        self.assertEqual(result.stdout.strip(), str(ROOT / "build/wine-mono" / mono.FILENAME))


if __name__ == "__main__":
    unittest.main()
