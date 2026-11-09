"""Reject stale/partial native inputs before constructing a candidate package."""
from pathlib import Path
import hashlib
import json
import sys
import tempfile
import unittest
from unittest.mock import patch
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'scripts'))
import local_proton_graphics_hap as candidate

class NativeReceiptTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.override = patch.object(candidate, 'ROOT', self.root)
        self.override.start()
        self.addCleanup(self.override.stop)
        self.addCleanup(self.temporary.cleanup)
        self.write('entry/src/main/cpp/CMakeLists.txt', b'complete native target')
        self.write('source.cpp', b'current source')
        self.write('header.h', b'current header')
        self.write(candidate.NATIVE+'/libsteamohos_gnu_child.so', b'compiled native')
        child = {'recipeSha256': self.digest('entry/src/main/cpp/CMakeLists.txt'),
                 'sourceObjects': {'source.cpp': self.digest('source.cpp')},
                 'headers': {'header.h': self.digest('header.h')},
                 'outputSha256': self.digest(candidate.NATIVE+'/libsteamohos_gnu_child.so')}
        self.write(candidate.NATIVE+'/native-child-build.json', json.dumps(child).encode())
        names = ['libproton_probe.so', 'libproton_vulkan.so', 'libvulkan_virtio.so', 'dxvk/d3d11.dll', 'dxvk/dxgi.dll']
        names += [f'wine/{arch}-windows/{name}' for arch in ['aarch64', 'x86_64']
                  for name in ['winevulkan.dll', 'vulkan-1.dll', 'd3dcompiler_47.dll']]
        files = []
        for name in names:
            contents = ('locked:'+name).encode()
            relative = candidate.PROFILE+name
            if name == 'libvulkan_virtio.so':
                relative = 'entry/src/main/resources/rawfile/linux-steam/bootstrap/native-game-support/'+name
            self.write(relative, contents)
            files.append(dict(path=name, bytes=len(contents), sha256=hashlib.sha256(contents).hexdigest()))
        self.write('scripts/patches/dxvk-bgra-uav.patch', b'reviewed DXVK patch')
        dxvk = dict(baseCommit='5dff1d7b3dc3c2cd7f51356b99213bcc427b1af9', architecture='arm64x',
                    sources={'scripts/patches/dxvk-bgra-uav.patch': self.digest('scripts/patches/dxvk-bgra-uav.patch')},
                    patchSha256=self.digest('scripts/patches/dxvk-bgra-uav.patch'),
                    files=[row for row in files if row['path'].startswith('dxvk/')])
        profile = dict(profile='dxvk-legacy-x64-native-memory-v1', files=files, dxvkBuild=dxvk,
                       sources={'source.cpp': self.digest('source.cpp')})
        self.write(candidate.PROFILE+'manifest.json', json.dumps(profile).encode())
    def write(self, relative, contents):
        path = self.root/relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(contents)
    def digest(self, relative):
        return hashlib.sha256((self.root/relative).read_bytes()).hexdigest()
    def test_unrecorded_pass_profile_header_rejected(self):
        self.write('entry/src/main/cpp/linux_steam/gnu_fixture/proton_vulkan_pass_profile.h', b'new instrumentation')
        with self.assertRaisesRegex(ValueError, 'missing from native source receipts'):
            candidate.validate_native()
    def test_valid_paired_outputs(self):
        candidate.validate_native()
    def test_dxvk_patch_edit_requires_rebuild(self):
        self.write('scripts/patches/dxvk-bgra-uav.patch', b'changed code')
        with self.assertRaisesRegex(ValueError, 'Rebuild required'):
            candidate.validate_native()
    def test_dxvk_binary_replacement_rejected(self):
        self.write(candidate.PROFILE+'dxvk/d3d11.dll', b'wrong DXVK')
        with self.assertRaisesRegex(ValueError, 'DXVK payload differs'):
            candidate.validate_native()
    def test_native_source_edit_requires_rebuild(self):
        self.write('source.cpp', b'edited after build')
        with self.assertRaisesRegex(ValueError, 'Rebuild required'):
            candidate.validate_native()
    def test_header_edit_requires_rebuild(self):
        self.write('header.h', b'ABI changed')
        with self.assertRaisesRegex(ValueError, 'Rebuild required'):
            candidate.validate_native()
    def test_child_replacement_rejected(self):
        self.write(candidate.NATIVE+'/libsteamohos_gnu_child.so', b'stale compiled native')
        with self.assertRaisesRegex(ValueError, 'child differs'):
            candidate.validate_native()
    def test_mismatched_graphics_dll_rejected(self):
        self.write(candidate.PROFILE+'wine/x86_64-windows/winevulkan.dll', b'wrong build')
        with self.assertRaisesRegex(ValueError, 'payload differs'):
            candidate.validate_native()
    def test_extra_unrecorded_file_rejected(self):
        self.write(candidate.PROFILE+'extra.dll', b'unrecorded')
        with self.assertRaisesRegex(ValueError, 'Unrecorded'):
            candidate.validate_native()
    def test_linked_payload_rejected(self):
        path = self.root/candidate.PROFILE/'libproton_probe.so'
        outside = self.root/'outside';outside.write_bytes(path.read_bytes())
        path.unlink();path.symlink_to(outside)
        with self.assertRaisesRegex(ValueError, 'linked'):
            candidate.validate_native()

if __name__ == '__main__':
    unittest.main()
