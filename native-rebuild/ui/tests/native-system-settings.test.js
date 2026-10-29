import test from 'node:test';
import assert from 'node:assert/strict';
import {createBridge,validHostMessage} from '../bridge.js';
import {initialState,reduce} from '../state.js';
import {card} from '../pages.js';
import {taskbarPage,taskbarStatus,taskbarNotice} from '../taskbar.js';
import {minimizePage,minimizeStatus,minimizeNotice} from '../minimize.js';

const taskbarMessage=(fields={})=>({
  version:1,type:'taskbar-state',enabled:false,running:false,transparentTaskbar:true,
  autoHideTaskbar:false,hideBehavior:'auto-hide',applying:false,pendingRecovery:false,taskbars:0,mode:'unavailable',
  error:'',loadStatus:'fresh',revision:0,...fields,
});
const minimizeMessage=(fields={})=>({
  version:1,type:'minimize-state',enabled:false,running:false,durationMs:450,
  restore:true,launch:false,multiMonitor:false,unhideTaskbar:true,unhideDurationMs:450,
  engine:'modern',x64:false,x86:false,hosts:0,hooks:0,workers:0,entries:0,pendingCleanup:0,systemError:0,
  error:'',loadStatus:'fresh',result:'snapshot',saved:false,...fields,
});
const enabledMinimize={enabled:true,running:true,x64:true,x86:true,hosts:2,hooks:4};

test('taskbar and animation commands are exact and reject extra fields and coercion',()=>{
  const sent=[],bridge=createBridge({postMessage:value=>sent.push(value)});
  for(const key of ['enabled','transparent','autoHide'])
    for(const value of [false,true])assert.equal(bridge.send('taskbar',key,value),true);
  for(const value of ['auto-hide','fully-hidden'])assert.equal(bridge.send('taskbar','hideBehavior',value),true);
  for(const key of ['enabled','restore','launch','multiMonitor','unhideTaskbar'])
    for(const value of [false,true])assert.equal(bridge.send('minimize',key,value),true);
  for(const [key,values] of [['durationMs',[50,450,2000]],['unhideDurationMs',[0,450,5000]],['engine',['modern','classic']]])
    for(const value of values)assert.equal(bridge.send('minimize',key,value),true);
  for(const scope of ['taskbar','minimize'])assert.equal(bridge.send(scope,'reset'),true);
  assert.ok(sent.includes('v1|taskbar|transparent|false'));
  assert.ok(sent.includes('v1|minimize|durationMs|2000'));
  assert.equal(sent.at(-1),'v1|minimize|reset');
  for(const args of [
    ['taskbar','enabled','true'],['taskbar','transparentTaskbar',true],['taskbar','reset',false],
    ['taskbar','hideBehavior',true],['taskbar','hideBehavior','hidden'],['taskbar','hideBehavior','fully-hidden','extra'],
    ['minimize','durationMs',49],['minimize','durationMs',2001],['minimize','durationMs',50.5],
    ['minimize','durationMs','450'],['minimize','durationMs',Infinity],['minimize','durationMs',NaN],
    ['minimize','unhideDurationMs',-1],['minimize','unhideDurationMs',5001],
    ['minimize','engine','modern|enabled|true'],['minimize','restore',1],['minimize','reset','extra'],
    ['minimize','enabled',true,'extra'],
  ])assert.equal(bridge.send(...args),false,JSON.stringify(args));
});

test('taskbar state validates the asynchronous schema without saved/result fields',()=>{
  assert.equal(validHostMessage(taskbarMessage()),true);
  assert.equal(validHostMessage({version:1,type:'navigate',page:'taskbar'}),true);
  for(const error of ['load','save','busy','unsupported','missing-payload','payload-integrity','conflict',
    'discover','read-appearance','apply-appearance','read-auto-hide','apply-auto-hide','core-start',
    'core-exited','core-stop','cancelled','recovery','watch','system-effects-disabled','runtime','read-visibility','apply-visibility'])
    assert.equal(validHostMessage(taskbarMessage({error})),true,error);
  for(const fields of [{enabled:1},{applying:'false'},{pendingRecovery:null},{taskbars:-1},{taskbars:1.5},{taskbars:33},
    {revision:-1},{revision:Number.MAX_SAFE_INTEGER+1},{mode:'unknown'},{loadStatus:'recovered'},
    {hideBehavior:'hidden'},{hideBehavior:true},{error:'<script>'},{saved:true},{result:'applied'},{extra:1}])
    assert.equal(validHostMessage(taskbarMessage(fields)),false,JSON.stringify(fields));
  for(const key of Object.keys(taskbarMessage())) {
    const message=taskbarMessage();delete message[key];assert.equal(validHostMessage(message),false,key);
  }
});

