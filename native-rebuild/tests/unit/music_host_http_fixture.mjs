import {createServer} from 'node:http';
import {readFile} from 'node:fs/promises';
import {spawn} from 'node:child_process';
import assert from 'node:assert/strict';

const [probe,host,wave]=process.argv.slice(2);
assert.ok(probe&&host&&wave,'probe, host and generated WAV paths required');
const bytes=await readFile(wave),timers=new Set();
let delayed=0,stalled=0;
const server=createServer((req,res)=>{
  if(req.url==='/stall.wav'){stalled++;return;}
  if(req.url!=='/delayed.wav')return res.writeHead(404).end();
  delayed++;
  const timer=setTimeout(()=>{
    timers.delete(timer);
    if(res.destroyed)return;
    const match=/^bytes=(\d+)-(\d*)$/.exec(req.headers.range||'');
    const start=match?Number(match[1]):0;
    const end=match&&match[2]?Math.min(bytes.length-1,Number(match[2])):bytes.length-1;
    if(start>end)return res.writeHead(416,{'Content-Range':`bytes */${bytes.length}`}).end();
    res.writeHead(match?206:200,{'Content-Type':'audio/wav','Accept-Ranges':'bytes',
      'Content-Length':end-start+1,...(match?{'Content-Range':`bytes ${start}-${end}/${bytes.length}`}:{})});
    res.end(req.method==='HEAD'?undefined:bytes.subarray(start,end+1));
  },1200);
  timers.add(timer);
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
try {
  const origin=`http://127.0.0.1:${server.address().port}`;
  const child=spawn(probe,[host,origin+'/delayed.wav',origin+'/stall.wav'],{stdio:'inherit',windowsHide:true});
  const watchdog=setTimeout(()=>child.kill(),15000);
  try {
    const code=await new Promise((resolve,reject)=>{child.once('error',reject);child.once('exit',resolve);});
    assert.equal(code,0,'real MF loopback probe passes');
    assert.ok(delayed>0&&stalled>0,'both probes actually reached the controlled HTTP source');
    console.log(`MF loopback requests: delayed=${delayed}, stalled=${stalled}`);
  } finally {clearTimeout(watchdog);}
} finally {
  for(const timer of timers)clearTimeout(timer);
  server.closeAllConnections();
  await new Promise(resolve=>server.close(resolve));
}
