'use strict';
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict');
const {spawnSync}=require('node:child_process');
const {pathToFileURL}=require('node:url');
async function main() {
    const root=path.resolve(__dirname,'..'),dir=fs.mkdtempSync(path.join(os.tmpdir(),'hajimu-json-'));
    const source=[
        `print(json_encode(json_decode(${JSON.stringify('{"name":"日本語","list":[1,true,null]}')})))`,
        `print(json_encode(json_decode(${JSON.stringify('[1,]')})))`,
        `print(json_encode(json_decode(${JSON.stringify('['.repeat(1024)+'0'+']'.repeat(1024))})))`,
        'var nested = 0',
        'for i from 0 to 79:',
        '    nested = [nested]',
        'end',
        'print(json_encode(nested))',
        ''
    ].join('\n');
    try {
        const filename=path.join(dir,'json.haj');fs.writeFileSync(filename,source);
        const result=spawnSync(path.join(root,'nihongo'),[filename],{encoding:'utf8',timeout:10000});
        assert.equal(result.status,0,result.stderr);
        const lines=result.stdout.trim().split('\n');
        assert.deepEqual(JSON.parse(lines[0]),{name:'日本語',list:[1,true,null]});
        assert.equal(lines[1],'null');
        assert.equal(lines[2],'null');
        assert.equal(lines[3],'null');
        if(process.env.HAJIMU_WASM_JS) {
            const js=path.resolve(process.env.HAJIMU_WASM_JS);
            const {default:create}=await import(pathToFileURL(js).href);
            const output=[],errors=[];
            const runtime=await create({wasmBinary:fs.readFileSync(js.replace(/\.js$/,'.wasm')),print:line=>output.push(line),printErr:line=>errors.push(line)});
            assert.equal(runtime.ccall('hajimu_run_source','number',['string'],[source]),0,errors.join('\n'));
            assert.deepEqual(output,lines);
            console.log('Native/WASM JSON parity and depth limits passed');
        } else {
            console.log('Native JSON parsing, encoding, and depth limits passed');
        }
    } finally {fs.rmSync(dir,{recursive:true,force:true});}
}
main().catch(error=>{console.error(error);process.exitCode=1;});
