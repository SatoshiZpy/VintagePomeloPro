const assert = require('node:assert/strict');
const fs = require('node:fs'), path = require('node:path'), os = require('node:os');
const vm = require('node:vm'), crypto = require('node:crypto');
const ts = require(path.resolve(process.argv[2]));
const home = fs.mkdtempSync(path.join(os.tmpdir(), 'vpp-graphics-'));
const root = home + '/proton-probe', profile = root + '/graphics-x64';
const hash = data => crypto.createHash('sha256').update(data).digest('hex');
const write = (name,data) => {fs.mkdirSync(path.dirname(name),{recursive:true});fs.writeFileSync(name,data);};
const backend = Buffer.from('locked Wine Vulkan unix backend');
const files = ['libproton_probe.so','libproton_vulkan.so','libvulkan_virtio.so','dxvk/d3d11.dll','dxvk/dxgi.dll'];
for(const arch of ['aarch64-windows','x86_64-windows'])
  for(const name of ['winevulkan.dll','vulkan-1.dll','d3dcompiler_47.dll'])files.push(`wine/${arch}/${name}`);
const payload = Object.fromEntries(files.map(name=>[name,Buffer.from('content:'+name)]));
const manifest = {schemaVersion:1,profile:'dxvk-legacy-x64-native-memory-v1',
  backend:{path:'files/lib/wine/aarch64-unix/winevulkan.so',bytes:backend.length,sha256:hash(backend)},
  files:files.map(name=>({path:name,bytes:payload[name].length,sha256:hash(payload[name])}))};
let writes=0, reads=0, corrupt='';
const deps = {'@kit.AbilityKit':{},'@kit.CoreFileKit':{fileIo:{accessSync:fs.existsSync,lstatSync:fs.lstatSync}},
  './ConfigurationFiles':{ConfigurationFiles:{
    text:data=>data.toString(),hash:async data=>hash(data),fileHash:async name=>hash(fs.readFileSync(name)),
    ensure:name=>{if(!fs.existsSync(name))fs.mkdirSync(name,{recursive:true});
      const stat=fs.lstatSync(name);if(!stat.isDirectory()||stat.isSymbolicLink())throw Error('CONFIG_UNSAFE_PATH');},
    write:(name,data)=>{writes++;write(name,data);}
  }}};
const exportsObject={};
const source=fs.readFileSync(path.join(__dirname,'../entry/src/main/ets/service/LinuxProtonGraphics.ets'),'utf8')
  .replaceAll('/data/storage/el2/base/files/steamohos-native/proton-probe',root);
const output=ts.transpileModule(source,{compilerOptions:{target:ts.ScriptTarget.ES2020,module:ts.ModuleKind.CommonJS}}).outputText;
vm.runInNewContext(output,{exports:exportsObject,require:name=>{assert.ok(name in deps,name);return deps[name];}});
const context={resourceManager:{getRawFileContent:async name=>{
  if(name.endsWith('/manifest.json'))return Buffer.from(JSON.stringify(manifest));
  reads++;const relative=name.startsWith('linux-proton/graphics-x64/')?name.slice('linux-proton/graphics-x64/'.length):'libvulkan_virtio.so';
  assert.ok(relative in payload,name);return relative===corrupt?Buffer.from('corrupt'):payload[relative];
}}};
const ensure=()=>exportsObject.ensureLinuxProtonGraphics(context);
(async()=>{try{
  for(const mutate of [m=>m.files.push(m.files[0]),m=>m.files[0].path='../escape',m=>m.files[0].path=m.files[1].path,
    m=>m.files[0].bytes=0,m=>m.files[0].sha256='broken',m=>m.backend.path='../../game',m=>m.backend.bytes=-1,m=>m.schemaVersion=99]){
    const bad=JSON.parse(JSON.stringify(manifest));mutate(bad);assert.throws(()=>exportsObject.validateLinuxProtonGraphics(bad));
  }
  write(root+'/package/'+manifest.backend.path,backend);write(root+'/prefix/user.reg','user data');
  for(const name of ['dxvk/d3d11.dll','dxvk/dxgi.dll']) {
    const incomplete=JSON.parse(JSON.stringify(manifest));
    incomplete.files=incomplete.files.filter(file=>file.path!==name);
    assert.throws(()=>exportsObject.validateLinuxProtonGraphics(incomplete));
  }
  await ensure();assert.equal(writes,files.length);assert.equal(reads,files.length);
  await ensure();assert.equal(writes,files.length);assert.equal(reads,files.length,'intact payload reused');
  write(profile+'/libproton_probe.so','damage');await ensure();assert.equal(writes,files.length+1);
  fs.rmSync(root+'/graphics',{recursive:true,force:true});write(root+'/graphics/libproton_vulkan.so','base reinstall');
  await ensure();assert.equal(writes,files.length+1,'base repair must not remove companion profile');
  const name='wine/aarch64-windows/winevulkan.dll';write(profile+'/'+name,'damage');corrupt=name;
  await assert.rejects(ensure(),/校验失败/);assert.equal(fs.readFileSync(profile+'/'+name,'utf8'),'damage');corrupt='';await ensure();
  write(root+'/package/'+manifest.backend.path,'different version');const before=writes;
  await assert.rejects(ensure(),/版本不匹配/);assert.equal(writes,before);write(root+'/package/'+manifest.backend.path,backend);
  fs.unlinkSync(profile+'/libproton_probe.so');write(home+'/outside','keep');fs.symlinkSync(home+'/outside',profile+'/libproton_probe.so');
  await assert.rejects(ensure(),/不安全/);assert.equal(fs.readFileSync(home+'/outside','utf8'),'keep');
  fs.unlinkSync(profile+'/libproton_probe.so');await ensure();
  fs.renameSync(profile,home+'/saved-profile');fs.symlinkSync(home+'/saved-profile',profile);
  await assert.rejects(ensure(),/UNSAFE_PATH/);
  assert.equal(fs.readFileSync(root+'/prefix/user.reg','utf8'),'user data');
  console.log('PASS: x64 profile identity, reuse, repair, base reinstall, corruption, wrong Wine version, symlink rejection and user data preservation');
}finally{fs.rmSync(home,{recursive:true,force:true});}})().catch(error=>{console.error(error);process.exitCode=1;});
