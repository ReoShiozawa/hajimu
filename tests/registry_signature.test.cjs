const fs=require('node:fs'),os=require('node:os'),path=require('node:path'),assert=require('node:assert/strict');
const {spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'hajimu-registry-'));
try {
 const registry=path.join(dir,'index.json');
 fs.copyFileSync(path.join(root,'registry/index.json'),registry);fs.copyFileSync(path.join(root,'registry/index.json.sig'),registry+'.sig');
 const env={...process.env,HAJIMU_REGISTRY:registry,HAJIMU_REGISTRY_PUBLIC_KEY:path.join(root,'registry/public-key.pem')};
 assert.equal(spawnSync(path.join(root,'nihongo'),['pkg','search'],{env}).status,0);
 fs.appendFileSync(registry,' ');
 assert.notEqual(spawnSync(path.join(root,'nihongo'),['pkg','search'],{env}).status,0);
 console.log('Signed registry accepted; tampered registry rejected');
}finally{fs.rmSync(dir,{recursive:true,force:true});}
