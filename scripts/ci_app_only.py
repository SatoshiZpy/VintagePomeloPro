#!/usr/bin/env python3
"""Reuse a verified full CI HAP only when every native input is unchanged.

The runtime's original source receipts stay intact. A separate app-only receipt
records the new ArkTS commit, base build and byte-for-byte payload comparison.
"""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import urllib.error
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
STATE = Path('ci-artifacts/app-only-base.json')
HAP = Path('entry/build/default/outputs/default/entry-default-unsigned.hap')
WORKFLOW = '.github/workflows/build.yml'
# Only orchestration for this fast path is excluded. Native build scripts,
# manifests, locks, C++ types, resources, package locks and gitlinks are inputs.
FAST_FILES = {WORKFLOW, 'scripts/ci_app_only.py', 'ci/Makefile.app-only'}
RECIPE_STEPS = ('Provision unsigned build-profile.json5',
                'Ensure CI build dependencies present',
                'Install ArkTS project dependencies', 'Build HAP')


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], stderr=subprocess.PIPE)


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def app_input(name):
    p = PurePosixPath(name)
    return (name in FAST_FILES or
            (name.startswith('entry/src/main/ets/') and p.suffix == '.ets') or
            (name.startswith('docs/') and p.suffix == '.md') or
            name.startswith('host_tests/') or
            (name.startswith('scripts/run_') and name.endswith('_tests.cjs')))


def native_recipe(root, commit):
    text = git(root, 'show', f'{commit}:{WORKFLOW}').decode()
    image = re.search(r'^      image: (.+)$', text, re.M)
    if not image:
        raise ValueError('Missing build container identity')
    chunks = [image.group(1)]
    for name in RECIPE_STEPS:
        match = re.search(r'^      - name: ' + re.escape(name) + r'\n(.*?)(?=^      - |\Z)', text, re.M | re.S)
        if not match:
            raise ValueError('Missing full-build recipe: ' + name)
        # An if condition only chooses this lane; it cannot change its recipe.
        lines = [line for line in match.group(1).splitlines()
                 if not line.startswith('        if:') and not line.lstrip().startswith('#')]
        chunks.append('\n'.join(lines).strip())
    return chunks


def eligible(root, base, head):
    if not re.fullmatch(r'[0-9a-f]{40}', base) or not re.fullmatch(r'[0-9a-f]{40}', head):
        return False
    names = git(root, 'diff', '--no-renames', '--name-only', '-z', base, head).decode().split('\0')
    if any(not app_input(name) for name in names if name):
        return False
    # Explicitly include the container, SDK profile, dependency installation
    # and the full make command/env, even though the workflow also adds routing.
    return native_recipe(root, base) == native_recipe(root, head)


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def api(path):
    request = urllib.request.Request('https://api.github.com/' + path,
        headers={'Authorization': 'Bearer ' + os.environ['GH_TOKEN'],
                 'Accept': 'application/vnd.github+json'})
    with urllib.request.urlopen(request, timeout=45) as response:
        return json.load(response)


def download(repo, artifact, destination):
    request = urllib.request.Request(
        f'https://api.github.com/repos/{repo}/actions/artifacts/{artifact["id"]}/zip',
        headers={'Authorization': 'Bearer ' + os.environ['GH_TOKEN']})
    try:
        urllib.request.build_opener(NoRedirect).open(request, timeout=45)
        raise ValueError('Missing GitHub artifact redirect')
    except urllib.error.HTTPError as error:
        if error.code != 302:
            raise ValueError('Artifact download unavailable') from None
        location = error.headers['Location']
    if not location.startswith('https://'):
        raise ValueError('Invalid artifact download transport')
    # A new request avoids forwarding the GitHub credential to blob storage.
    with urllib.request.urlopen(location, timeout=120) as response, destination.open('wb') as output:
        shutil.copyfileobj(response, output, 1024 * 1024)
    if destination.stat().st_size != artifact['size_in_bytes'] or \
            'sha256:' + sha(destination) != artifact.get('digest'):
        raise ValueError('GitHub artifact digest mismatch')


def extract(archive, target, include=lambda name: True):
    seen = set()
    with zipfile.ZipFile(archive) as z:
        for item in z.infolist():
            p = PurePosixPath(item.filename)
            if (p.is_absolute() or '..' in p.parts or '\\' in item.filename or
                    item.filename in seen or (item.external_attr >> 16) & 0o170000 == 0o120000):
                raise ValueError('Unsafe artifact member')
            seen.add(item.filename)
            if item.is_dir() or not include(item.filename):
                continue
            destination = target / item.filename
            destination.parent.mkdir(parents=True, exist_ok=True)
            with z.open(item) as source, destination.open('wb') as out:
                shutil.copyfileobj(source, out, 1024 * 1024)


