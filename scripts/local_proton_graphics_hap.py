#!/usr/bin/env python3
"""Build an isolated x64 GNU graphics candidate with explicitly rebuilt modules.

This is not the app-only lane and never updates its base/receipts. All other
native inputs and packaged bytes must match the verified full-build baseline.
Requires ci/Makefile.proton-graphics outputs from the configured Linux container.
"""
import argparse
import copy
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import time
import zipfile
import ci_app_only as reuse
import local_hap as local
import build_linux_proton_bootstrap as bootstrap
from verify_ci_release import verify_artifact

ROOT = Path(__file__).resolve().parents[1]
PROFILE = 'entry/src/main/resources/rawfile/linux-proton/graphics-x64/'
BOOTSTRAP = 'entry/src/main/resources/rawfile/linux-proton/bootstrap/'
CHILD = 'libs/arm64-v8a/libsteamohos_gnu_child.so'
NATIVE = 'ci-artifacts/graphics-native'
REBUILT_INPUTS = {
    # Complete OHOS child and GNU adapter receipts cover exec reclamation.
    'entry/src/main/cpp/linux_steam/native_loader.cpp',
    'entry/src/main/cpp/linux_steam/native_exec_ranges.h',
    'host_tests/native_exec_ranges_test.cpp',
    # Host-only HUD regression script; not a native input or packaged payload.
    'scripts/test_performance_hud.cjs',
    '.gitignore', 'entry/src/main/cpp/linux_steam/proton_probe.cpp',
    'entry/src/main/cpp/linux_steam/proton_graphics/pe_image.h',
    'entry/src/main/cpp/linux_steam/gnu_fixture/proton_vulkan_x64.cpp',
    # Compiled into the GNU probe; hash checked in the fresh native receipt.
    'entry/src/main/cpp/linux_steam/gnu_fixture/proton_file_mapping.h',
    'entry/src/main/cpp/linux_steam/gnu_fixture/proton_vulkan_addressing.h',
    'entry/src/main/cpp/linux_steam/gnu_fixture/proton_vulkan_bc_features.h',
    # Opt-in x64 timing, compiled and checked by both fresh native receipts.
    'entry/src/main/cpp/linux_steam/gnu_fixture/proton_vulkan_profile.h',
    'entry/src/main/cpp/linux_steam/gnu_fixture/proton_vulkan_bgra_usage.h',
    'host_tests/proton_vulkan_bgra_usage_test.cpp',
    'scripts/build_proton_dxvk.py', 'scripts/patches/dxvk-bgra-uav.patch', 'docs/graphics/proton-dxvk.lock.json',
    'scripts/build_dxvk_arm64x.sh', 'ci/Makefile.proton-dxvk',
    'scripts/build_linux_proton_probe.py', 'scripts/build_linux_proton_x64_graphics.py',
    'scripts/local_proton_graphics_hap.py', 'ci/Makefile.proton-graphics',
    'scripts/build_linux_proton_bootstrap.py', 'host_tests/proton_bootstrap_closure_test.py',
    'ci/Makefile.proton-demo', 'smoke/proton_x64_bgra_uav.c', 'smoke/proton_x64_d3d11_cube.c', 'smoke/proton_native_memory_proxy.c',
}

