#!/usr/bin/env node
'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const [version,directory,output]=process.argv.slice(2);
if(!/^\d+\.\d+\.\d+$/.test(version||'') || !directory || !output) throw new Error('usage: generate-install-manifests VERSION ARTIFACT_DIR OUTPUT_DIR');
function find(dir,name) {
  for(const entry of fs.readdirSync(dir,{withFileTypes:true})) {
    const file=path.join(dir,entry.name);
    if(entry.isFile() && entry.name===name) return file;
    if(entry.isDirectory()) { const nested=find(file,name);if(nested)return nested; }
  }
}
function artifact(name) {
  const file=find(directory,name);
  if(!file) throw new Error(`missing artifact ${name}`);
  return {url:`https://github.com/ReoShiozawa/hajimu/releases/download/v${version}/${name}`,sha256:crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex')};
}
const installer=artifact('hajimu_setup.exe'),macArm=artifact('nihongo-macos-arm64'),macX64=artifact('nihongo-macos-x64'),linux=artifact('nihongo-linux-x64');
fs.mkdirSync(output,{recursive:true});
fs.writeFileSync(path.join(output,'hajimu.json'),JSON.stringify({version,description:'Hajimu programming language',homepage:'https://hajimu.jp',license:'MIT',architecture:{'64bit':{url:installer.url,hash:installer.sha256}},installer:{script:'Start-Process -Wait -FilePath "$dir\\hajimu_setup.exe" -ArgumentList "/S", "/D=$dir\\app"'},bin:'app\\hajimu.exe'},null,2)+'\n');
const winget={PackageIdentifier:'HajimuProject.Hajimu',PackageVersion:version,InstallerType:'nullsoft',Installers:[{Architecture:'x64',InstallerUrl:installer.url,InstallerSha256:installer.sha256.toUpperCase()}],ManifestType:'installer',ManifestVersion:'1.6.0'};
fs.writeFileSync(path.join(output,'HajimuProject.Hajimu.installer.yaml'),JSON.stringify(winget,null,2)+'\n');
fs.writeFileSync(path.join(output,'HajimuProject.Hajimu.yaml'),JSON.stringify({PackageIdentifier:'HajimuProject.Hajimu',PackageVersion:version,DefaultLocale:'en-US',ManifestType:'version',ManifestVersion:'1.6.0'},null,2)+'\n');
fs.writeFileSync(path.join(output,'HajimuProject.Hajimu.locale.en-US.yaml'),JSON.stringify({PackageIdentifier:'HajimuProject.Hajimu',PackageVersion:version,PackageLocale:'en-US',Publisher:'HajimuProject',PackageName:'Hajimu',License:'MIT',ShortDescription:'Programming language with Japanese and English syntax',ManifestType:'defaultLocale',ManifestVersion:'1.6.0'},null,2)+'\n');
fs.writeFileSync(path.join(output,'hajimu.rb'),`class Hajimu < Formula\n  desc "Programming language with Japanese and English syntax"\n  homepage "https://hajimu.jp"\n  version "${version}"\n  license "MIT"\n  on_macos do\n    if Hardware::CPU.arm?\n      url "${macArm.url}"\n      sha256 "${macArm.sha256}"\n    else\n      url "${macX64.url}"\n      sha256 "${macX64.sha256}"\n    end\n  end\n  on_linux do\n    url "${linux.url}"\n    sha256 "${linux.sha256}"\n  end\n  def install\n    bin.install Dir["nihongo-*"] .first => "hajimu"\n  end\n  test do\n    assert_match version.to_s, shell_output("#{bin}/hajimu --version")\n  end\nend\n`);
console.log('Generated Winget, Scoop, and Homebrew manifests from artifact hashes');
