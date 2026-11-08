#!/usr/bin/env python3
"""Full DXVK ARM64X rebuild, pinned source + reviewed patch, for GNU Wine.

The Vulkan import archive is explicitly generated from the real core entrypoint
exported by the pinned GE vulkan-1.dll; it is not a Wine build artifact.
"""
import argparse, hashlib, json, os, subprocess, shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE='5dff1d7b3dc3c2cd7f51356b99213bcc427b1af9'
PATCH='scripts/patches/dxvk-bgra-uav.patch'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def run(*args,**kwargs):return subprocess.run(list(map(str,args)),check=True,**kwargs)
def main():
 p=argparse.ArgumentParser();p.add_argument('--build-dir',type=Path,required=True);p.add_argument('--toolchain',type=Path,required=True);a=p.parse_args()
 src=ROOT/'thirdparty/dxvk';out=a.build_dir;out.mkdir(parents=True,exist_ok=True)
 lock=json.loads((ROOT/'docs/graphics/proton-dxvk.lock.json').read_text())
 dep=lock['wineBcDecoder'];header=ROOT/'thirdparty/wine'/dep['path']
 assert sha(header)==dep['sha256'], 'Wine BC decoder differs from pinned source'
 contents=header.read_bytes()
 assert hashlib.sha1(b'blob '+str(len(contents)).encode()+b'\0'+contents).hexdigest()==dep['gitBlobSha1']
 archive=a.toolchain.parent/'llvm-mingw.tar.xz'
 assert archive.stat().st_size==lock['toolchain']['bytes'] and sha(archive)==lock['toolchain']['archiveSha256'], 'Toolchain archive changed'
 assert subprocess.check_output(['git','-C',str(src),'rev-parse','HEAD'],text=True).strip()==BASE
 delta=subprocess.check_output(['git','-C',str(src),'diff','--binary'])
 assert delta==(ROOT/PATCH).read_bytes(), 'DXVK source differs from reviewed patch'
 assert not subprocess.check_output(['git','-C',str(src),'ls-files','--others','--exclude-standard']).strip(), 'Unrecorded DXVK source files'
 # Match the Vulkan loader's sole direct import. All other functions resolve
 # through vkGetInstanceProcAddr; no capability or implementation is stubbed.
 definition=out/'vulkan-1.def';definition.write_text('LIBRARY vulkan-1.dll\nEXPORTS\n vkGetInstanceProcAddr\n')
 implib=out/'libvulkan-1-arm64x.a'
 run(a.toolchain/'bin/llvm-dlltool','-m','arm64ec','-d',definition,'-N',definition,'-l',implib)
 env=dict(os.environ,LLVM_MINGW=str(a.toolchain),BUILD_DIR=str(out),WINE_ARCH='aarch64',DXVK_VULKAN_IMPLIB=str(implib))
 run('bash',ROOT/'scripts/build_dxvk_arm64x.sh',env=env)
 raw=ROOT/'entry/src/main/resources/rawfile/linux-proton/graphics-x64/dxvk';raw.mkdir(parents=True,exist_ok=True)
 files=[]
 for name in ['d3d11.dll','dxgi.dll']:
  source=out/'dxvk/legacy/arm64x/bin'/name;shutil.copy2(source,raw/name)
  files.append(dict(path='dxvk/'+name,bytes=source.stat().st_size,sha256=sha(source)))
 names=subprocess.check_output(['git','-C',str(src),'ls-files'],text=True).splitlines()
 tree=hashlib.sha256()
 for name in sorted(names):
  path=src/name
  if path.is_file():tree.update(name.encode()+b'\0'+hashlib.sha256(path.read_bytes()).digest())
 source_names=['docs/graphics/proton-dxvk.lock.json',PATCH,'scripts/build_proton_dxvk.py','scripts/build_dxvk_arm64x.sh','scripts/arm64x_build_cache.sh','scripts/env.sh','ci/Makefile.proton-dxvk']
 receipt=dict(schemaVersion=1,baseCommit=BASE,patchSha256=sha(ROOT/PATCH),sourceTreeSha256=tree.hexdigest(),architecture='arm64x',
  sources={name:sha(ROOT/name) for name in source_names},compilerSha256=sha(a.toolchain/'bin/clang'),
  vulkanImportDefinitionSha256=sha(definition),wineBcDecoder=dep,toolchain=lock['toolchain'],compilerBackendSha256=sha(a.toolchain/'bin/clang-23'),files=files,scope='complete DXVK Legacy ARM64X build; pinned GE Wine and Venus unchanged')
 (out/'dxvk-build.json').write_text(json.dumps(receipt,indent=2)+'\n')
 print('PASS: complete DXVK Legacy ARM64X build with BGRA UAV emulation')
if __name__=='__main__':main()