test('animation state validates runtime evidence and keeps legitimate failure snapshots',()=>{
  assert.equal(validHostMessage(minimizeMessage()),true);
  assert.equal(validHostMessage({version:1,type:'navigate',page:'minimize'}),true);
  assert.equal(validHostMessage(minimizeMessage({...enabledMinimize,saved:true,result:'applied',loadStatus:'loaded'})),true);
  for(const error of ['save','rollback','invalid-settings','missing-host','already-running','ipc',
    'host-start','hook-install','stop-timeout','host-exited','unsupported-architecture','runtime'])
    assert.equal(validHostMessage(minimizeMessage({error,systemError:2})),true,error);
  for(const fields of [{durationMs:49},{durationMs:2001},{durationMs:'450'},{unhideDurationMs:-1},
    {unhideDurationMs:5001},{engine:'future'},{restore:1},{hosts:3},{hooks:5},{workers:-1},
    {entries:-1},{entries:0.5},{entries:'0'},{entries:4294967296},
    {pendingCleanup:-1},{pendingCleanup:0.5},{pendingCleanup:false},{pendingCleanup:4294967296},
    {systemError:4294967296},{x64:'true'},{hosts:1},{x86:true},{running:true},{error:'<script>'},{saved:true},{extra:true},
    {saved:true,result:'applied',loadStatus:'loaded',error:'host-start'}])
    assert.equal(validHostMessage(minimizeMessage(fields)),false,JSON.stringify(fields));
  for(const key of Object.keys(minimizeMessage())) {
    const message=minimizeMessage();delete message[key];assert.equal(validHostMessage(message),false,key);
  }
});

test('native animation lifecycle snapshots reach the subscribed page state',()=>{
  let receive,state=initialState();
  const bridge=createBridge({addEventListener:(_,listener)=>{receive=listener;}});
  bridge.subscribe(message=>{state=reduce(state,{type:'host',message});});
  receive({data:minimizeMessage()});
  assert.equal(state.minimizeStatus,'idle','the native snapshot must release the loading controls');
  receive({data:minimizeMessage({...enabledMinimize,saved:true,result:'applied',loadStatus:'loaded'})});
  assert.equal(state.minimizeStatus,'saved');
  assert.equal(state.minimize.enabled,true);
  receive({data:minimizeMessage({enabled:true,workers:1,entries:1,pendingCleanup:1,
    error:'stop-timeout',result:'applied',loadStatus:'loaded'})});
  assert.equal(state.minimizeStatus,'error','retained cleanup ownership must remain visible as a failure');
  assert.equal(state.minimizeError,'stop-timeout');
});

test('pages preserve source controls and report recovery or incomplete runtime without success notices',()=>{
  const initial=initialState(),taskbarHtml=taskbarPage(initial),minimizeHtml=minimizePage(initial);
  assert.match(taskbarHtml,/data-settings-scope="taskbar"/);
  assert.match(taskbarHtml,/任务栏透明化/);assert.match(taskbarHtml,/自动隐藏任务栏/);
  assert.match(minimizeHtml,/data-settings-scope="minimize"/);
  assert.match(minimizeHtml,/id="minimize-durationMs"[^>]+value="450"/);
  assert.match(minimizeHtml,/id="minimize-unhideDurationMs"[^>]+value="450"/);
  assert.match(minimizeHtml,/data-select-value="modern"/);
  assert.match(minimizePage(initial,'details'),/默认关闭/);
  for(const fields of [{error:'core-start'},{pendingRecovery:true},{enabled:true}]) {
    const state=reduce(reduce(initial,{type:'request',scope:'taskbar'}),
      {type:'host',message:taskbarMessage({revision:1,loadStatus:'loaded',...fields})});
    assert.doesNotMatch(taskbarStatus(state),/已应用|已保存|保存成功/);
    assert.equal(taskbarNotice(state,{hasInlineStatus:true}),null);
  }
  const unavailable=reduce(initial,{type:'unavailable',scope:'minimize'});
  assert.match(minimizeNotice(unavailable),/宿主未连接/);
  const missing=reduce(initial,{type:'host',message:minimizeMessage({error:'missing-host',systemError:2})});
  assert.match(minimizeStatus(missing),/32 位.*64 位.*2/);
  assert.doesNotMatch(minimizeNotice(missing),/已应用|已保存|运行中/);
  assert.equal(minimizeNotice(missing,{hasInlineStatus:true}),null);
});

