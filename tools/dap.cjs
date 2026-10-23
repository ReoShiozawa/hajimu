#!/usr/bin/env node
'use strict';
const {spawn} = require('node:child_process');
const path = require('node:path');
const readline = require('node:readline');
const {transport} = require('./protocol.cjs');
let sequence=0, child, ready=false, configured=false, launched=false, snapshot, launchArgs;
const breakpoints=new Map(), evaluations=new Map();
function event(name,body={}) { send({seq:++sequence,type:'event',event:name,body}); }
function response(request,body={},success=true,message) { send({seq:++sequence,type:'response',request_seq:request.seq,command:request.command,success,body,message}); }
function command(name,body={}) { if (child && !child.killed) child.stdin.write(JSON.stringify({command:name,...body})+'\n'); }
function configure() {
  if (!ready || !configured) return;
  command('breakpoints',{breakpoints:[...breakpoints.values()].flat()});
  command(launchArgs.stopOnEntry?'step':'continue');
}
const send=transport(process.stdin,process.stdout,request=>{
  if (request.type!=='request') return;
  const args=request.arguments||{};
  try {
    switch(request.command) {
      case 'initialize': response(request,{supportsConfigurationDoneRequest:true,supportsConditionalBreakpoints:true,supportsEvaluateForHovers:true,supportsStepBack:false}); event('initialized'); break;
      case 'launch': {
        if (launched || typeof args.program!=='string') throw new Error('実行対象が不正です');
        launched=true; launchArgs=args;
        const program=path.resolve(args.program);
        child=spawn(args.runtimeExecutable || process.env.HAJIMU_EXECUTABLE || process.argv[2] || 'hajimu',['--debug-protocol',program],{cwd:args.cwd||path.dirname(program),env:{...process.env,...args.env},shell:false,windowsHide:true});
        child.stdout.on('data',data=>event('output',{category:'stdout',output:data.toString()}));
        readline.createInterface({input:child.stderr}).on('line',line=>{
          if (!line.startsWith('@hajimu:')) { event('output',{category:'stderr',output:line+'\n'}); return; }
          let item; try { item=JSON.parse(line.slice(8)); } catch { return; }
          if (item.event==='ready') { ready=true; configure(); }
          if (item.event==='stopped') { snapshot=item; event('stopped',{reason:item.reason,threadId:1,allThreadsStopped:true}); }
          if (item.event==='evaluation') {
            const waiting=evaluations.get(item.id);
            if(waiting) { evaluations.delete(item.id); response(waiting,{result:item.value,variablesReference:0},item.ok,item.ok?undefined:item.value); }
          }
        });
        child.stdin.on('error',()=>{});
        child.on('error',error=>{event('output',{category:'stderr',output:error.message+'\n'}); event('terminated');});
        child.on('close',code=>{ for(const pending of evaluations.values()) response(pending,{},false,'実行が終了しました'); evaluations.clear(); event('exited',{exitCode:code??1}); event('terminated'); });
        response(request); break;
      }
      case 'setBreakpoints': {
        const file=path.resolve(args.source.path);
        const points=(args.breakpoints||[]).map(bp=>({file,line:bp.line,condition:bp.condition||''}));
        breakpoints.set(file,points);
        if (snapshot) command('breakpoints',{breakpoints:[...breakpoints.values()].flat()});
        response(request,{breakpoints:points.map(bp=>({verified:true,line:bp.line}))}); break;
      }
      case 'configurationDone': configured=true; configure(); response(request); break;
      case 'threads': response(request,{threads:[{id:1,name:'main'}]}); break;
      case 'stackTrace': response(request,{stackFrames:(snapshot?.frames||[]).map((f,i)=>({id:i,name:f.name,line:f.line,column:1,source:{name:path.basename(f.file),path:f.file}})),totalFrames:snapshot?.frames.length||0}); break;
      case 'scopes': response(request,{scopes:[{name:'Locals',variablesReference:args.frameId+1,expensive:false},{name:'Globals',variablesReference:1000,expensive:false}]}); break;
      case 'variables': response(request,{variables:(args.variablesReference===1000?snapshot?.globals:snapshot?.locals[args.variablesReference-1]||[]).map(v=>({...v,type:String(v.type),variablesReference:0}))}); break;
      case 'evaluate':
        if (!snapshot) throw new Error('停止中だけ評価できます');
        evaluations.set(request.seq,request); command('evaluate',{id:request.seq,expression:args.expression}); break;
      case 'continue': case 'next': case 'stepIn': case 'stepOut':
        if (!snapshot) throw new Error('実行は停止していません');
        snapshot=null; command({continue:'continue',next:'next',stepIn:'step',stepOut:'out'}[request.command]);
        response(request,{allThreadsContinued:true}); event('continued',{threadId:1,allThreadsContinued:true}); break;
      case 'disconnect': case 'terminate': if(child) { command('disconnect'); child.kill(); } response(request); break;
      default: response(request,{},false,`未対応の要求: ${request.command}`);
    }
  } catch(error) { response(request,{},false,error.message); }
});
process.stdin.on('end',()=>{ if(child) child.kill(); });
