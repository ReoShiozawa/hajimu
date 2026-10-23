#!/usr/bin/env node
'use strict';
const {spawn} = require('node:child_process');
const {fileURLToPath} = require('node:url');
const {transport} = require('./protocol.cjs');
const executable = process.env.HAJIMU_EXECUTABLE || process.argv[2] || 'hajimu';
const documents = new Map();
let shutdown = false;
function analyze(text, uri) {
  return new Promise((resolve, reject) => {
    const child = spawn(executable, ['--analyze', fileURLToPath(uri)], {shell:false, windowsHide:true});
    let data = '', error = '';
    const timer = setTimeout(() => { child.kill(); reject(new Error('解析がタイムアウトしました')); }, 10000);
    child.stdout.on('data', chunk => { data += chunk; if (data.length > 16*1024*1024) child.kill(); });
    child.stderr.on('data', chunk => { error = (error + chunk).slice(-4096); });
    child.on('error', e => { clearTimeout(timer); reject(e); });
    child.on('close', code => { clearTimeout(timer); try { if (code) throw new Error(error || '解析APIに対応したはじむ本体が必要です'); resolve(JSON.parse(data)); } catch (e) { reject(e); } });
    child.stdin.on('error', () => {}); child.stdin.end(text);
  });
}
function position(doc, token) {
  const line = Math.max(0, token.line - 1);
  const prefix = [...(doc.text.split('\n')[line] || '')].slice(0, Math.max(0, token.column - 1)).join('');
  return {line, character:prefix.length};
}
function range(doc, token) {
  const start = position(doc, token);
  return {start, end:{line:start.line, character:start.character+token.name.length}};
}
function bind(doc) {
  const ids = doc.analysis.identifiers;
  const used = new Set();
  for (const symbol of doc.analysis.symbols) {
    const index = ids.findIndex((token,i) => !used.has(i) && token.name === symbol.name && token.line === symbol.line && token.column >= symbol.column);
    if (index >= 0) { symbol.token = ids[index]; ids[index].declaration = symbol; used.add(index); }
  }
}
function declaration(doc, token) {
  if (token.member) return null;
  if (token.declaration) return token.declaration;
  const candidates = doc.analysis.symbols.filter(s => s.name === token.name && s.token && (s.scope===0 || (s.scope<=token.line && token.line<=s.scopeEnd)));
  return candidates.sort((a,b) => b.scope-a.scope || b.line-a.line)[0];
}
async function update(params) {
  const item=params.textDocument;
  if (!item.uri.startsWith('file:')) return;
  const text = item.text ?? params.contentChanges?.[0]?.text;
  if (typeof text !== 'string') return;
  const doc={text, version:item.version, analysis:null}; documents.set(item.uri,doc);
  try {
    doc.analysis=await analyze(text,item.uri); bind(doc);
    if (documents.get(item.uri)!==doc) return;
    const diagnostics=doc.analysis.diagnostics.map(d => {
      const start=position(doc,d), end=position(doc,{line:d.line,column:d.endColumn});
      return {range:{start,end},message:d.message,severity:1,source:'hajimu-parser'};
    });
    send({jsonrpc:'2.0',method:'textDocument/publishDiagnostics',params:{uri:item.uri,version:item.version,diagnostics}});
  } catch (error) { send({jsonrpc:'2.0',method:'window/logMessage',params:{type:1,message:error.message}}); }
}
function tokenAt(doc, p) { return doc.analysis.identifiers.find(t => { const r=range(doc,t); return r.start.line===p.line && r.start.character<=p.character && r.end.character>=p.character; }); }
async function request(method, params) {
  if (method==='initialize') return {capabilities:{textDocumentSync:1,completionProvider:{},definitionProvider:true,referencesProvider:true,renameProvider:{prepareProvider:true},documentSymbolProvider:true,documentFormattingProvider:true,hoverProvider:true},serverInfo:{name:'hajimu',version:'1.0.0'}};
  if (method==='shutdown') { shutdown=true; return null; }
  const doc=documents.get(params?.textDocument?.uri);
  if (!doc) return null;
  if (!doc.analysis) { doc.analysis=await analyze(doc.text,params.textDocument.uri); bind(doc); }
  if (method==='textDocument/documentSymbol') return doc.analysis.symbols.filter(s=>s.token).map(s=>({name:s.name,kind:s.kind==='function'?12:s.kind==='class'?5:13,location:{uri:params.textDocument.uri,range:range(doc,s.token)}}));
  if (method==='textDocument/completion') return [...new Set([...doc.analysis.symbols.map(s=>s.name), ...doc.analysis.builtins])].map(label=>({label,kind:6}));
  if (method==='textDocument/formatting') {
    // 字句トークンが同一であることを再解析で確認してからインデントだけを変える。
    let depth=0;
    const formatted=doc.text.split('\n').map(line=>{
      const content=line.trim();
      if (/^(終わり|end|それ以外|else|elif|捕獲|catch|最終|finally|場合|case|既定|default)(?:\s|:|$)/u.test(content)) depth=Math.max(0,depth-1);
      const out=content ? ' '.repeat(depth*(params.options?.tabSize||4))+content : '';
      if (/^(関数 |生成関数 |function |fn |型 |class |もし |if |それ以外|else|elif|試行|try|捕獲|catch|最終|finally|条件 |while |for |各 |each |選択 |switch |照合 |match )/u.test(content) || /繰り返す\s*$/u.test(content)) depth++;
      return out;
    }).join('\n');
    // 三重引用符を含む場合は文字列内部を変更しない。
    if (doc.text.includes('"""') || doc.text.includes("'''")) return [];
    const checked=await analyze(formatted,params.textDocument.uri);
    if (!checked.valid || JSON.stringify(checked.lexemes)!==JSON.stringify(doc.analysis.lexemes)) return [];
    return [{range:{start:{line:0,character:0},end:{line:doc.text.split('\n').length,character:0}},newText:formatted}];
  }
  const token=tokenAt(doc,params.position);
  if (!token) return null;
  const target=declaration(doc,token);
  if (method==='textDocument/hover') return target ? {contents:{kind:'plaintext',value:`${target.kind} ${target.name}`},range:range(doc,token)} : null;
  if (method==='textDocument/definition') return target ? {uri:params.textDocument.uri,range:range(doc,target.token)} : null;
  if (method==='textDocument/prepareRename') return target ? {range:range(doc,token),placeholder:token.name} : null;
  const occurrences=doc.analysis.identifiers.filter(t=>target && declaration(doc,t)===target);
  if (method==='textDocument/references') return occurrences.filter(t=>params.context?.includeDeclaration || t!==target?.token).map(t=>({uri:params.textDocument.uri,range:range(doc,t)}));
  if (method==='textDocument/rename') {
    if (!target || !/^[\p{L}_][\p{L}\p{N}_]*$/u.test(params.newName)) throw new Error('有効な識別子を指定してください');
    if (doc.analysis.symbols.some(s=>s!==target && s.scope===target.scope && s.name===params.newName)) throw new Error('同じスコープに同名の宣言があります');
    if (doc.analysis.lexemes.some(t=>(t.startsWith('"') || t.startsWith("'")) && t.includes('{') && t.includes(token.name))) throw new Error('文字列補間内の参照を含むため、自動名前変更を適用できません');
    const edits=occurrences.map(t=>({range:range(doc,t),newText:params.newName}));
    let text=doc.text;
    for (const edit of [...edits].reverse()) { const lines=text.split('\n'); const offset=lines.slice(0,edit.range.start.line).reduce((n,l)=>n+l.length+1,0)+edit.range.start.character; text=text.slice(0,offset)+edit.newText+text.slice(offset+token.name.length); }
    if (!(await analyze(text,params.textDocument.uri)).valid) throw new Error('名前変更後の構文が不正になります');
    return {documentChanges:[{textDocument:{uri:params.textDocument.uri,version:doc.version},edits}]};
  }
  throw Object.assign(new Error('Method not found'),{code:-32601});
}
const send=transport(process.stdin,process.stdout,async message=>{
  if (message.method==='exit') return process.exit(shutdown?0:1);
  if (message.method==='textDocument/didOpen'||message.method==='textDocument/didChange') return update(message.params);
  if (message.method==='textDocument/didClose') { documents.delete(message.params.textDocument.uri); send({jsonrpc:'2.0',method:'textDocument/publishDiagnostics',params:{uri:message.params.textDocument.uri,diagnostics:[]}}); return; }
  if (!Object.hasOwn(message,'id')) return;
  try { send({jsonrpc:'2.0',id:message.id,result:await request(message.method,message.params)}); }
  catch(error) { send({jsonrpc:'2.0',id:message.id,error:{code:error.code||-32603,message:error.message}}); }
});