test('source defaults and direct settings routes are represented without optimistic enable',()=>{
  let state=initialState();
  assert.ok(state.taskbar);assert.ok(state.minimize);
  assert.deepEqual([state.taskbar.enabled,state.taskbar.transparentTaskbar,state.taskbar.autoHideTaskbar],[false,true,false]);
  const reset=reduce({...state,taskbar:{...state.taskbar,enabled:true,autoHideTaskbar:true}},
    {type:'request',scope:'taskbar',key:'reset'});
  assert.deepEqual(reset.taskbarPending,{transparentTaskbar:true,autoHideTaskbar:false,hideBehavior:'auto-hide'});
  assert.equal(reset.taskbar.enabled,true,'reset preserves the confirmed enable state');
  assert.deepEqual([state.minimize.enabled,state.minimize.durationMs,state.minimize.restore,state.minimize.launch,
    state.minimize.multiMonitor,state.minimize.unhideTaskbar,state.minimize.unhideDurationMs,state.minimize.engine],
    [false,450,true,true,false,true,450,'modern']);
  for(const scope of ['taskbar','minimize']) {
    assert.equal(reduce(state,{type:'navigate',view:scope}).view,scope);
    const pending=reduce(state,{type:'request',scope});
    assert.equal(pending[scope].enabled,false);
    assert.equal(pending[`${scope}Status`],'saving');
    assert.match(card({id:scope,title:scope,toggle:true},pending),new RegExp(`data-action="${scope}-toggle" disabled`));
    assert.equal(reduce(state,{type:'unavailable',scope:'all'})[`${scope}Status`],'unavailable');
  }
});

test('taskbar waits for a newer settled revision and holds confirmed preferences while applying',()=>{
  let state=reduce(initialState(),{type:'host',message:taskbarMessage({revision:3})});
  state=reduce(state,{type:'request',scope:'taskbar'});
  state=reduce(state,{type:'host',message:taskbarMessage({revision:3})});
  assert.equal(state.taskbarStatus,'saving','old snapshot cannot acknowledge a new request');
  state=reduce(state,{type:'host',message:taskbarMessage({revision:3,applying:true,enabled:true})});
  assert.equal(state.taskbarStatus,'saving');
  assert.equal(state.taskbar.enabled,false,'requested preferences are not evidence of application');
  state=reduce(state,{type:'host',message:taskbarMessage({revision:4,enabled:true,running:true,loadStatus:'loaded'})});
  assert.equal(state.taskbarStatus,'applied');
  assert.equal(state.taskbar.enabled,true);
  assert.equal(state.minimizeStatus,'loading');
  const stale=reduce(state,{type:'host',message:taskbarMessage({revision:2,error:'save'})});
  assert.equal(stale,state,'late older snapshots cannot overwrite the current state');
});

test('taskbar startup restore shows the host preferences while it is still applying',()=>{
  let state=reduce(initialState(),{type:'host',message:taskbarMessage({revision:1,applying:true,enabled:true,loadStatus:'loaded'})});
  assert.equal(state.taskbar.enabled,true,'an enabled feature being restored is not shown as off');
  assert.equal(state.taskbarStatus,'saving');
  assert.match(card({id:'taskbar',title:'任务栏透明化',toggle:true},state),/aria-checked="true"/);
  state=reduce(state,{type:'host',message:taskbarMessage({revision:2,enabled:true,running:true,loadStatus:'loaded'})});
  assert.equal(state.taskbarStatus,'idle');
  assert.equal(state.taskbar.enabled,true);
});

