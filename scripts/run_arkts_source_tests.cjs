// Targeted, no-emit SDK type/lint checks; this is not a Hvigor HAP build.
const fs = require('node:fs'), path = require('node:path');
const sdk = path.resolve(process.argv[2]);
const ts = require(path.join(sdk, 'ets/build-tools/ets-loader/node_modules/typescript'));
const root = process.cwd();
const selected = process.argv.slice(3).map(p => path.resolve(p));
if (!selected.length) throw Error('Pass at least one ArkTS source file');
for (const file of selected) {
  if (!file.endsWith('.ets') || !fs.statSync(file).isFile()) throw Error('Expected an existing .ets file: '+file);
}
// SDK NAPI declarations require needDoArkTsLinter; otherwise .so imports
// are intentionally treated as unverified and produce misleading any types.
const api = path.join(sdk, 'ets/api'), kits = path.join(sdk, 'ets/kits');
const declarations = path.join(sdk, 'ets/build-tools/ets-loader/declarations');
const ambient = [declarations, path.join(api, 'global')].flatMap(dir => fs.readdirSync(dir)
  .filter(n => n.endsWith('.d.ts')).map(n => path.join(dir, n)));
const native = {'libentry.so':'libentry', 'libsmoke_napi.so':'smoke_napi', 'libsteamdecompress.so':'libsteamdecompress'};
const options = {noEmit:true,needDoArkTsLinter:true,isCompatibleVersion:false,strict:true,skipLibCheck:true,allowNonTsExtensions:true,types:[],
  target:ts.ScriptTarget.ES2020,module:ts.ModuleKind.CommonJS,moduleResolution:ts.ModuleResolutionKind.NodeJs,
  allowSyntheticDefaultImports:true,experimentalDecorators:true,useUnknownInCatchVariables:false};
const host = ts.createCompilerHost(options);
const getSource = host.getSourceFile;
host.getSourceFile = (...args) => {const file=getSource(...args);if(file)file.version='0';return file;};
host.resolveModuleNames = (names, from) => names.map(name => {
  let candidates = [];
  if (name.startsWith('@kit.')) candidates = [path.join(kits,name+'.d.ts')];
  else if (name.startsWith('@ohos.') || name.startsWith('@system.') || name.startsWith('@arkts.'))
    candidates = [path.join(api,name+'.d.ts'),path.join(api,name+'.d.ets')];
  else if (native[name]) candidates = [path.join(root,'entry/src/main/cpp/types',native[name],'Index.d.ts')];
  else if (name.startsWith('.')) candidates = ['.ets','.ts','.d.ts','/index.ets','/index.ts'].map(ext => path.resolve(path.dirname(from),name)+ext);
  const found = candidates.find(p => fs.existsSync(p));
  if (found) return {resolvedFileName:found,isExternalLibraryImport:!!native[name],extension:found.endsWith('.ets')?ts.Extension.Ets:(found.endsWith('.d.ts')?ts.Extension.Dts:ts.Extension.Ts)};
  return ts.resolveModuleName(name,from,options,host).resolvedModule;
});
const builder = ts.createSemanticDiagnosticsBuilderProgram([...selected,...ambient],options,host);
const program = builder.getProgram();
const belongs = d => d.file && selected.includes(path.resolve(d.file.fileName));
const diagnostics = [...program.getSyntacticDiagnostics(),...program.getSemanticDiagnostics(),
  ...ts.ArkTSLinter_1_1.runArkTSLinter(builder,undefined,undefined,'ArkTS_1_1')].filter(belongs);
let errors=0,warnings=0;
for(const d of diagnostics) {
 if(d.category===ts.DiagnosticCategory.Error) errors++; else warnings++;
 const pos=d.file.getLineAndCharacterOfPosition(d.start);
 console.log(`${path.relative(root,d.file.fileName)}:${pos.line+1}:${pos.character+1} ${ts.DiagnosticCategory[d.category]} ${d.code} ${ts.flattenDiagnosticMessageText(d.messageText,' ')}`);
}
console.log(`SDK type/lint check: ${selected.length} selected files, ${errors} errors, ${warnings} warnings`);
process.exitCode=errors?1:0;
