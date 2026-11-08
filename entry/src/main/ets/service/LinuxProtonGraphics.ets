import { common } from '@kit.AbilityKit';
import { fileIo as fs } from '@kit.CoreFileKit';
import { ConfigurationFiles } from './ConfigurationFiles';
interface GraphicsFile { path: string; bytes: number; sha256: string; }
interface GraphicsBackend { path: string; bytes: number; sha256: string; }
interface GraphicsManifest { schemaVersion: number; profile: string; backend: GraphicsBackend; files: GraphicsFile[]; }
const ROOT = '/data/storage/el2/base/files/steamohos-native/proton-probe';
const RAW = 'linux-proton/graphics-x64';
export function validateLinuxProtonGraphics(manifest: GraphicsManifest): void {
  const required = new Set<string>(['libproton_probe.so', 'libproton_vulkan.so', 'libvulkan_virtio.so', 'dxvk/d3d11.dll', 'dxvk/dxgi.dll']);
  for (const arch of ['aarch64-windows', 'x86_64-windows']) {
    for (const name of ['winevulkan.dll', 'vulkan-1.dll', 'd3dcompiler_47.dll']) required.add(`wine/${arch}/${name}`);
  }
  if (manifest.schemaVersion !== 1 || manifest.profile !== 'dxvk-legacy-x64-native-memory-v1' ||
      manifest.backend.path !== 'files/lib/wine/aarch64-unix/winevulkan.so' ||
      !Number.isSafeInteger(manifest.backend.bytes) || manifest.backend.bytes < 1 ||
      !/^[a-f0-9]{64}$/.test(manifest.backend.sha256) || manifest.files.length !== required.size) {
    throw new Error('Proton 64 位图形支持清单无效');
  }
  for (const file of manifest.files) {
    if (!required.delete(file.path) || !Number.isSafeInteger(file.bytes) || file.bytes < 1 ||
        file.bytes > 16777216 || !/^[a-f0-9]{64}$/.test(file.sha256)) throw new Error('Proton 64 位图形支持文件无效');
  }
}
// Called inside component preparation, before Steam or any Wine game is active.
// A separate directory prevents the immutable base component from undoing the profile.
export async function ensureLinuxProtonGraphics(context: common.Context): Promise<void> {
  const manifest = JSON.parse(ConfigurationFiles.text(await context.resourceManager.getRawFileContent(`${RAW}/manifest.json`))) as GraphicsManifest;
  validateLinuxProtonGraphics(manifest);
  const backend = `${ROOT}/package/${manifest.backend.path}`;
  const stat = fs.lstatSync(backend);
  if (!stat.isFile() || stat.isSymbolicLink() || stat.size !== manifest.backend.bytes ||
      await ConfigurationFiles.fileHash(backend) !== manifest.backend.sha256) throw new Error('当前 Proton 与 64 位图形支持版本不匹配');
  ConfigurationFiles.ensure(`${ROOT}/graphics-x64`);
  for (const file of manifest.files) {
    let parent = `${ROOT}/graphics-x64`; const parts = file.path.split('/'); parts.pop();
    for (const part of parts) { parent += '/' + part; ConfigurationFiles.ensure(parent); }
    const path = `${ROOT}/graphics-x64/${file.path}`;
    if (fs.accessSync(path)) {
      const current = fs.lstatSync(path);
      if (!current.isFile() || current.isSymbolicLink()) throw new Error('Proton 64 位图形支持路径不安全');
      if (current.size === file.bytes && await ConfigurationFiles.fileHash(path) === file.sha256) continue;
    }
    const resource = file.path === 'libvulkan_virtio.so' ?
      'linux-steam/bootstrap/native-game-support/libvulkan_virtio.so' : `${RAW}/${file.path}`;
    const bytes = await context.resourceManager.getRawFileContent(resource);
    if (bytes.byteLength !== file.bytes || await ConfigurationFiles.hash(bytes) !== file.sha256) throw new Error('Proton 64 位图形支持校验失败');
    ConfigurationFiles.write(path, bytes);
  }
}
