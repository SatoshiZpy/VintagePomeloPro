#!/usr/bin/env python3
"""Fail-closed runtime reuse and byte-preserving repack regression tests."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import ci_app_only as reuse


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], stderr=subprocess.PIPE).decode().strip()


def write(root, name, data):
    p = root / name
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(data)


def commit(root):
    git(root, 'add', '.')
    git(root, 'commit', '-qm', 'fixture')
    return git(root, 'rev-parse', 'HEAD')


def hap(path, changed=None, missing=None, extra=None):
    values = {'libs/arm64-v8a/libentry.so': b'native',
              'resources/rawfile/wine-data.zip': b'Wine with Mono',
              'resources/rawfile/source-build-candidate.json': b'original receipt',
              'ets/modules.abc': b'compiled app'}
    if changed:
        values.update(changed)
    if missing:
        del values[missing]
    if extra:
        values.update(extra)
    with zipfile.ZipFile(path, 'w') as z:
        for name, value in values.items():
            z.writestr(name, value)


class ReuseTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        git(self.root, 'init', '-q')
        git(self.root, 'config', 'user.name', 'CI fixture')
        git(self.root, 'config', 'user.email', 'ci@example.invalid')
        self.workflow = (ROOT / '.github/workflows/build.yml').read_text()
        write(self.root, reuse.WORKFLOW, self.workflow)
        write(self.root, 'entry/src/main/ets/service/Test.ets', 'export const value = 1;')
        write(self.root, 'entry/src/main/cpp/native.cpp', 'native v1')
        self.base = commit(self.root)

    def test_app_changes_reuse_but_native_sources_do_not(self):
        write(self.root, 'entry/src/main/ets/service/Test.ets', 'export const value = 2;')
        self.assertTrue(reuse.eligible(self.root, self.base, commit(self.root)))
        write(self.root, 'entry/src/main/cpp/native.cpp', 'native v2')
        self.assertFalse(reuse.eligible(self.root, self.base, commit(self.root)))

    def test_runtime_build_and_contract_inputs_force_full(self):
        for name in ['Makefile', 'scripts/wine_mono.py', 'patches/wine/fix.patch',
                     'docs/graphics/runtime-core.lock.json', 'oh-package-lock.json5',
                     'entry/src/main/cpp/types/libentry/index.d.ts',
                     'entry/src/main/resources/rawfile/runtime.json',
                     'entry/src/main/module.json5', 'AppScope/app.json5']:
            with self.subTest(name=name):
                write(self.root, name, '{}')
                self.assertFalse(reuse.eligible(self.root, self.base, commit(self.root)))
                git(self.root, 'reset', '--hard', self.base)

    def test_agent_docs_preserve_reuse_but_skill_scripts_do_not(self):
        for name in ['AGENTS.md', '.agents/skills/winehua-build-release/SKILL.md',
                     '.agents/skills/winehua-build-release/references/local.md']:
            write(self.root, name, 'Local build guidance')
            self.assertTrue(reuse.eligible(self.root, self.base, commit(self.root)))
        write(self.root, '.agents/skills/winehua-build-release/scripts/build.py', 'print(1)')
        self.assertFalse(reuse.eligible(self.root, self.base, commit(self.root)))

    def test_gitlinks_invalidate_reuse(self):
        git(self.root, 'update-index', '--add', '--cacheinfo', '160000,' + self.base + ',thirdparty/wine-valve')
        git(self.root, 'commit', '-qm', 'gitlink')
        self.assertFalse(reuse.eligible(self.root, self.base, git(self.root, 'rev-parse', 'HEAD')))

    def test_native_flags_image_and_sdk_invalidate_reuse(self):
        for before, after in [('BUILD_WINE_MONO: \'1\'', 'BUILD_WINE_MONO: \'0\''),
                              ('winehua-buildenv:test', 'winehua-buildenv:other'),
                              ('6.1.0(23)', '6.2.0(24)')]:
            with self.subTest(before=before):
                self.assertIn(before, self.workflow)
                write(self.root, reuse.WORKFLOW, self.workflow.replace(before, after))
                self.assertFalse(reuse.eligible(self.root, self.base, commit(self.root)))
                git(self.root, 'reset', '--hard', self.base)

    def test_rename_from_native_to_ets_cannot_hide_removal(self):
        git(self.root, 'mv', 'entry/src/main/cpp/native.cpp', 'entry/src/main/ets/Native.ets')
        self.assertFalse(reuse.eligible(self.root, self.base, commit(self.root)))

    def test_repack_preserves_native_runtime_receipts_and_allows_new_arkts(self):
        before, after = self.root / 'base.hap', self.root / 'new.hap'
        hap(before)
        hap(after, {'ets/modules.abc': b'new compiled app'})
        self.assertEqual(reuse.same_payload(before, after), 3)
        for member in ['libs/arm64-v8a/libentry.so', 'resources/rawfile/wine-data.zip',
                       'resources/rawfile/source-build-candidate.json']:
            hap(after, {member: b'changed'})
            with self.assertRaisesRegex(ValueError, 'bytes changed'):
                reuse.same_payload(before, after)
        hap(after, missing='libs/arm64-v8a/libentry.so')
        with self.assertRaisesRegex(ValueError, 'member set'):
            reuse.same_payload(before, after)
        hap(after, extra={'libs/x86_64/extra.so': b'extra'})
        with self.assertRaisesRegex(ValueError, 'member set'):
            reuse.same_payload(before, after)

    def test_sdk_elf_processing_is_restored_without_hiding_runtime_or_app_changes(self):
        before, after = self.root / 'base.hap', self.root / 'new.hap'
        hap(before)
        hap(after, {'libs/arm64-v8a/libentry.so': b'SDK processed ELF',
                    'ets/modules.abc': b'new compiled app'})
        reuse.restore_libraries(before, after)
        self.assertEqual(reuse.same_payload(before, after), 3)
        with zipfile.ZipFile(after) as z:
            self.assertEqual(z.read('ets/modules.abc'), b'new compiled app')
            # Harmony's installer must receive ordinary ZIP32 member headers.
            self.assertTrue(all(info.extract_version < 45 for info in z.infolist()))
        hap(after, {'resources/rawfile/wine-data.zip': b'wrong runtime'})
        reuse.restore_libraries(before, after)
        with self.assertRaisesRegex(ValueError, 'bytes changed'):
            reuse.same_payload(before, after)
        hap(after, extra={'libs/x86_64/extra.so': b'extra'})
        with self.assertRaisesRegex(ValueError, 'member set'):
            reuse.restore_libraries(before, after)

    def test_extraction_rejects_traversal_duplicate_and_symlink(self):
        for kind in ['traversal', 'symlink', 'duplicate']:
            archive = self.root / f'{kind}.zip'
            with zipfile.ZipFile(archive, 'w') as z:
                if kind == 'traversal':
                    z.writestr('../escape', b'bad')
                elif kind == 'symlink':
                    info = zipfile.ZipInfo('link')
                    info.external_attr = 0o120777 << 16
                    z.writestr(info, b'/etc/passwd')
                else:
                    import warnings
                    with warnings.catch_warnings():
                        warnings.simplefilter('ignore')
                        z.writestr('same', b'one')
                        z.writestr('same', b'two')
            with self.assertRaisesRegex(ValueError, 'Unsafe'):
                reuse.extract(archive, self.root / 'unpacked')

    def test_stage_disables_native_build_and_stripping_without_changing_payload(self):
        base = self.root / 'base.hap'
        hap(base)
        write(self.root, 'entry/build-profile.json5', json.dumps({
            'buildOption': {'externalNativeOptions': {'path': 'CMakeLists.txt'}},
            'buildOptionSet': [{'nativeLib': {'debugSymbol': {'strip': True}}}]}))
        old_root, old_state, old_git = reuse.ROOT, reuse.state, reuse.git
        try:
            reuse.ROOT = self.root
            reuse.state = lambda: {'baseHap': str(base)}
            reuse.git = lambda *args: b''
            reuse.stage()
        finally:
            reuse.ROOT, reuse.state, reuse.git = old_root, old_state, old_git
        config = json.loads((self.root / 'entry/build-profile.json5').read_text())
        self.assertNotIn('externalNativeOptions', config['buildOption'])
        self.assertFalse(config['buildOptionSet'][0]['nativeLib']['debugSymbol']['strip'])
        self.assertEqual((self.root / 'entry/libs/arm64-v8a/libentry.so').read_bytes(), b'native')
        self.assertEqual((self.root / 'entry/src/main/resources/rawfile/source-build-candidate.json').read_bytes(), b'original receipt')


if __name__ == '__main__':
    unittest.main()
