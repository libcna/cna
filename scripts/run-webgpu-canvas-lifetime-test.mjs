// SPDX-License-Identifier: MS-PL
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import {spawn, execFileSync} from 'node:child_process';
import crypto from 'node:crypto';
// Node22+, Chrome and Python3/Pillow. On Linux invoke through tools/platform/run_gpu_tests_private.sh --exec.
// Args: existing Emscripten CNA build directory, optional report directory.
// The final frame is held for an exact screenshot check of the actual presented canvas.
if(typeof WebSocket==='undefined')throw Error('Node22+ with WebSocket support is required');
const build=path.resolve(process.argv[2]??'cmake-build-wasm-webgpu'),root=build;
const reports=path.resolve(process.argv[3]??build+'/canvas-lifetime-reports')+'/';
fs.mkdirSync(reports,{recursive:true});
const prefix='canvas-lifetime-'+new Date().toISOString().replaceAll(':','-');
const name='cna_webgpu_canvas_lifetime_page';
const hash=f=>crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex');
const frozen=Object.fromEntries(['js','wasm'].map(ext=>[build+'/'+name+'.'+ext,hash(build+'/'+name+'.'+ext)]));
if(process.platform==='linux'&&!process.env.XDG_RUNTIME_DIR?.startsWith('/tmp/cna-wl-'))throw Error('Use the private GPU runner');
const stages=[];
let error=null;
const pause=ms=>new Promise(r=>setTimeout(r,ms));
async function run() {
 const label='canvas',log=[],failures=[];
 const profile=fs.mkdtempSync(root+'/'+prefix+'-'+label+'-chrome-');
 const html=`<!doctype html><meta charset="utf-8"><canvas id="canvas" width="64" height="64"></canvas><script>
 window.addEventListener('error',e=>console.error('PAGE_ERROR '+e.message));
 window.addEventListener('unhandledrejection',e=>console.error('PAGE_REJECTION '+e.reason));
 window.CNA_CANVAS_HOLD=true;
 var Module={canvas:document.getElementById('canvas'),print:t=>console.log('CNAOUT '+t),
 printErr:t=>console.error('CNAERR '+t),onAbort:t=>console.error('CNAABORT '+t),onExit:c=>console.log('CNAEXIT '+c)};
 </script><script src="${name}.js"></script>`;
 const server=http.createServer((req,res)=>{
  if(req.url==='/'){res.setHeader('Content-Type','text/html');res.end(html);return;}
  const file=path.basename(new URL(req.url,'http://localhost').pathname);
  if(![name+'.js',name+'.wasm'].includes(file)){res.writeHead(404);res.end();return;}
  res.setHeader('Content-Type',file.endsWith('.wasm')?'application/wasm':'text/javascript');
  fs.createReadStream(build+'/'+file).pipe(res);
 });
 await new Promise(r=>server.listen(0,'127.0.0.1',r));
 const chromeLog=fs.openSync(reports+prefix+'-'+label+'-chrome.log','wx');
 const chrome=spawn(process.env.CNA_WEBGPU_CHROME??'google-chrome',['--headless=new','--no-sandbox','--disable-gpu-sandbox','--no-first-run','--no-default-browser-check',
 '--enable-unsafe-webgpu','--enable-features=Vulkan','--use-angle=vulkan','--ignore-gpu-blocklist',
 '--remote-debugging-port=0','--user-data-dir='+profile,'about:blank'],{stdio:['ignore',chromeLog,chromeLog]});
 let socket;
 try {
  const deadline=Date.now()+120000;
  while(!fs.existsSync(profile+'/DevToolsActivePort')) {if(chrome.exitCode!==null||Date.now()>deadline)throw Error('Chrome startup failed');await pause(100);}
  const port=fs.readFileSync(profile+'/DevToolsActivePort','utf8').split('\n')[0];
  const tabs=await (await fetch('http://127.0.0.1:'+port+'/json/list')).json();
  socket=new WebSocket(tabs.find(t=>t.type==='page').webSocketDebuggerUrl);
  await new Promise((resolve,reject)=>{socket.addEventListener('open',resolve,{once:true});socket.addEventListener('error',reject,{once:true});});
  let id=0;const pending=new Map();
  const call=(method,params={})=>new Promise((resolve,reject)=>{
   const n=++id;
   const timer=setTimeout(()=>{pending.delete(n);reject(Error('Timed out: '+method));},30000);
   pending.set(n,{resolve,reject,timer});socket.send(JSON.stringify({id:n,method,params}));
  });
  socket.addEventListener('message',e=>{
   const message=JSON.parse(e.data);
   if(message.id){const p=pending.get(message.id);pending.delete(message.id);clearTimeout(p?.timer);if(message.error)p?.reject(Error(JSON.stringify(message.error)));else p?.resolve(message.result);}
   if(message.method==='Runtime.consoleAPICalled')log.push(message.params.args.map(a=>a.value??a.description).join(' '));
   if(message.method==='Runtime.exceptionThrown'){const detail=message.params.exceptionDetails;failures.push(detail.exception?.description??detail.text);}
  });
  await call('Runtime.enable');await call('Page.enable');
  await call('Page.navigate',{url:'http://127.0.0.1:'+server.address().port+'/'});
  while(Date.now()<deadline&&!log.some(l=>l.includes('CANVAS_LIFETIME_RESULT')||/PAGE_(ERROR|REJECTION)|CNAABORT/.test(l))&&!failures.length)await pause(100);
  // Wait for asynchronous validation callbacks, bounded by a real queue completion
  // already performed by the fixture's capability probes; this is observation, not retry.
  await call('Runtime.evaluate',{expression:'new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)))',awaitPromise:true});
  let screenshot=null;
  if(log.some(l=>l.includes('CANVAS_LIFETIME_RESULT'))) {
   const rect=await call('Runtime.evaluate',{expression:'JSON.stringify((()=>{const r=document.getElementById("canvas").getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height,scale:1};})())',returnByValue:true});
   const clip=JSON.parse(rect.result.value);
   const capture=await call('Page.captureScreenshot',{format:'png',clip});
   screenshot=reports+prefix+'-canvas.png';
   fs.writeFileSync(screenshot,Buffer.from(capture.data,'base64'),{flag:'wx'});
   const pixels=JSON.parse(execFileSync('python3',['-c',
    'import json,sys;from PIL import Image;i=Image.open(sys.argv[1]).convert("RGB");p=list(i.getdata());print(json.dumps({"size":i.size,"checked":len(p),"wrong":sum(c!=(0,0,255) for c in p)}))',screenshot],{encoding:'utf8'}));
   console.log(JSON.stringify({canvasPixels:pixels,screenshot}));
   if(pixels.size[0]!==80||pixels.size[1]!==48||pixels.checked!==3840||pixels.wrong!==0)failures.push('Presented canvas pixels differ: '+JSON.stringify(pixels));
   await call('Runtime.evaluate',{expression:'window.CNA_CANVAS_CAPTURED=true'});
   await call('Runtime.evaluate',{expression:'new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)))',awaitPromise:true});
  }
  const bad=[...failures,...log.filter(l=>/PAGE_(ERROR|REJECTION)|CNAABORT|uncaptured error|\[FAIL\]/i.test(l))];
  const summary=log.find(l=>l.includes('CANVAS_LIFETIME_RESULT'));
  const accepted=!!summary&&summary.includes('checks=16 failures=0')&&!bad.length;
  const result={label,accepted,summary,bad,log,failures,profile,screenshot};
  fs.writeFileSync(reports+prefix+'-'+label+'-browser.json',JSON.stringify(result,null,2)+'\n',{flag:'wx'});
  stages.push(result);console.log(JSON.stringify({label,accepted,summary,bad}));
 } finally {
  socket?.close();chrome.kill('SIGTERM');await new Promise(r=>chrome.exitCode!==null?r():chrome.once('exit',r));fs.closeSync(chromeLog);
  await new Promise(r=>server.close(r));
 }
}
try {await run();}catch(e){error=e.stack;}
const changed=Object.entries(frozen).filter(([f,h])=>hash(f)!==h).map(([f])=>f);
const accepted=!error&&!changed.length&&stages.length===1&&stages.every(s=>s.accepted);
fs.writeFileSync(reports+prefix+'-browser-audit.json',JSON.stringify({accepted,error,changed,frozen,stages},null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({accepted,error,changed}));process.exitCode=accepted?0:1;
