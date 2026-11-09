#!/usr/bin/env python3
"""Rebuild the tested Proton profile against an immutable, verified full CI base.

Only GNU adapters, the complete OHOS GNU child and DXVK are rebuilt. Other native
inputs must still match the base commit; their packaged bytes stay unchanged.
This lane never changes the app-only reuse policy or claims a full Wine rebuild.
"""
import argparse
import hashlib
import http.client
import io
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import time
import urllib.error
import urllib.request
import zipfile

import ci_app_only as reuse
import local_proton_graphics_hap as profile

ROOT = Path(__file__).resolve().parents[1]
LOCK = ROOT / 'docs/graphics/proton-ci-profile.lock.json'
CI_ONLY = {
    '.github/workflows/build-proton-profile.yml', 'scripts/ci_proton_graphics.py',
    'ci/Makefile.proton-ci', 'docs/graphics/proton-ci-profile.lock.json',
    'docs/graphics/proton-ci-base-source.json',
    # Host-built diagnostic only; never part of the HAP runtime payload.
    'smoke/proton_x64_network_probe.c',
}


def run(*args, **kwargs):
    return subprocess.run(list(map(str, args)), check=True, **kwargs)


def read(path):
    return json.loads(path.read_text())


def covered_input(name):
    return (reuse.app_input(name) or name in profile.REBUILT_INPUTS or
            name in profile.BRANDING_INPUTS or name in CI_ONLY or
            name.startswith((profile.PROFILE, profile.BOOTSTRAP)))


def guard():
    lock = read(LOCK)
    base = lock['base']['commit']
    if reuse.native_recipe(ROOT, base) != reuse.native_recipe(ROOT, 'HEAD'):
        raise ValueError('Full native recipe changed; rebuild the full base first')
    names = reuse.git(ROOT, 'diff', '--no-renames', '--name-only', base, 'HEAD').decode().splitlines()
    for name in names:
        if not covered_input(name):
            raise ValueError('Not covered by focused native rebuild: ' + name)
    # Reject edits and untracked inputs before any generated output is written.
    if reuse.git(ROOT, 'status', '--porcelain', '--untracked-files=all', '--ignore-submodules=all').strip():
        raise ValueError('Start the reproducible CI build from a clean checkout')
    profile.validate_sources(profile.BRANDING_INPUTS)
    profile.validate_bootstrap()
    manifest = ROOT / 'docs/graphics/proton-ci-base-source.json'
    if reuse.sha(manifest) != lock['base']['sourceManifestSha256']:
        raise ValueError('Full-base source manifest differs from lock')
    if read(manifest)['commit'] != base:
        raise ValueError('Full-base source commit mismatch')
    return lock


def download(url, target, digest, algorithm='sha256', size=None):
    if target.exists() and (size is None or target.stat().st_size == size):
        if file_hash(target, algorithm) == digest:
            return
    target.parent.mkdir(parents=True, exist_ok=True)
    pending = target.with_suffix(target.suffix + '.pending')
    # Sources are pinned public HTTPS artifacts; never attach GitHub credentials
    # to redirected release storage requests.
    for attempt in range(4):
        try:
            with urllib.request.urlopen(url, timeout=120) as src, pending.open('wb') as dst:
                shutil.copyfileobj(src, dst, 1024 * 1024)
        except (urllib.error.URLError, TimeoutError, ConnectionError, http.client.IncompleteRead) as exc:
            pending.unlink(missing_ok=True)
            if isinstance(exc, urllib.error.HTTPError) and exc.code not in (408, 429, 500, 502, 503, 504):
                raise
            if attempt == 3:
                raise
            delay = (2, 5, 10)[attempt]
            reason = f'HTTP {exc.code}' if isinstance(exc, urllib.error.HTTPError) else type(exc).__name__
            print(f'Retrying {target.name}: {reason}; attempt {attempt + 2}/4 in {delay}s', flush=True)
            time.sleep(delay)
            continue
        try:
            if (size is not None and pending.stat().st_size != size) or file_hash(pending, algorithm) != digest:
                raise ValueError('Downloaded input does not match lock: ' + target.name)
            pending.replace(target)
            return
        finally:
            pending.unlink(missing_ok=True)


def file_hash(path, algorithm):
    with path.open('rb') as f:
        return hashlib.file_digest(f, algorithm).hexdigest()


