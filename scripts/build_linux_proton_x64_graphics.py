#!/usr/bin/env python3
"""Rebuild the paired GNU x64 graphics adapters and affected OHOS child in Linux.

Invoked by ci/Makefile.proton-graphics. Uses the exact locked GE DLLs, the
already-verified native-game Venus and the existing application's DXVK Legacy.
Never rewrites the immutable Proton ZIP or claims Wine/Mesa were rebuilt.
"""
import argparse, hashlib, json, shutil, subprocess, tarfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
NATIVE=ROOT/'entry/src/main/cpp/linux_steam'
FIXTURE=NATIVE/'gnu_fixture'
RAW=ROOT/'entry/src/main/resources/rawfile/linux-proton/graphics-x64'

def sha(path,algorithm='sha256'):
    h=hashlib.new(algorithm)
    with path.open('rb') as stream:
        for block in iter(lambda:stream.read(1048576),b''):h.update(block)
    return h.hexdigest()

def run(args): subprocess.run(list(map(str,args)),check=True)
def verify_gnu_symbols(outputs, link_dir, readelf):
    providers = set()
    libraries = ['libc.so.6', 'libstdc++.so.6', 'libgcc_s.so.1', 'libwayland-client.so.0', 'ld-linux-aarch64.so.1']
    for name in libraries:
        for line in subprocess.check_output([readelf, '--dyn-syms', '--wide', link_dir/name], text=True).splitlines():
            row = line.split()
            if len(row) >= 8 and row[6] != 'UND':
                symbol = row[7].replace('@@', '@')
                providers.update([symbol, symbol.split('@')[0]])
    for output in outputs:
        for line in subprocess.check_output([readelf, '--dyn-syms', '--wide', output], text=True).splitlines():
            row = line.split()
            if len(row) >= 8 and row[6] == 'UND' and row[4] != 'WEAK' and row[7] not in providers:
                raise ValueError(f'{output.name}: runtime does not provide {row[7]}')
    return {name: sha(link_dir/name) for name in libraries}

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc',type=Path,required=True);p.add_argument('--sdk',type=Path,required=True)
    p.add_argument('--build-dir',type=Path,required=True);p.add_argument('--link-dir',type=Path,required=True)
    p.add_argument('--archive',type=Path,required=True);a=p.parse_args()
    if not subprocess.check_output([a.cc,'-dumpmachine'],text=True).strip().startswith('aarch64-linux-gnu'):raise ValueError('GNU AArch64 compiler required')
    out=a.build_dir.resolve();out.mkdir(parents=True,exist_ok=True);base=out/'base';base.mkdir(exist_ok=True)
    cc=a.cc.absolute();cxx=cc.with_name(cc.name.replace('gcc', 'g++'))
    # Recompile all reused objects, recording their real source inputs. This is
    # an adapter-object build receipt, not a replacement Steam runtime manifest.
    names=['sysvipc_compat.c','popen_compat.c','socket_metadata.c','native_loader.cpp','native_loader_entry.S']
    for name in names:
        src=(NATIVE if name.startswith('native_loader') else FIXTURE)/name
        run([cc if src.suffix=='.c' else cxx,'-O2','-Wall','-Wextra','-Werror','-fPIC',*(['-std=c++17'] if src.suffix=='.cpp' else []),'-c',src,'-o',base/(name+'.o')])
    inputs=[FIXTURE/n for n in ['posix_compat.c','sysvipc_compat.c','filesystem_compat.c','popen_compat.c','socket_metadata.c','exec_compat.cpp','syscall_compat.S']]+[NATIVE/n for n in ['native_loader.cpp','native_loader_entry.S','native_loader.h']]
    (base/'runtime').mkdir(exist_ok=True)
    (base/'runtime/manifest.json').write_text(json.dumps({'scope':'freshly compiled adapter objects only','sources':{str(x.relative_to(ROOT)):sha(x) for x in inputs}},indent=2)+'\n')
    run(['python3',ROOT/'scripts/build_linux_proton_probe.py','--base-build',base,'--output',out/'probe','--cc',cc])
    RAW.mkdir(parents=True,exist_ok=True);shutil.copyfile(out/'probe/libproton_probe.so',RAW/'libproton_probe.so')
    run([cxx,'-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-shared','-fPIC',FIXTURE/'proton_vulkan_x64.cpp','-L'+str(a.link_dir),'-Wl,-rpath-link,'+str(a.link_dir),'-l:libwayland-client.so.0','-ldl','-pthread','-o',RAW/'libproton_vulkan.so'])
    lock=json.loads((ROOT/'docs/linux-steam/gnu-proton-probe.lock.json').read_text());dist=lock['distribution']
    if a.archive.stat().st_size!=dist['bytes'] or sha(a.archive,'sha512')!=dist['sha512']:raise ValueError('GE archive identity mismatch')
    needed={f'files/lib/wine/{arch}-windows/{name}' for arch in ['aarch64','x86_64'] for name in ['winevulkan.dll','vulkan-1.dll','d3dcompiler_47.dll']}
    with tarfile.open(a.archive,'r|gz') as archive:
        for member in archive:
            name='/'.join(Path(member.name).parts[1:])
            if name not in needed:continue
            if not member.isfile() or not 0<member.size<2*1024*1024:raise ValueError('Invalid PE member')
            dest=RAW/'wine'/Path(name).parent.name/Path(name).name;dest.parent.mkdir(parents=True,exist_ok=True)
            dest.write_bytes(archive.extractfile(member).read());needed.remove(name)
    if needed:raise ValueError('Incomplete graphics PE closure')
    dxvk=json.loads((out.parent/'proton-dxvk-build/dxvk-build.json').read_text())
    for name,digest in dxvk['sources'].items():
        if sha(ROOT/name)!=digest:raise ValueError('DXVK source changed after rebuild: '+name)
    for row in dxvk['files']:
        if sha(RAW/row['path'])!=row['sha256']:raise ValueError('DXVK payload changed after rebuild')
    files=[]
    for src in sorted(RAW.rglob('*')):
        if src.is_file() and src.name!='manifest.json':files.append({'path':str(src.relative_to(RAW)),'bytes':src.stat().st_size,'sha256':sha(src)})
    support=ROOT/'entry/src/main/resources/rawfile/linux-steam/bootstrap/native-game-support'
    native=json.loads((support/'manifest.json').read_text())['files']['libvulkan_virtio.so']
    if sha(support/'libvulkan_virtio.so')!=native['sha256']:raise ValueError('Native Venus identity mismatch')
    files.append({'path':'libvulkan_virtio.so',**native})
    backend=next(x for x in lock['selected'] if x['path']=='files/lib/wine/aarch64-unix/winevulkan.so')
    runtime = verify_gnu_symbols([RAW/'libproton_probe.so', RAW/'libproton_vulkan.so'], a.link_dir, cc.with_name(cc.name.replace('gcc', 'readelf')))
    sources=[Path(__file__),ROOT/'ci/Makefile.proton-graphics',ROOT/'scripts/build_linux_proton_probe.py',FIXTURE/'proton_vulkan_x64.cpp',FIXTURE/'proton_vulkan_wsi.cpp',*inputs,*sorted(NATIVE.rglob('*.h'))]
    manifest={'schemaVersion':1,'profile':'dxvk-legacy-x64-native-memory-v1','distributionSha512':dist['sha512'],'backend':{k:backend[k] for k in ['path','bytes','sha256']},'files':sorted(files,key=lambda x:x['path']),
        'sources':{str(x.relative_to(ROOT)):sha(x) for x in sources},'gnuCompilerSha256':sha(cc),'gnuRuntime':runtime,'probeBuild':json.loads((out/'probe/posix-probe-identity.json').read_text())['buildInputs'],'dxvkBuild':dxvk}
    (RAW/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    # Match the repository CMake target's complete translation-unit set.
    clang=a.sdk/'native/llvm/bin/clang++';sysroot=a.sdk/'native/sysroot'
    sources=[NATIVE/n for n in ['native_exec.cpp','gnu_child.cpp','native_loader.cpp','native_loader_entry.S','xwayland_probe.cpp','proton_probe.cpp']]
    child=out/'libsteamohos_gnu_child.so'
    run([clang,'--target=aarch64-linux-ohos','--sysroot='+str(sysroot),'-O2','-shared','-fPIC','-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-command-line-argument',*sources,'-ldl','-pthread','-Wl,-soname,libsteamohos_gnu_child.so','-o',child])
    (out/'native-child-build.json').write_text(json.dumps({'recipeSha256':sha(ROOT/'entry/src/main/cpp/CMakeLists.txt'),'sourceObjects':{str(x.relative_to(ROOT)):sha(x) for x in sources},'headers':{str(x.relative_to(ROOT)):sha(x) for x in sorted(NATIVE.rglob('*.h'))},'compilerSha256':sha(clang),'outputSha256':sha(child),'scope':'complete steamohos_gnu_child target; other HAP native libraries unchanged'},indent=2)+'\n')
    print('Built GNU x64 graphics profile and complete OHOS GNU child target')
if __name__=='__main__':main()
