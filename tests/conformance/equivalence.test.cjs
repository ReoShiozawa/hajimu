const assert=require('node:assert/strict');
const {spawnSync}=require('node:child_process');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const root=path.resolve(__dirname,'../..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'hajimu-spec-'));
const pairs=[
 ['変数 値 = 2\n表示(値 * 3 + 1)\n','var 値 = 2\nprint(値 * 3 + 1)\n','7'],
 ['表示(長さ("日本語"))\n','print(len("日本語"))\n','3'],
 ['変数 値 = [1, 2, 3]\n表示(値[0])\n','var 値 = [1, 2, 3]\nprint(値[0])\n','1'],
 ['関数 足す(a, b):\n    戻す a + b\n終わり\n表示(足す(2, 3))\n','function 足す(a, b):\n    return a + b\nend\nprint(足す(2, 3))\n','5'],
 ['表示(偽 かつ 不明な名前)\n','print(false and 不明な名前)\n','偽'],
 ['変数 値 = 1\n関数 読む():\n    戻す 値\n終わり\n値 = 9\n表示(読む())\n','var 値 = 1\nfunction 読む():\n    return 値\nend\n値 = 9\nprint(読む())\n','9']
];
try {
 for(const [ja,en,expected] of pairs) {
  for(const [index,source] of [ja,en].entries()) {
   const file=path.join(dir,`case${index}.jp`);fs.writeFileSync(file,source);
   const result=spawnSync(path.join(root,'nihongo'),[file],{encoding:'utf8'});
   assert.equal(result.status,0,result.stderr);assert.equal(result.stdout.trim(),expected);
  }
 }
 const file=path.join(dir,'const.haj');fs.writeFileSync(file,'const x = 1\nx = 2\n');
 assert.notEqual(spawnSync(path.join(root,'nihongo'),[file]).status,0);
 console.log('Language conformance passed');
} finally {fs.rmSync(dir,{recursive:true,force:true});}