def select():
    STATE.parent.mkdir(parents=True, exist_ok=True)
    STATE.unlink(missing_ok=True)
    if os.environ.get('FORCE_FULL', '').lower() == 'true':
        print('Full build explicitly required')
        return False
    repo, branch = os.environ['GITHUB_REPOSITORY'], os.environ['GITHUB_REF_NAME']
    head = git(ROOT, 'rev-parse', 'HEAD').decode().strip()
    artifacts = api(f'repos/{repo}/actions/artifacts?per_page=100')['artifacts']
    for artifact in artifacts:
        run = artifact.get('workflow_run', {})
        base = run.get('head_sha', '')
        if (artifact['expired'] or run.get('head_branch') != branch or
                artifact['name'] != 'VintagePomeloPro-arm64-v8a-unsigned-' + base):
            continue
        try:
            git(ROOT, 'cat-file', '-e', base)
        except subprocess.CalledProcessError:
            git(ROOT, 'fetch', '--depth=1', '--no-tags', 'origin', base)
        if not eligible(ROOT, base, head):
            continue
        completed = api(f'repos/{repo}/actions/runs/{run["id"]}')
        if (completed['conclusion'] != 'success' or completed['head_sha'] != base or
                completed['event'] not in ('push', 'workflow_dispatch') or completed['path'] != WORKFLOW):
            continue
        directory = ROOT / 'workspace_temp' / f'app-only-base-{artifact["id"]}'
        directory.mkdir(parents=True, exist_ok=True)
        archive = directory / 'artifact.zip'
        download(repo, artifact, archive)
        extract(archive, directory / 'unpacked')
        ci = directory / 'unpacked/ci-artifacts'
        # Never build an unbounded chain of reused artifacts: use a full build.
        if (ci / 'ci-app-only.json').exists():
            continue
        source = json.loads((ci / 'ci-source.json').read_text())
        if source['commit'] != base:
            raise ValueError('Base artifact source mismatch')
        hap = directory / 'unpacked' / HAP
        from verify_ci_release import verify_artifact
        checked = verify_artifact(hap, source)
        if checked['sha256'] != json.loads((ci / 'ci-artifact.json').read_text())['sha256']:
            raise ValueError('Base HAP digest mismatch')
        current = json.loads(Path('ci-artifacts/ci-source.json').read_text())
        if source['submodules'] != current['submodules'] or source['patches'] != current['patches'] or source['app'] != current['app']:
            raise ValueError('Base runtime inputs differ')
        shutil.copy2(ci / 'ci-source.json', 'ci-artifacts/ci-runtime-source.json')
        STATE.write_text(json.dumps(dict(baseCommit=base, appCommit=head,
            baseRun=run['id'], baseArtifact=artifact['id'], baseHap=str(hap),
            baseHapSha256=checked['sha256'], archiveDigest=artifact['digest']), indent=2) + '\n')
        print(f'Reusing verified full build {run["id"]} ({base[:12]}); native inputs unchanged')
        return True
    print('No compatible verified full build; using full source build')
    return False


def state():
    value = json.loads(STATE.read_text())
    head = git(ROOT, 'rev-parse', 'HEAD').decode().strip()
    if head != value['appCommit'] or not eligible(ROOT, value['baseCommit'], head):
        raise ValueError('App-only source changed after selection')
    if sha(Path(value['baseHap'])) != value['baseHapSha256']:
        raise ValueError('Base HAP changed after selection')
    return value


def immutable(name):
    return name.startswith(('libs/', 'resources/rawfile/')) and not name.endswith('/')


