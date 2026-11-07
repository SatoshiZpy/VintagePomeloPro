#!/usr/bin/env python3
"""Local runtime reuse must reject dirty native inputs and preserve app edits."""
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import local_hap as local


class LocalBuildTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.original = local.ROOT
        local.ROOT = self.root
        self.addCleanup(setattr, local, 'ROOT', self.original)
        self.git('init', '-q')
        self.git('config', 'user.name', 'Fixture')
        self.git('config', 'user.email', 'fixture@example.invalid')
        self.write(local.reuse.WORKFLOW, (ROOT / local.reuse.WORKFLOW).read_text())
        self.write('entry/src/main/ets/Test.ets', 'export const value = 1;')
        self.write('entry/src/main/cpp/Test.cpp', 'native')
        self.write('.gitignore', '/snapshot.tar\n')
        self.git('add', '.')
        self.git('commit', '-qm', 'base')
        self.base = self.git('rev-parse', 'HEAD')

    def git(self, *args):
        return subprocess.check_output(['git', *args], cwd=self.root, text=True).strip()

    def write(self, name, value):
        p = self.root / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(value)

    def test_uncommitted_app_edits_and_new_files_are_built(self):
        self.write('entry/src/main/ets/Test.ets', 'export const value = 2;')
        self.write('entry/src/main/ets/New.ets', 'export const other = 3;')
        result = local.snapshot(self.base, self.root / 'snapshot.tar')
        self.assertTrue(result['dirty'])
        with tarfile.open(self.root / 'snapshot.tar') as archive:
            self.assertEqual(archive.extractfile('entry/src/main/ets/Test.ets').read(), b'export const value = 2;')
            self.assertIn('entry/src/main/ets/New.ets', archive.getnames())
        self.assertEqual(result['snapshotSha256'], local.snapshot(self.base, self.root / 'snapshot.tar')['snapshotSha256'])

    def test_native_edits_block_reuse_before_compilation(self):
        self.write('entry/src/main/cpp/Test.cpp', 'changed native')
        with self.assertRaisesRegex(ValueError, 'local change'):
            local.guard(self.base)
        self.git('add', '.')
        self.git('commit', '-qm', 'native changed')
        with self.assertRaisesRegex(ValueError, 'Native inputs differ'):
            local.guard(self.base)

    def test_dirty_workflow_and_unknown_untracked_files_block_reuse(self):
        self.write('unexpected.pem', 'fixture')
        with self.assertRaisesRegex(ValueError, 'unexpected.pem'):
            local.guard(self.base)
        (self.root / 'unexpected.pem').unlink()
        self.write(local.reuse.WORKFLOW, (ROOT / local.reuse.WORKFLOW).read_text() + '\n')
        with self.assertRaisesRegex(ValueError, 'local change'):
            local.guard(self.base)

    def test_deleted_ets_is_not_exported(self):
        (self.root / 'entry/src/main/ets/Test.ets').unlink()
        result = local.snapshot(self.base, self.root / 'snapshot.tar')
        with tarfile.open(self.root / 'snapshot.tar') as archive:
            self.assertNotIn('entry/src/main/ets/Test.ets', archive.getnames())
        self.assertTrue(result['dirty'])

    def test_unchanged_write_preserves_mtime(self):
        p = self.root / 'same'
        local.write_changed(p, b'same')
        before = p.stat().st_mtime_ns
        local.write_changed(p, b'same')
        self.assertEqual(before, p.stat().st_mtime_ns)


if __name__ == '__main__':
    unittest.main()
