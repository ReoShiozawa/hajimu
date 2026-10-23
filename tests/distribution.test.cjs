'use strict';
const assert=require('node:assert/strict');
const fs=require('node:fs'),os=require('node:os'),path=require('node:path'),crypto=require('node:crypto');
const {spawnSync}=require('node:child_process');
const root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'hajimu-distribution-'));
try {
    // ビルド成果物ではなく、ハッシュ計算と定義生成の契約を検証するテスト入力。
    const artifacts=path.join(dir,'artifacts'),output=path.join(dir,'manifests');
    fs.mkdirSync(artifacts);
    const hashes={};
    for(const file of ['hajimu_setup.exe','hajimu-windows-x64.zip','nihongo-macos-arm64','nihongo-macos-x64','nihongo-linux-x64']) {
        const bytes=Buffer.from(`fixture:${file}\n`);
        fs.writeFileSync(path.join(artifacts,file),bytes);
        hashes[file]=crypto.createHash('sha256').update(bytes).digest('hex');
    }
    const generator=path.join(root,'scripts/generate-install-manifests.cjs');
    assert.equal(spawnSync(process.execPath,[generator,'1.2.3',artifacts,output]).status,0);
    const scoop=JSON.parse(fs.readFileSync(path.join(output,'hajimu.json'),'utf8'));
    assert.equal(scoop.architecture['64bit'].hash,hashes['hajimu-windows-x64.zip']);
    assert.equal(scoop.bin,'hajimu.exe');
    assert(!scoop.installer);
    const winget=JSON.parse(fs.readFileSync(path.join(output,'HajimuProject.Hajimu.installer.yaml'),'utf8'));
    assert.equal(winget.Installers[0].InstallerSha256,hashes['hajimu_setup.exe'].toUpperCase());
    const formula=fs.readFileSync(path.join(output,'hajimu.rb'),'utf8');
    for(const file of ['nihongo-macos-arm64','nihongo-macos-x64','nihongo-linux-x64']) {
        assert(formula.includes(hashes[file]));
        assert(formula.includes(`/v1.2.3/${file}`));
    }
    const ruby=spawnSync('ruby',['-c',path.join(output,'hajimu.rb')]);
    if(!ruby.error) assert.equal(ruby.status,0,ruby.stderr.toString());
    fs.unlinkSync(path.join(artifacts,'nihongo-macos-x64'));
    assert.notEqual(spawnSync(process.execPath,[generator,'1.2.3',artifacts,path.join(dir,'incomplete')]).status,0);
    assert(!fs.existsSync(path.join(dir,'incomplete')));

    const setup=path.join(dir,'setup_curl.sh');
    fs.copyFileSync(path.join(root,'win/setup_curl.sh'),setup);
    const installed=path.join(dir,'curl-win64');
    fs.mkdirSync(installed);fs.writeFileSync(path.join(installed,'keep'),'existing installation');
    const corrupt=path.join(dir,'corrupt.zip');fs.writeFileSync(corrupt,'not a verified archive');
    const result=spawnSync('bash',[setup],{env:{...process.env,HAJIMU_CURL_ARCHIVE:corrupt},encoding:'utf8'});
    assert.notEqual(result.status,0);
    assert(result.stderr.includes('SHA-256'));
    assert.equal(fs.readFileSync(path.join(installed,'keep'),'utf8'),'existing installation');
    assert(!fs.readdirSync(dir).some(name=>name.startsWith('.curl-setup.')));
    console.log('Distribution hashes, manifests, and corrupt-download rejection passed');
} finally {
    fs.rmSync(dir,{recursive:true,force:true});
}