def stage():
    value = state()
    git(ROOT, 'diff', '--exit-code', 'HEAD', '--ignore-submodules=all')
    base = Path(value['baseHap'])
    temporary = ROOT / 'workspace_temp/app-only-payload'
    if temporary.exists():
        raise ValueError('Use a clean staging directory')
    extract(base, temporary, immutable)
    for source, dest in [(temporary / 'libs', ROOT / 'entry/libs'),
                         (temporary / 'resources/rawfile', ROOT / 'entry/src/main/resources/rawfile')]:
        shutil.copytree(source, dest, dirs_exist_ok=True)
    # Build ArkTS/resources only; package the verified native bytes unchanged.
    profile = ROOT / 'entry/build-profile.json5'
    text = re.sub(r'//[^\n]*', '', profile.read_text())
    config = json.loads(re.sub(r',\s*([}\]])', r'\1', text))
    del config['buildOption']['externalNativeOptions']
    for option in config.get('buildOptionSet', []):
        if 'nativeLib' in option:
            option['nativeLib']['debugSymbol']['strip'] = False
    profile.write_text(json.dumps(config, indent=2) + '\n')
    print('Staged verified runtime; native compilation and stripping disabled for app-only build')


def restore_libraries(base, candidate):
    # Hvigor can process ELF metadata even with nativeLib.debugSymbol.strip=false.
    # Package the trusted full-build libraries verbatim AFTER its package step;
    # keep the newly compiled ArkTS/resources. The unsigned HAP is signed later.
    temporary = candidate.with_suffix('.native-preserved.hap')
    try:
        with zipfile.ZipFile(base) as trusted, zipfile.ZipFile(candidate) as built:
            expected = {n for n in trusted.namelist() if immutable(n)}
            actual = {n for n in built.namelist() if immutable(n)}
            if expected != actual or len(built.namelist()) != len(set(built.namelist())):
                raise ValueError('Runtime payload member set changed')
            with zipfile.ZipFile(temporary, 'w') as out:
                out.comment = built.comment
                for item in built.infolist():
                    source = trusted if item.filename.startswith('libs/') and not item.is_dir() else built
                    info = copy.copy(source.getinfo(item.filename))
                    with source.open(item.filename) as data, out.open(info, 'w', force_zip64=True) as destination:
                        shutil.copyfileobj(data, destination, 1024 * 1024)
        temporary.replace(candidate)
    finally:
        temporary.unlink(missing_ok=True)


def same_payload(base, candidate):
    with zipfile.ZipFile(base) as before, zipfile.ZipFile(candidate) as after:
        expected = {n for n in before.namelist() if immutable(n)}
        actual = {n for n in after.namelist() if immutable(n)}
        if expected != actual:
            raise ValueError('Runtime payload member set changed')
        for name in sorted(expected):
            with before.open(name) as a, after.open(name) as b:
                while True:
                    chunk = a.read(1024 * 1024)
                    if chunk != b.read(1024 * 1024):
                        raise ValueError('Runtime payload bytes changed: ' + name)
                    if not chunk:
                        break
        if not after.read('ets/modules.abc'):
            raise ValueError('Missing compiled ArkTS')
    return len(expected)


def verify():
    value = state()
    count = same_payload(Path(value['baseHap']), HAP)
    source = json.loads(Path('ci-artifacts/ci-runtime-source.json').read_text())
    from verify_ci_release import verify_artifact
    checked = verify_artifact(HAP, source)
    # Keep base sourceBuild and binary receipts intact rather than claiming the
    # runtime was rebuilt from the new app commit.
    checked.update(commit=value['appCommit'], runtimeCommit=value['baseCommit'], buildKind='app-only')
    Path('ci-artifacts/ci-artifact.json').write_text(json.dumps(checked, indent=2) + '\n')
    receipt = {k: v for k, v in value.items() if k != 'baseHap'}
    receipt.update(immutableMembers=count, payloadPreserved=True, hapSha256=checked['sha256'])
    Path('ci-artifacts/ci-app-only.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(f'PASS: {count} native/runtime members byte-identical; new ArkTS HAP verified')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['select', 'stage', 'repack', 'verify'])
    args = parser.parse_args()
    if args.mode == 'select':
        try:
            fast = select()
        except (OSError, ValueError, KeyError, subprocess.CalledProcessError, zipfile.BadZipFile):
            STATE.unlink(missing_ok=True)
            print('Reusable artifact unavailable or invalid; falling back to full build')
            fast = False
        if os.environ.get('GITHUB_OUTPUT'):
            with open(os.environ['GITHUB_OUTPUT'], 'a') as out:
                out.write('fast=' + str(fast).lower() + '\n')
    elif args.mode == 'stage':
        stage()
    elif args.mode == 'repack':
        value = state()
        restore_libraries(Path(value['baseHap']), HAP)
        same_payload(Path(value['baseHap']), HAP)
        print('Restored original native bytes after SDK packaging; runtime comparison passed')
    else:
        verify()


if __name__ == '__main__':
    main()