# Explicitly rebuilt HMDeck UI resources from approved branding commit 6d936969.
# Keep app-only guards and app identity/ABI/native checks unchanged.
BRANDING_INPUTS = {'AppScope/resources/base/element/string.json': '064b8368605d9e8a40f57f88f024a837f4b9ed8ad80e9cadbd7a11f8fefcfdfb', 'AppScope/resources/base/media/foreground.png': '3f3987f080682a79c061dbefd1ffbdb98fa69c966ccc587ee5827e2d1aefb88d', 'AppScope/resources/base/media/hmdeck_background.svg': 'e4477c3c77b0de120f56c39f142f96320decc3aebaccfc821cd07ca546c686c9', 'AppScope/resources/base/media/layered_image.json': 'f4d0bb2810d08140cbebc57a1520bf9ccf365c4948eea4cab3b7657754efce24', 'AppScope/resources/en/element/string.json': '70e2b6ba928e2c2aaefd48f615868db183d58773e0035706d6d5d9c4d4fea13e', 'entry/src/main/resources/base/element/string.json': '2b7a2465182e9d6e0e04a5ed18240ba41d082867bebf553a5b9073dad3f90071', 'entry/src/main/resources/base/media/foreground.png': '3f3987f080682a79c061dbefd1ffbdb98fa69c966ccc587ee5827e2d1aefb88d', 'entry/src/main/resources/base/media/hmdeck_background.svg': 'e4477c3c77b0de120f56c39f142f96320decc3aebaccfc821cd07ca546c686c9', 'entry/src/main/resources/base/media/layered_image.json': 'f4d0bb2810d08140cbebc57a1520bf9ccf365c4948eea4cab3b7657754efce24', 'entry/src/main/resources/base/media/startIcon.png': '3f3987f080682a79c061dbefd1ffbdb98fa69c966ccc587ee5827e2d1aefb88d', 'entry/src/main/resources/en/element/string.json': '0e0f8f1000ed206b14308658f41cc44d31fb35895399ff70d8711911a8269d8e'}

def data(path):
    return json.loads(path.read_text())

def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + '\n')

def validate_sources(manifest):
    for name, digest in manifest.items():
        path = ROOT / name
        if path.is_symlink() or reuse.sha(path) != digest:
            raise ValueError('Rebuild required: source differs from native receipt: ' + name)

def validate_native():
    profile = data(ROOT / PROFILE / 'manifest.json')
    child = data(ROOT / NATIVE / 'native-child-build.json')
    validate_sources(profile['sources'])
    validate_sources(child['sourceObjects'])
    validate_sources(child['headers'])
    if child['recipeSha256'] != reuse.sha(ROOT/'entry/src/main/cpp/CMakeLists.txt'):
        raise ValueError('Native CMake target changed')
    if reuse.sha(ROOT / NATIVE / 'libsteamohos_gnu_child.so') != child['outputSha256']:
        raise ValueError('GNU child differs from native build receipt')
    dxvk = profile['dxvkBuild']
    if dxvk['baseCommit'] != '5dff1d7b3dc3c2cd7f51356b99213bcc427b1af9' or dxvk['architecture'] != 'arm64x':
        raise ValueError('Unexpected DXVK rebuild source/ABI')
    validate_sources(dxvk['sources'])
    if dxvk['patchSha256'] != reuse.sha(ROOT/'scripts/patches/dxvk-bgra-uav.patch'):
        raise ValueError('DXVK patch differs from rebuild receipt')
    expected_dxvk = {'dxvk/d3d11.dll', 'dxvk/dxgi.dll'}
    if {row['path'] for row in dxvk['files']} != expected_dxvk:
        raise ValueError('DXVK rebuilt file inventory differs')
    for row in dxvk['files']:
        if reuse.sha(ROOT/PROFILE/row['path']) != row['sha256']:
            raise ValueError('DXVK payload differs from rebuild receipt')
    required = {'libproton_probe.so', 'libproton_vulkan.so', 'libvulkan_virtio.so'} | expected_dxvk
    required.update(f'wine/{arch}-windows/{name}' for arch in ['aarch64', 'x86_64']
                    for name in ['winevulkan.dll', 'vulkan-1.dll', 'd3dcompiler_47.dll'])
    if profile['profile'] != 'dxvk-legacy-x64-native-memory-v1' or len(profile['files']) != len(required):
        raise ValueError('Unexpected profile')
    for row in profile['files']:
        if row['path'] not in required:
            raise ValueError('Unexpected graphics member')
        required.remove(row['path'])
        path = ROOT / PROFILE / row['path']
        if row['path'] == 'libvulkan_virtio.so':
            path = ROOT/'entry/src/main/resources/rawfile/linux-steam/bootstrap/native-game-support/libvulkan_virtio.so'
        if path.stat().st_size != row['bytes'] or reuse.sha(path) != row['sha256']:
            raise ValueError('Graphics payload differs: ' + row['path'])
    expected_files = {row['path'] for row in profile['files'] if row['path'] != 'libvulkan_virtio.so'} | {'manifest.json'}
    actual_files = {str(p.relative_to(ROOT/PROFILE)) for p in (ROOT/PROFILE).rglob('*') if p.is_file()}
    if actual_files != expected_files or any(p.is_symlink() for p in (ROOT/PROFILE).rglob('*')):
        raise ValueError('Unrecorded or linked graphics payload')
    return profile, child

