const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const {spawnSync}=require('node:child_process');
const assert=require('node:assert/strict');
const root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'hajimu-abi-'));
try {
 for(const mode of ['good','bad','legacy']) {
  const source='#include "hajimu_plugin.h"\n'+(mode==='good'?'HAJIMU_PLUGIN_ABI_EXPORT()\n':mode==='bad'?'HAJIMU_PLUGIN_EXPORT const HajimuAbiInfo *hajimu_plugin_abi(void){static HajimuAbiInfo a={999,sizeof(HajimuAbiInfo),0,0,0,0,0};return &a;}\n':'')+
   'static Value ping(int n,Value *v){(void)n;(void)v;return hajimu_null();}\nstatic HajimuPluginFunc functions[]={{"ping",ping,0,0}};\nHAJIMU_PLUGIN_EXPORT HajimuPluginInfo *hajimu_plugin_init(void){static HajimuPluginInfo p={"abi_test","1.0.0","test","test",functions,1};return &p;}\n';
  const cfile=path.join(dir,mode+'.c'),plugin=path.join(dir,mode+'.hjp'),jpfile=path.join(dir,mode+'.jp');
  fs.writeFileSync(cfile,source);fs.writeFileSync(jpfile,`取り込む "${plugin}" として P\n表示(P["__バージョン__"])\n`);
  const compile=spawnSync('cc',['-shared','-fPIC','-I'+path.join(root,'include'),cfile,'-o',plugin],{encoding:'utf8'});
  assert.equal(compile.status,0,compile.stderr);
  const run=spawnSync(path.join(root,'nihongo'),[jpfile],{encoding:'utf8',env:{...process.env,HAJIMU_ALLOW_LEGACY_ABI:''}});
  if(mode==='good') assert.equal(run.status,0,run.stderr);else assert.notEqual(run.status,0);
 }
 console.log('Plugin ABI acceptance and incompatibility rejection passed');
} finally {fs.rmSync(dir,{recursive:true,force:true});}
