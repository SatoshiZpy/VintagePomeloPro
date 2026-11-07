#!/usr/bin/env python3
"""Persistent Linux app-only HAP builds; no device access or CI dispatch."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tarfile
import time

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / '.hvigor/local-build'
LOCAL_FILES = {'scripts/local_hap.py', 'ci/Makefile.local-app'}
sys.path.insert(0, str(ROOT / 'scripts'))
import ci_app_only as reuse
from verify_ci_release import verify_artifact


def command(*args, **kwargs):
    return subprocess.check_output(list(args), **kwargs).decode().strip()


def write_changed(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def json_write(path, data):
    write_changed(path, (json.dumps(data, indent=2) + '\n').encode())


def guard(base):
    head = command('git', 'rev-parse', 'HEAD', cwd=ROOT)
    if not reuse.eligible(ROOT, base, head):
        raise ValueError('Native inputs differ from cached full build; a new full build is required')
    changed = command('git', 'diff', '--no-renames', '--name-only', 'HEAD', cwd=ROOT).splitlines()
    untracked = command('git', 'ls-files', '--others', '--exclude-standard', cwd=ROOT).splitlines()
    for name in changed + untracked:
        # An uncommitted workflow edit cannot bypass the recipe comparison.
        if name == reuse.WORKFLOW or not (reuse.app_input(name) or name in LOCAL_FILES):
            raise ValueError('Cannot reuse runtime with local change: ' + name)
    for name in subprocess.check_output(['git', 'submodule', 'status'], cwd=ROOT, text=True).splitlines():
        if name.startswith(('+', 'U')):
            raise ValueError('Changed submodule checkout; full build required')
        if name.startswith(' '):
            child = name.split()[1]
            if command('git', '-C', str(ROOT / child), 'status', '--porcelain'):
                raise ValueError('Dirty submodule: ' + child)
    return head, bool(changed or untracked)


def build_input(name):
    if name.startswith('entry/src/main/cpp/'):
        return name.startswith('entry/src/main/cpp/types/')
    return (name.startswith(('entry/', 'AppScope/')) or name in {
        'hvigorfile.ts', 'hvigor/hvigor-config.json5', 'oh-package.json5',
        'oh-package-lock.json5', reuse.WORKFLOW, 'scripts/local_hap.py',
        'scripts/ci_app_only.py', 'scripts/verify_ci_release.py', 'ci/Makefile.local-app'})


def snapshot(base, output):
    head, dirty = guard(base)
    names = command('git', 'ls-files', '--cached', '--others', '--exclude-standard', cwd=ROOT).splitlines()
    digest = hashlib.sha256()
    with tarfile.open(output, 'w') as archive:
        for name in sorted(set(names)):
            if not build_input(name):
                continue
            p = ROOT / name
            if not p.exists() or p.is_dir():
                continue
            if p.is_symlink():
                raise ValueError('Source symlink unsupported: ' + name)
            data = p.read_bytes()
            digest.update(name.encode() + b'\0' + hashlib.sha256(data).digest())
            info = tarfile.TarInfo(name)
            info.size, info.mode = len(data), (0o755 if os.access(p, os.X_OK) else 0o644)
            archive.addfile(info, io.BytesIO(data))
    return dict(commit=head, dirty=dirty, snapshotSha256=digest.hexdigest())


def init(args):
    CACHE.mkdir(parents=True, exist_ok=True)
    if (CACHE / 'config.json').exists():
        raise ValueError('Already configured; use build or doctor')
    meta = json.loads(args.metadata.read_text())
    # Validate supplied metadata against the authorized repository once at setup.
    repo = 'SatoshiZpy/VintagePomeloPro'
    official = json.loads(command('gh', 'api', f'repos/{repo}/actions/artifacts/{meta["id"]}'))
    if official != meta:
        raise ValueError('Artifact metadata differs from GitHub')
    run = json.loads(command('gh', 'api', f'repos/{repo}/actions/runs/{meta["workflow_run"]["id"]}'))
    if run['conclusion'] != 'success' or run['path'] != reuse.WORKFLOW or run['head_sha'] != meta['workflow_run']['head_sha']:
        raise ValueError('Full build did not complete successfully')
    if meta['expired'] or meta['workflow_run']['head_branch'] != 'main_proton_steamarm64':
        raise ValueError('Unexpected or expired full-build artifact')
    if reuse.sha(args.artifact) != meta['digest'].removeprefix('sha256:'):
        raise ValueError('Artifact archive digest mismatch')
    target = CACHE / 'base'
    reuse.extract(args.artifact, target)
    ci = target / 'ci-artifacts'
    if (ci / 'ci-app-only.json').exists():
        raise ValueError('Use a full build, not a reused app-only artifact')
    source = json.loads((ci / 'ci-source.json').read_text())
    if source['commit'] != meta['workflow_run']['head_sha']:
        raise ValueError('Artifact source mismatch')
    hap = target / reuse.HAP
    checked = verify_artifact(hap, source)
    if checked['sha256'] != json.loads((ci / 'ci-artifact.json').read_text())['sha256']:
        raise ValueError('Base HAP digest mismatch')
    guard(source['commit'])
    image = json.loads(command('docker', 'image', 'inspect', args.image))[0]
    if image['Os'] != 'linux' or image['Architecture'] != 'amd64':
        raise ValueError('Expected CI Linux amd64 image')
    key = hashlib.sha256(str(ROOT).encode()).hexdigest()[:12]
    config = dict(image=image['Id'], imageRef=args.image, container='vpp-local-' + key,
                  volume='vpp-local-' + key + '-data', baseCommit=source['commit'],
                  baseHap=str(hap.relative_to(CACHE)), baseHapSha256=checked['sha256'],
                  baseArtifact=meta['id'], baseRun=meta['workflow_run']['id'],
                  archiveDigest=meta['digest'])
    json_write(CACHE / 'config.json', config)
    print('Configured verified full-build runtime and pinned Linux image')


def ensure_container(config):
    name = config['container']
    found = subprocess.run(['docker', 'container', 'inspect', name], capture_output=True)
    if found.returncode:
        subprocess.run(['docker', 'volume', 'create', config['volume']], check=True, stdout=subprocess.DEVNULL)
        subprocess.run(['docker', 'create', '--name', name, '--platform', 'linux/amd64',
                        '--label', 'vpp.local.repo=' + str(ROOT),
                        '--mount', 'type=volume,src=' + config['volume'] + ',dst=/data',
                        '--mount', 'type=bind,src=' + str(CACHE) + ',dst=/input,readonly',
                        '-e', 'HOME=/data/home', '-e', 'NODE_OPTIONS=--max-old-space-size=2048',
                        '-w', '/data/src', config['image'], 'sleep', 'infinity'], check=True)
    else:
        info = json.loads(found.stdout)[0]
        if info['Image'] != config['image'] or info['Config']['Labels'].get('vpp.local.repo') != str(ROOT):
            raise ValueError('Container ownership/image mismatch')
    subprocess.run(['docker', 'start', name], check=True, stdout=subprocess.DEVNULL)
    return name


def sync_source():
    """Runs inside Linux; preserve timestamps and caches for unchanged inputs."""
    root = Path('/data/src')
    root.mkdir(parents=True, exist_ok=True)
    Path('/data/home').mkdir(parents=True, exist_ok=True)
    previous = Path('/data/source-files.json')
    old = json.loads(previous.read_text()) if previous.exists() else []
    names = []
    with tarfile.open('/input/source.tar') as archive:
        for member in archive.getmembers():
            name = member.name
            if not member.isfile() or PurePosixPath(name).is_absolute() or '..' in PurePosixPath(name).parts:
                raise ValueError('Unsafe source member')
            # Raw runtime resources are immutable and come only from the full HAP.
            if name.startswith('entry/src/main/resources/rawfile/'):
                continue
            data = archive.extractfile(member).read()
            if name == 'entry/build-profile.json5':
                text = reuse.re.sub(r'//[^\n]*', '', data.decode())
                value = json.loads(reuse.re.sub(r',\s*([}\]])', r'\1', text))
                value['buildOption'].pop('externalNativeOptions', None)
                for option in value.get('buildOptionSet', []):
                    if 'nativeLib' in option:
                        option['nativeLib']['debugSymbol']['strip'] = False
                data = (json.dumps(value, indent=2) + '\n').encode()
            if name == 'hvigor/hvigor-config.json5':
                value = json.loads(reuse.re.sub(r'//[^\n]*', '', data.decode()))
                value.setdefault('nodeOptions', {})['maxOldSpaceSize'] = 2048
                value.setdefault('execution', {}).update(parallel=False, incremental=True)
                data = (json.dumps(value, indent=2) + '\n').encode()
            write_changed(root / name, data)
            (root / name).chmod(member.mode)
            names.append(name)
    for name in set(old) - set(names):
        (root / name).unlink(missing_ok=True)
    json_write(previous, names)


def inside_prepare():
    config = json.loads(Path('/input/config.json').read_text())
    base = Path('/input') / config['baseHap']
    if reuse.sha(base) != config['baseHapSha256']:
        raise ValueError('Cached base HAP changed')
    marker = Path('/data/runtime-sha256')
    if not marker.exists() or marker.read_text() != config['baseHapSha256']:
        staged = Path('/data/payload-' + config['baseHapSha256'][:12])
        reuse.extract(base, staged, reuse.immutable)
        for src, dst in [(staged / 'libs', ROOT / 'entry/libs'),
                         (staged / 'resources/rawfile', ROOT / 'entry/src/main/resources/rawfile')]:
            if dst.exists():
                shutil.rmtree(dst)
            shutil.move(str(src), dst)
        marker.write_text(config['baseHapSha256'])
    # Match the unsigned API 23 profile used by the successful full CI build.
    workflow = (ROOT / reuse.WORKFLOW).read_text()
    text = workflow.split("cat > build-profile.json5 <<'JSON5'", 1)[1].split('          JSON5', 1)[0]
    json_write(ROOT / 'build-profile.json5', json.loads(text))
    deps = ['oh-package.json5', 'oh-package-lock.json5', 'entry/oh-package.json5',
            'entry/oh-package-lock.json5', 'hvigor/hvigor-config.json5']
    fingerprint = hashlib.sha256(b''.join((ROOT / p).read_bytes() for p in deps if (ROOT / p).exists())).hexdigest()
    marker = Path('/data/dependencies-sha256')
    if not marker.exists() or marker.read_text() != fingerprint:
        subprocess.run(['ohpm', 'install', '--all'], check=True, cwd=ROOT)
        after = hashlib.sha256(b''.join((ROOT / p).read_bytes() for p in deps if (ROOT / p).exists())).hexdigest()
        if after != fingerprint:
            raise ValueError('Dependency install changed pinned project inputs')
        marker.write_text(fingerprint)


def inside_verify():
    config = json.loads(Path('/input/config.json').read_text())
    base = Path('/input') / config['baseHap']
    reuse.restore_libraries(base, ROOT / reuse.HAP)
    count = reuse.same_payload(base, ROOT / reuse.HAP)
    source = json.loads((Path('/input/base/ci-artifacts/ci-source.json')).read_text())
    checked = verify_artifact(ROOT / reuse.HAP, source)
    checked.update(json.loads(Path('/input/snapshot.json').read_text()))
    checked.update(runtimeCommit=config['baseCommit'], immutableMembers=count,
                   image=config['image'], buildKind='local-app-only', signed=False)
    json_write(ROOT / 'local-build-report.json', checked)
    print(f'PASS: API 23 ARM64 HAP; {count} runtime members byte-identical')


def build():
    config = json.loads((CACHE / 'config.json').read_text())
    if reuse.sha(CACHE / config['baseHap']) != config['baseHapSha256']:
        raise ValueError('Cached base changed; refusing build')
    current = snapshot(config['baseCommit'], CACHE / 'source.tar')
    json_write(CACHE / 'snapshot.json', current)
    name = ensure_container(config)
    # Bootstrap sync code directly; no mount of the writable host checkout.
    code = "import sys;sys.path.insert(0,'/input');import bootstrap;bootstrap.sync_source()"
    shutil.copy2(Path(__file__), CACHE / 'bootstrap.py')
    shutil.copy2(ROOT / 'scripts/ci_app_only.py', CACHE / 'ci_app_only.py')
    shutil.copy2(ROOT / 'scripts/verify_ci_release.py', CACHE / 'verify_ci_release.py')
    started = time.monotonic()
    subprocess.run(['docker', 'exec', name, 'python3', '-c', code], check=True)
    subprocess.run(['docker', 'exec', name, 'make', '-f', 'ci/Makefile.local-app', 'hap-local-app'], check=True)
    output = ROOT / '.hvigor/outputs/local-hap' / time.strftime('%Y%m%d-%H%M%S')
    output.mkdir(parents=True, exist_ok=True)
    for src, dest in [(str(reuse.HAP), 'entry-default-unsigned.hap'), ('local-build-report.json', 'report.json')]:
        subprocess.run(['docker', 'cp', name + ':/data/src/' + src, str(output / dest)], check=True)
    report = json.loads((output / 'report.json').read_text())
    report['seconds'] = round(time.monotonic() - started, 2)
    json_write(output / 'report.json', report)
    print(f'PASS: local HAP in {report["seconds"]} seconds\n{output}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='mode', required=True)
    setup = sub.add_parser('init')
    setup.add_argument('--artifact', required=True, type=Path)
    setup.add_argument('--metadata', required=True, type=Path)
    setup.add_argument('--image', required=True)
    for mode in ['build', 'doctor', '_prepare', '_verify']:
        sub.add_parser(mode)
    args = parser.parse_args()
    if args.mode == 'init':
        init(args)
    elif args.mode == 'build':
        # Prevent overlapping source syncs, Hvigor runs and artifact exports.
        import fcntl
        CACHE.mkdir(parents=True, exist_ok=True)
        with (CACHE / 'build.lock').open('w') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            build()
    elif args.mode == 'doctor':
        config = json.loads((CACHE / 'config.json').read_text())
        name = ensure_container(config)
        subprocess.run(['docker', 'exec', name, 'bash', '-lc',
                        'uname -m; node --version; java -version; '
                        'cat /apps/harmony/sdk/default/openharmony/ets/oh-uni-package.json; '
                        'df -h /data'], check=True)
    elif args.mode == '_prepare':
        inside_prepare()
    elif args.mode == '_verify':
        inside_verify()


if __name__ == '__main__':
    main()