def validate_bootstrap():
    manifest = bootstrap.verify(ROOT)
    expected = {r['name'] for r in manifest['files']} | {'manifest.json'}
    actual = {str(p.relative_to(ROOT/BOOTSTRAP)) for p in (ROOT/BOOTSTRAP).rglob('*') if p.is_file()}
    if actual != expected or any(p.is_symlink() for p in (ROOT/BOOTSTRAP).rglob('*')):
        raise ValueError('Unrecorded or linked bootstrap payload')
    return manifest

def prepare():
    receipt = data(ROOT/'ci-artifacts/graphics-input.json')
    base = Path(receipt['baseHap'])
    if reuse.sha(base) != receipt['baseHapSha256']:
        raise ValueError('Base HAP changed')
    validate_bootstrap()
    supplement = {p.name: p.read_bytes() for p in (ROOT/BOOTSTRAP).iterdir() if p.is_file()}
    staging = ROOT/'ci-artifacts/base-payload'
    reuse.extract(base, staging, reuse.immutable)
    shutil.copytree(staging/'libs', ROOT/'entry/libs', dirs_exist_ok=True)
    shutil.copytree(staging/'resources/rawfile', ROOT/'entry/src/main/resources/rawfile', dirs_exist_ok=True)
    for name, contents in supplement.items():
        (ROOT/BOOTSTRAP/name).write_bytes(contents)
    validate_bootstrap()
    profile, child = validate_native()
    # Validate compiler/link inputs against the exact GNU runtime in this base.
    with zipfile.ZipFile(base) as hap:
        with zipfile.ZipFile(io.BytesIO(hap.read('resources/rawfile/linux-steam/hmosdeck-gnu-arm64-v1.zip'))) as runtime:
            for name, digest in profile['gnuRuntime'].items():
                if hashlib.sha256(runtime.read('release/runtime/'+name)).hexdigest() != digest:
                    raise ValueError('GNU link/runtime input mismatch: ' + name)
    shutil.copyfile(ROOT/NATIVE/'libsteamohos_gnu_child.so', ROOT/'entry'/CHILD)
    # Native targets were fully compiled separately; Hvigor builds ArkTS/resources.
    config_path = ROOT/'entry/build-profile.json5'
    text = reuse.re.sub(r'//[^\n]*', '', config_path.read_text())
    config = json.loads(reuse.re.sub(r',\s*([}\]])', r'\1', text))
    config['buildOption'].pop('externalNativeOptions', None)
    for option in config.get('buildOptionSet', []):
        if 'nativeLib' in option:
            option['nativeLib']['debugSymbol']['strip'] = False
    save(config_path, config)
    workflow = (ROOT/reuse.WORKFLOW).read_text()
    text = workflow.split("cat > build-profile.json5 <<'JSON5'", 1)[1].split('          JSON5', 1)[0]
    save(ROOT/'build-profile.json5', json.loads(text))
    hvigor_text = reuse.re.sub(r'//[^\n]*', '', (ROOT/'hvigor/hvigor-config.json5').read_text())
    hvigor = json.loads(reuse.re.sub(r',\s*([}\]])', r'\1', hvigor_text))
    hvigor.setdefault('nodeOptions', {})['maxOldSpaceSize'] = 2048
    hvigor.setdefault('execution', {}).update(parallel=False, incremental=True)
    save(ROOT/'hvigor/hvigor-config.json5', hvigor)
    for name in ['oh-package.json5', 'oh-package-lock.json5', 'entry/oh-package.json5', 'entry/oh-package-lock.json5']:
        if (ROOT/name).read_bytes() != (Path('/data/src')/name).read_bytes():
            raise ValueError('Dependency cache differs: ' + name)
    for name in ['oh_modules', 'entry/oh_modules']:
        if not (ROOT/name).exists():
            shutil.copytree(Path('/data/src')/name, ROOT/name, symlinks=True)
    print('Prepared complete rebuilt GNU child/profile; all other native bytes retain baseline provenance')

