#!/usr/bin/env node
// Offline host checks. Never builds a HAP, downloads dependencies, signs, pushes,
// launches Docker or touches a connected device.
const fs = require('node:fs'), path = require('node:path'), cp = require('node:child_process');
const root = path.resolve(__dirname, '..');
const argv = process.argv.slice(2);
function argument(name, fallback) {
  const i=argv.indexOf(name);
  if(i<0)return fallback;
  if(!argv[i+1]||argv[i+1].startsWith('--'))throw Error('Missing value for '+name);
  return argv[i+1];
}
if(argv.includes('--help')) {
  console.log('node scripts/run_local_tests.cjs [--doctor] [--scope core|startup] [--sdk PATH] [--files file.ets ...]');
  console.log('core: model/session, Wine, Steam, Proton and packaging regressions; startup: Wine startup subset.');
  console.log('Reports and full diagnostics: .hvigor/outputs/local-check/<run>/report.json');
  process.exit(0);
}
const scope=argument('--scope','core');
if(!['core','startup'].includes(scope))throw Error('Unknown scope: '+scope);
const explicitSdk=argument('--sdk',process.env.OHOS_SDK||'');
const candidates=explicitSdk?[explicitSdk]:[
  ...(process.env.TOOL_HOME?[path.join(process.env.TOOL_HOME,'sdk/default/openharmony')]:[]),
  '/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony',
  '/apps/harmony/sdk/default/openharmony'];
const sdk=candidates.map(p=>path.resolve(p)).find(p=>fs.existsSync(path.join(p,'ets/build-tools/ets-loader/node_modules/typescript/lib/typescript.js')));
if(!sdk)throw Error('SDK not found. Set OHOS_SDK or pass --sdk; no download was attempted.');
const toolHome=path.resolve(sdk,'../../..');
const node=['tools/node/bin/node','tool/node/bin/node'].map(p=>path.join(toolHome,p)).find(p=>fs.existsSync(p))||process.execPath;
const ts=path.join(sdk,'ets/build-tools/ets-loader/node_modules/typescript/lib/typescript.js');
const python=process.env.PYTHON||'python3';
const probe=cp.spawnSync(python,['--version'],{encoding:'utf8'});
if(probe.error||probe.status!==0)throw Error('Python 3 is required; set PYTHON to its executable.');
const sdkInfo=JSON.parse(fs.readFileSync(path.join(sdk,'ets/oh-uni-package.json'),'utf8'));
const environment={node,python,sdk,sdkApi:sdkInfo.apiVersion,sdkVersion:sdkInfo.version,targetApi:23};
console.log(`Local SDK API ${environment.sdkApi} (${environment.sdkVersion}); app target API 23`);
if(String(environment.sdkApi)!=='23')console.log('NOTE: local SDK differs from target; these checks do not certify API 23 packaging compatibility.');
if(argv.includes('--doctor')) {
  console.log(JSON.stringify(environment,null,2));
  console.log('Ready: SDK source diagnostics and offline regression tests. HAP/native builds are a separate environment.');
  process.exit(0);
}
const filesIndex=argv.indexOf('--files');
const selected=filesIndex>=0?argv.slice(filesIndex+1):[
  'entry/src/main/ets/service/WineEngineService.ets',
  'entry/src/main/ets/service/WineWindowManager.ets',
  ...(scope==='core'?['entry/src/main/ets/service/LinuxSteamComponentDownload.ets',
    'entry/src/main/ets/service/LinuxSteamParallelDownload.ets']:[])];
if(!selected.length||selected.some(p=>p.startsWith('--')))throw Error('--files must be last and contain .ets files');
const out=path.join(root,'.hvigor/outputs/local-check',new Date().toISOString().replace(/[:.]/g,'-')+'-'+process.pid);
fs.mkdirSync(out,{recursive:true});
const started=Date.now(),results=[];
const env={...process.env,OHOS_SDK:sdk,PYTHONDONTWRITEBYTECODE:'1'};
const source=cp.spawnSync('git',['rev-parse','HEAD'],{cwd:root,encoding:'utf8'}).stdout.trim();
function run(name,exe,args) {
  const start=Date.now();
  const result=cp.spawnSync(exe,args,{cwd:root,env,encoding:'utf8',timeout:120000,maxBuffer:8*1024*1024});
  const text=(result.stdout||'')+(result.stderr||'')+(result.error?'\n'+result.error.message:'');
  const log=path.join(out,name+'.log');fs.writeFileSync(log,text);
  const record={name,status:result.status===0&&!result.error?'PASS':'FAIL',seconds:(Date.now()-start)/1000,log};results.push(record);
  console.log(`${record.status} ${name} (${record.seconds.toFixed(2)}s)`);
  if(record.status==='FAIL')console.log(text.split('\n').slice(-14).join('\n'));
  if(name==='sdk-source')console.log(text.split('\n').find(s=>s.startsWith('SDK type/lint check:'))||'');
  return record.status==='PASS';
}
run('sdk-source',node,[path.join(root,'scripts/run_arkts_source_tests.cjs'),sdk,...selected.map(p=>path.resolve(root,p))]);
const startup=['wine_startup','wine_upgrade_guard','wine_managed_cpu','host_window_task','deck_launch_feedback','linux_steam_window_host','edge_window'];
const steam=['linux_steam_download','linux_proton_component','linux_proton_graphics','linux_proton_bootstrap','linux_steam_game_launch','linux_steam_supervisor','linux_steam_deck','steam_startup_diagnostics'];
for(const name of [...startup,...(scope==='core'?steam:[])])run(name,node,[`scripts/run_${name}_tests.cjs`,ts]);
for(const name of ['wine_mono',...(scope==='core'?['ci_app_only','ci_release']:[])])run(name,python,[`host_tests/${name}_test.py`]);
if(scope==='core') {
  const models=['model/AppModels','model/AppCatalogRules','model/PerformanceHudSettings','common/EvdevKeyNames'];
  const inputs=models.map(name=>{
    const target=path.join(out,'ts',name+'.ts');fs.mkdirSync(path.dirname(target),{recursive:true});
    fs.copyFileSync(path.join(root,'entry/src/main/ets',name+'.ets'),target);return target;
  });
  const compiled=path.join(out,'js');
  if(run('model-types',node,[path.join(path.dirname(ts),'tsc.js'),'--target','ES2020','--module','commonjs','--skipLibCheck','--rootDir',path.join(out,'ts'),'--outDir',compiled,...inputs])) {
    run('catalog',node,['scripts/run_catalog_unit_tests.cjs',path.join(compiled,'model')]);
    run('app-session',node,['scripts/run_app_session_unit_tests.cjs',compiled,ts]);
  }
}
const failed=results.filter(r=>r.status==='FAIL');
const report={scope,source,environment,selectedFiles:selected,seconds:(Date.now()-started)/1000,
  status:failed.length?'FAIL':'PASS',checks:results.length,failed:failed.length,results,
  boundaries:['Selected-file SDK diagnostics, not whole-app ArkUI/HAP compilation',
    'SDK/native behavior is mocked in host service tests; no device acceptance',
    'No Docker, downloads, signing, device commands or CI triggers']};
const reportPath=path.join(out,'report.json');fs.writeFileSync(reportPath,JSON.stringify(report,null,2)+'\n');
console.log(`${report.status}: ${results.length-failed.length}/${results.length} local checks in ${report.seconds.toFixed(2)}s`);
console.log('Report: '+reportPath);
process.exitCode=failed.length?1:0;
