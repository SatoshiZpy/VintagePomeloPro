#!/usr/bin/env python3
"""Reject mismatched GNU x64 graphics sources and packaged payloads in CI."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RAW = ROOT / 'entry/src/main/resources/rawfile/linux-proton/graphics-x64'

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def verify(root=ROOT, hap=None):
    raw = root / 'entry/src/main/resources/rawfile/linux-proton/graphics-x64'
    manifest = json.loads((raw / 'manifest.json').read_text())
    dxvk = manifest['dxvkBuild']
    for group in (manifest['sources'], dxvk['sources']):
        for name, expected in group.items():
            assert digest(root / name) == expected, 'Graphics source differs from build: ' + name
    assert digest(root / 'scripts/patches/dxvk-bgra-uav.patch') == dxvk['patchSha256']
    rows = manifest['files']
    assert len({row['path'] for row in rows}) == len(rows), 'Duplicate graphics payload'
    for row in rows:
        name = row['path']
        assert not Path(name).is_absolute() and '..' not in Path(name).parts
        file = raw / name
        if name == 'libvulkan_virtio.so':
            file = root / 'entry/src/main/resources/rawfile/linux-steam/bootstrap/native-game-support' / name
        assert not file.is_symlink(), 'Linked graphics payload: ' + name
        assert file.stat().st_size == row['bytes'] and digest(file) == row['sha256'], 'Graphics payload differs: ' + name
    actual = {str(p.relative_to(raw)) for p in raw.rglob('*') if p.is_file() and p.name != 'manifest.json'}
    assert actual == {row['path'] for row in rows if row['path'] != 'libvulkan_virtio.so'}, 'Unrecorded graphics payload'
    for row in dxvk['files']:
        assert row in rows, 'DXVK receipt differs from graphics manifest'
    if hap is not None:
        prefix = root / 'entry/src/main/resources/rawfile'
        manifest_name = 'resources/rawfile/' + (raw / 'manifest.json').relative_to(prefix).as_posix()
        assert hap.read(manifest_name) == (raw / 'manifest.json').read_bytes(), 'HAP graphics manifest differs'
        for row in rows:
            file = raw / row['path']
            if row['path'] == 'libvulkan_virtio.so':
                file = prefix / 'linux-steam/bootstrap/native-game-support' / row['path']
            data = hap.read('resources/rawfile/' + file.relative_to(prefix).as_posix())
            assert len(data) == row['bytes'] and hashlib.sha256(data).hexdigest() == row['sha256'], 'HAP graphics bytes differ'
    return manifest['sources']


def main():
    verify()
    print('PASS: GNU x64 graphics source, DXVK patch and all packaged payload hashes')

if __name__ == '__main__':
    main()