def finish():
    receipt = data(ROOT/'ci-artifacts/graphics-input.json')
    profile, child = validate_native()
    base, candidate = Path(receipt['baseHap']), ROOT/reuse.HAP
    expected_new = {'resources/rawfile/linux-proton/graphics-x64/'+str(p.relative_to(ROOT/PROFILE)):
                    reuse.sha(p) for p in (ROOT/PROFILE).rglob('*') if p.is_file()}
    bootstrap_manifest = validate_bootstrap()
    replacements = {'resources/rawfile/linux-proton/bootstrap/'+p.name: reuse.sha(p)
                    for p in (ROOT/BOOTSTRAP).iterdir() if p.is_file()}
    tmp = candidate.with_suffix('.preserved.hap')
    with zipfile.ZipFile(base) as trusted, zipfile.ZipFile(candidate) as built:
        expected = {n for n in trusted.namelist() if reuse.immutable(n)}
        actual = {n for n in built.namelist() if reuse.immutable(n)}
        if expected & expected_new.keys() or actual != expected | expected_new.keys() | replacements.keys():
            raise ValueError('Unexpected native/raw member addition or removal')
        if len(built.namelist()) != len(set(built.namelist())):
            raise ValueError('Duplicate HAP member')
        with zipfile.ZipFile(tmp, 'w') as out:
            for item in built.infolist():
                name = item.filename
                if name == CHILD:
                    out.writestr(item, (ROOT/NATIVE/'libsteamohos_gnu_child.so').read_bytes())
                else:
                    source = trusted if name.startswith('libs/') and not item.is_dir() else built
                    with source.open(name) as inp, out.open(copy.copy(source.getinfo(name)), 'w') as dst:
                        shutil.copyfileobj(inp, dst, 1048576)
    tmp.replace(candidate)
    with zipfile.ZipFile(base) as trusted, zipfile.ZipFile(candidate) as built:
        for name in expected | expected_new.keys() | replacements.keys():
            with built.open(name) as inp:
                digest = hashlib.file_digest(inp, 'sha256').hexdigest()
            if name == CHILD:
                target = child['outputSha256']
            elif name in expected_new:
                target = expected_new[name]
            elif name in replacements:
                target = replacements[name]
            else:
                with trusted.open(name) as inp:
                    target = hashlib.file_digest(inp, 'sha256').hexdigest()
            if digest != target:
                raise ValueError('Unexpected runtime byte change: ' + name)
    checked = verify_artifact(candidate, data(Path('/input/base/ci-artifacts/ci-source.json')))
    checked.update(receipt)
    checked.update(buildKind='focused-native-gnu-x64-graphics', signed=False,
                   rebuiltChild=child, graphicsProfile=profile,
                   unchangedRuntimeMembers=len(expected - replacements.keys())-1,
                   addedProfileMembers=len(expected_new), bootstrapSupplement=bootstrap_manifest)
    save(ROOT/'graphics-build-report.json', checked)
    print('PASS: API 23 ARM64 candidate; one rebuilt native target and verified graphics supplement')

