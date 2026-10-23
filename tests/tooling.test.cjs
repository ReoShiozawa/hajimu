'use strict';
const assert=require('node:assert/strict');
const {spawn,spawnSync}=require('node:child_process');
const fs=require('node:fs');
const os=require('node:os');
const path=require('node:path');
const {pathToFileURL}=require('node:url');
const {transport}=require('../tools/protocol.cjs');
const root=path.resolve(__dirname,'..'), binary=path.join(root,'nihongo');
function client(script, dap=false) {
  const child=spawn(process.execPath,[path.join(root,'tools',script),binary]);
  const messages=[], waiters=[];
  const send=transport(child.stdout,child.stdin,item=>{ messages.push(item); for(const resolve of waiters.splice(0)) resolve(); });
  child.stderr.on('data',data=>process.stderr.write(data));
  return {child,send,async wait(test) {
    const deadline=Date.now()+10000;
    while(Date.now()<deadline) {
      const index=messages.findIndex(test);
      if(index>=0) return messages.splice(index,1)[0];
      await Promise.race([new Promise(r=>waiters.push(r)),new Promise(r=>setTimeout(r,30))]);
    }
    throw new Error(`timeout: ${script} ${JSON.stringify(messages)}`);
  },request(id,method,params) { send(dap?{seq:id,type:'request',command:method,arguments:params}:{jsonrpc:'2.0',id,method,params}); return this.wait(m=>dap?m.request_seq===id:m.id===id); }};
}
(async()=>{
  const analyzed=spawnSync(binary,['--analyze','unsafe.jp'],{input:'print("must not execute")\nvar x =\n',encoding:'utf8'});
  const result=JSON.parse(analyzed.stdout);
  assert.equal(result.valid,false); assert.ok(result.diagnostics.length); assert.equal(analyzed.stderr,'');
  const lsp=client('lsp.cjs');
  try {
    assert.equal((await lsp.request(1,'initialize',{})).result.capabilities.definitionProvider,true);
    const uri=pathToFileURL(path.join(root,'tests','virtual.haj')).href;
    lsp.send({jsonrpc:'2.0',method:'textDocument/didOpen',params:{textDocument:{uri,version:1,text:'var 数 = 1\nprint(数)\n'}}});
    assert.deepEqual((await lsp.wait(m=>m.method==='textDocument/publishDiagnostics')).params.diagnostics,[]);
    const definition=await lsp.request(2,'textDocument/definition',{textDocument:{uri},position:{line:1,character:6}});
    assert.equal(definition.result.range.start.line,0);
    const references=await lsp.request(3,'textDocument/references',{textDocument:{uri},position:{line:1,character:6},context:{includeDeclaration:true}});
    assert.equal(references.result.length,2);
    const rename=await lsp.request(4,'textDocument/rename',{textDocument:{uri},position:{line:1,character:6},newName:'合計'});
    assert.equal(rename.result.documentChanges[0].edits.length,2);
    await lsp.request(5,'shutdown');
  } finally { lsp.child.kill(); }
  const temporary=fs.mkdtempSync(path.join(os.tmpdir(),'hajimu-dap-'));
  const file=path.join(temporary,'test.haj');
  fs.writeFileSync(file,'var x = 1\nfunction add(a):\n    var b = a + 1\n    return b\nend\nx = add(x)\nprint(x)\n');
  const dap=client('dap.cjs',true);
  try {
    await dap.request(1,'initialize',{});
    assert.equal((await dap.request(2,'launch',{program:file})).success,true);
    await dap.request(3,'setBreakpoints',{source:{path:file},breakpoints:[{line:3,condition:'a == 1'}]});
    await dap.request(4,'configurationDone',{});
    await dap.wait(m=>m.event==='stopped');
    const stack=(await dap.request(5,'stackTrace',{})).body.stackFrames;
    assert.equal(stack[0].line,3); assert.ok(stack.length>=2);
    const locals=(await dap.request(6,'variables',{variablesReference:1})).body.variables;
    assert.equal(locals.find(v=>v.name==='a').value,'1');
    assert.equal((await dap.request(7,'evaluate',{expression:'a + 2'})).body.result,'3');
    assert.equal((await dap.request(8,'evaluate',{expression:'print("unsafe")'})).success,false);
    await dap.request(9,'next',{}); await dap.wait(m=>m.event==='stopped');
    assert.equal((await dap.request(10,'stackTrace',{})).body.stackFrames[0].line,4);
    await dap.request(11,'continue',{}); await dap.wait(m=>m.event==='terminated');
  } finally { dap.child.kill(); fs.rmSync(temporary,{recursive:true,force:true}); }
  console.log('Compiler analysis, LSP, and DAP integration passed');
})().catch(error=>{console.error(error);process.exit(1);});