def prepare_inputs(cache, local_base=None):
    lock = guard()
    base = Path('/input/base')
    hap = base / reuse.HAP
    hap.parent.mkdir(parents=True, exist_ok=True)
    if local_base:
        if reuse.sha(local_base) != lock['base']['hapSha256']:
            raise ValueError('Local full-base HAP differs from lock')
        shutil.copyfile(local_base, hap)
    else:
        download(lock['base']['url'], hap, lock['base']['hapSha256'], size=lock['base']['hapBytes'])
    ci = base / 'ci-artifacts'
    ci.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / 'docs/graphics/proton-ci-base-source.json', ci / 'ci-source.json')
    profile.verify_artifact(hap, read(ci / 'ci-source.json'))
    # Obtain the exact GNU providers and Venus input from the verified HAP.
    link = cache / 'proton-graphics-link'
    link.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(hap) as z:
        with zipfile.ZipFile(io.BytesIO(z.read('resources/rawfile/linux-steam/hmosdeck-gnu-arm64-v1.zip'))) as gnu:
            for name in ['libc.so.6', 'libstdc++.so.6', 'libgcc_s.so.1', 'libwayland-client.so.0', 'ld-linux-aarch64.so.1']:
                (link / name).write_bytes(gnu.read('release/runtime/' + name))
        for name in ['manifest.json', 'libvulkan_virtio.so']:
            relative = 'linux-steam/bootstrap/native-game-support/' + name
            target = ROOT / 'entry/src/main/resources/rawfile' / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(z.read('resources/rawfile/' + relative))
    ge = read(ROOT / 'docs/linux-steam/gnu-proton-probe.lock.json')['distribution']
    download(ge['source'], cache / ge['file'], ge['sha512'], 'sha512', ge['bytes'])
    dxvk = read(ROOT / 'docs/graphics/proton-dxvk.lock.json')
    tools = cache / 'tools'
    version = dxvk['toolchain']['version']
    dirname = f'llvm-mingw-{version}-ucrt-ubuntu-22.04-x86_64'
    archive = tools / 'llvm-mingw.tar.xz'
    download(f'https://github.com/mstorsjo/llvm-mingw/releases/download/{version}/{dirname}.tar.xz',
             archive, dxvk['toolchain']['archiveSha256'], size=dxvk['toolchain']['bytes'])
    if not (tools / dirname / 'bin/clang').exists():
        with tarfile.open(archive) as t:
            t.extractall(tools, filter='data')
    bc = dxvk['wineBcDecoder']
    download(bc['url'], ROOT / 'thirdparty/wine' / bc['path'], bc['sha256'])
    # Mutate only this job's isolated submodule checkout, never a developer tree.
    sub = ROOT / 'thirdparty/dxvk'
    if reuse.git(sub, 'rev-parse', 'HEAD').decode().strip() != dxvk['dxvkCommit']:
        raise ValueError('DXVK gitlink mismatch')
    if reuse.git(sub, 'status', '--porcelain').strip():
        raise ValueError('DXVK checkout must be clean before applying the recorded patch')
    run('git', '-C', sub, 'apply', '--check', ROOT / 'scripts/patches/dxvk-bgra-uav.patch')
    run('git', '-C', sub, 'apply', ROOT / 'scripts/patches/dxvk-bgra-uav.patch')
    receipt = dict(baseHap=str(hap), baseHapSha256=lock['base']['hapSha256'],
                   baseCommit=lock['base']['commit'], sourceCommit=reuse.git(ROOT, 'rev-parse', 'HEAD').decode().strip(),
                   dirty=False, image=lock['container'],
                   branding=dict(name='HMDeck', sources=profile.BRANDING_INPUTS))
    profile.save(ROOT / 'ci-artifacts/graphics-input.json', receipt)
    print('PASS: clean source scope, base HAP, GNU providers and pinned external inputs')


def stage_child(cache):
    out = ROOT / profile.NATIVE
    out.mkdir(parents=True, exist_ok=True)
    for name in ['libsteamohos_gnu_child.so', 'native-child-build.json']:
        shutil.copyfile(cache / 'proton-graphics-build' / name, out / name)
    profile.validate_native()
    # The existing local packager uses /data/src only as the dependency cache.
    # CI installs from the committed lock in this same clean checkout.
    current = Path('/data/src')
    if not current.exists():
        current.parent.mkdir(parents=True, exist_ok=True)
        current.symlink_to(ROOT, target_is_directory=True)
    print('PASS: complete native source and payload receipts')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('mode', choices=['prepare', 'stage', 'guard'])
    p.add_argument('--cache', type=Path, default=Path('/data/proton-ci'))
    p.add_argument('--base-hap', type=Path)
    a = p.parse_args()
    if a.mode == 'prepare':
        prepare_inputs(a.cache, a.base_hap)
    elif a.mode == 'stage':
        stage_child(a.cache)
    else:
        guard()