test('taskbar toggles respond immediately while confirmation and failures stay truthful',()=>{
  const settled=reduce(initialState(),{type:'host',message:taskbarMessage({revision:8,loadStatus:'loaded'})});
  let state=reduce(settled,{type:'request',scope:'taskbar',key:'enabled',value:true});
  const assertRequested=()=>{
    assert.equal(state.taskbar.enabled,false,'confirmed state must remain separate from requested state');
    assert.equal(state.taskbarStatus,'saving');
    assert.match(card({id:'taskbar',title:'任务栏透明化',toggle:true},state),/aria-checked="true"[^>]*data-action="taskbar-toggle" disabled/);
    assert.doesNotMatch(taskbarPage(state),/data-action="taskbar-toggle"/);
    assert.match(taskbarPage(state),/data-action="taskbar-transparent" disabled/);
    assert.match(taskbarPage(state),/data-action="taskbar-autoHide" disabled/);
    assert.match(taskbarStatus(state),/正在应用/);
    assert.doesNotMatch(taskbarStatus(state),/已应用|已保存/);
  };
  assertRequested();
  state=reduce(state,{type:'host',message:taskbarMessage({revision:8,loadStatus:'loaded'})});
  assertRequested();
  state=reduce(state,{type:'host',message:taskbarMessage({revision:8,applying:true,enabled:true})});
  assertRequested();
  state=reduce(state,{type:'host',message:taskbarMessage({revision:9,error:'core-start',loadStatus:'loaded'})});
  assert.equal(state.taskbarStatus,'error');
  assert.match(card({id:'taskbar',title:'任务栏透明化',toggle:true},state),/aria-checked="false"/);
  assert.match(taskbarStatus(state),/启动失败/);
  state=reduce(settled,{type:'request',scope:'taskbar',key:'enabled',value:true});
  state=reduce(state,{type:'unavailable',scope:'taskbar'});
  assert.match(card({id:'taskbar',title:'任务栏透明化',toggle:true},state),/aria-checked="false"/);
  state=reduce(settled,{type:'request',scope:'taskbar',key:'enabled',value:true});
  state=reduce(state,{type:'host',message:taskbarMessage({revision:9,enabled:true,running:true,loadStatus:'loaded'})});
  state=reduce(state,{type:'request',scope:'taskbar',key:'enabled',value:false});
  assert.match(card({id:'taskbar',title:'任务栏透明化',toggle:true},state),/aria-checked="false"/);
  assert.equal(state.taskbar.enabled,true,'shutdown is not confirmed before the host reply');
  state=reduce(state,{type:'host',message:taskbarMessage({revision:10,loadStatus:'loaded'})});
  assert.equal(state.taskbarStatus,'applied');
  assert.equal(state.taskbar.enabled,false);
});

test('taskbar failures, unfinished recovery and stopped runtime cannot produce success',()=>{
  for(const fields of [
    {error:'save'},{error:'cancelled'},{pendingRecovery:true},{loadStatus:'failed'},
    {enabled:true,running:false},{enabled:false,running:true},
  ]) {
    let state=reduce(initialState(),{type:'request',scope:'taskbar'});
    state=reduce(state,{type:'host',message:taskbarMessage({revision:1,loadStatus:'loaded',...fields})});
    assert.equal(state.taskbarStatus,'error',JSON.stringify(fields));
  }
  let state=reduce(initialState(),{type:'request',scope:'taskbar'});
  state=reduce(state,{type:'host',message:taskbarMessage({applying:true,error:'core-start'})});
  assert.equal(state.taskbarStatus,'saving','operation is still running');
  state=reduce(state,{type:'host',message:taskbarMessage({revision:1,error:'core-start'})});
  assert.equal(state.taskbarStatus,'error');
});

test('animation snapshots cannot acknowledge requests and failed hosts never report saved',()=>{
  let state=reduce(initialState(),{type:'request',scope:'minimize'});
  state=reduce(state,{type:'host',message:minimizeMessage()});
  assert.equal(state.minimizeStatus,'saving');
  state=reduce(state,{type:'host',message:minimizeMessage({error:'missing-host',result:'applied',systemError:2})});
  assert.equal(state.minimizeStatus,'error');
  assert.equal(state.minimize.enabled,false);
  assert.equal(state.taskbarStatus,'loading');
  state=reduce(state,{type:'request',scope:'minimize'});
  state=reduce(state,{type:'host',message:minimizeMessage({...enabledMinimize,result:'applied',saved:true,loadStatus:'loaded'})});
  assert.equal(state.minimizeStatus,'saved');
  state=reduce(state,{type:'host',message:minimizeMessage({enabled:true,error:'host-exited'})});
  assert.equal(state.minimizeStatus,'error');
  state=reduce(state,{type:'host',message:minimizeMessage({loadStatus:'recovered'})});
  assert.equal(state.minimizeStatus,'recovered');
});

test('taskbar status only mentions restoring when the feature is off',async()=>{
  const {taskbarStatus}=await import('../taskbar.js');
  const base={...initialState(),taskbarStatus:'idle',taskbarLoadStatus:'loaded'};
  const on={...base,taskbarError:'apply-auto-hide',taskbar:{...base.taskbar,enabled:true,running:false,pendingRecovery:true}};
  assert.doesNotMatch(taskbarStatus(on),/尚未完全恢复/);
  assert.match(taskbarStatus(on),/自动隐藏/);
  const off={...base,taskbarError:'',taskbar:{...base.taskbar,enabled:false,running:false,pendingRecovery:true}};
  assert.match(taskbarStatus(off),/尚未完全恢复/);
});