def build():
    validate_sources(BRANDING_INPUTS)
    validate_bootstrap()
    config = data(local.CACHE/'config.json')
    base_commit = config['baseCommit']
    if reuse.native_recipe(ROOT, base_commit) != reuse.native_recipe(ROOT, 'HEAD'):
        raise ValueError('Base build recipe changed')
    changed = local.command('git', 'diff', '--no-renames', '--name-only', base_commit, cwd=ROOT).splitlines()
    untracked = local.command('git', 'ls-files', '--others', '--exclude-standard', cwd=ROOT).splitlines()
    for name in changed + untracked:
        if not (reuse.app_input(name) or name in REBUILT_INPUTS or name in BRANDING_INPUTS or name.startswith((PROFILE, BOOTSTRAP))):
            raise ValueError('Outside this focused rebuild scope: ' + name)
    for status in subprocess.check_output(['git','submodule','status'], cwd=ROOT, text=True).splitlines():
        if status.startswith(('+','U')):
            raise ValueError('Submodule changed')
        if status.startswith(' ') and local.command('git','-C',str(ROOT/status.split()[1]),'status','--porcelain'):
            raise ValueError('Dirty submodule')
    container = local.ensure_container(config)
    stamp = time.strftime('%Y%m%d-%H%M%S')
    output = ROOT/'.hvigor/outputs/proton-x64-hap'/stamp
    output.mkdir(parents=True)
    archive = output/'source.tar'
    names = local.command('git','ls-files','--cached','--others','--exclude-standard',cwd=ROOT).splitlines()
    names = [name for name in set(names) if local.build_input(name) or name.startswith('entry/src/main/cpp/') or
             name.startswith('scripts/') or name in {'ci/Makefile.proton-graphics', 'ci/Makefile.proton-dxvk',
                 'docs/linux-steam/gnu-proton-probe.lock.json', 'docs/linux-steam/proton-media.lock.json', 'docs/graphics/proton-dxvk.lock.json'}]
    digest = hashlib.sha256()
    with tarfile.open(archive, 'w') as tar:
        for name in sorted(names):
            p = ROOT/name
            if not p.is_file() or (name.startswith('entry/src/main/resources/rawfile/') and not name.startswith((PROFILE, BOOTSTRAP))):
                continue
            if p.is_symlink():
                raise ValueError('Source symlink: ' + name)
            contents = p.read_bytes()
            digest.update(name.encode()+b'\0'+hashlib.sha256(contents).digest())
            info = tarfile.TarInfo(name);info.size=len(contents);info.mode=0o644
            tar.addfile(info, io.BytesIO(contents))
        for name in ['libsteamohos_gnu_child.so', 'native-child-build.json']:
            tar.add(ROOT/'.hvigor/outputs/proton-x64-native'/name, arcname=NATIVE+'/'+name)
        receipt = dict(baseHap='/input/'+config['baseHap'],baseHapSha256=config['baseHapSha256'],
                       baseCommit=base_commit,sourceCommit=local.command('git','rev-parse','HEAD',cwd=ROOT),
                       dirty=bool(local.command('git','status','--porcelain',cwd=ROOT)),snapshotSha256=digest.hexdigest(),image=config['image'],
                       branding=dict(name='HMDeck', sourceCommit='6d936969', sources=BRANDING_INPUTS))
        contents = json.dumps(receipt).encode();info=tarfile.TarInfo('ci-artifacts/graphics-input.json');info.size=len(contents)
        tar.addfile(info,io.BytesIO(contents))
    destination = '/data/proton-graphics-hap-'+stamp
    subprocess.run(['docker','cp',str(archive),container+':'+destination+'.tar'],check=True)
    code = 'import tarfile;tarfile.open('+repr(destination+'.tar')+').extractall('+repr(destination)+',filter="data")'
    subprocess.run(['docker','exec',container,'python3','-c',code],check=True)
    subprocess.run(['docker','exec','-w',destination,container,'make','-f','ci/Makefile.proton-graphics','hap-proton-x64'],check=True)
    for source, name in [(str(reuse.HAP),'entry-default-unsigned.hap'),('graphics-build-report.json','report.json')]:
        subprocess.run(['docker','cp',container+':'+destination+'/'+source,str(output/name)],check=True)
    print('Candidate: '+str(output))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode',choices=['build','_prepare','_verify'])
    args = parser.parse_args()
    if args.mode == 'build':
        import fcntl
        with (local.CACHE/'build.lock').open('w') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            build()
    elif args.mode == '_prepare':
        prepare()
    else:
        finish()
