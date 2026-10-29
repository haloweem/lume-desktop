import test from 'node:test';
import assert from 'node:assert/strict';
import {createBridge,validHostMessage} from '../bridge.js';
import {initialState,reduce} from '../state.js';
import {card} from '../pages.js';
import {performancePage,performanceNotice,performanceStatus} from '../performance.js';

const message=(fields={})=>({version:1,type:'performance-state',enabled:false,running:false,locked:false,
  material:'liquid-glass',materialStatus:'inactive',loadStatus:'fresh',error:'',width:340,height:216,
  result:'snapshot',saved:false,...fields});
const resourceGroup=(fields={})=>({
  workingSetBytes:1024*1024,privateBytes:512*1024,gpuDedicatedBytes:null,gpuSharedBytes:null,
  processCount:1,coverage:'complete',processes:[],
  ...fields,
});
const resourceMessage=(fields={})=>({
  version:1,type:'resource-snapshot',schemaVersion:1,
  sampledAt:{low:1,high:2},
  shell:resourceGroup(),webview:resourceGroup({processCount:3}),
  music:resourceGroup({processCount:1}),gpu:resourceGroup({processCount:1,gpuDedicatedBytes:2*1024*1024}),
  wallpaper:resourceGroup({processCount:1,workingSetBytes:64*1024*1024}),
  nonWallpaper:resourceGroup({workingSetBytes:128*1024*1024,privateBytes:96*1024*1024,processCount:5}),
  processes:[],
  ...fields,
});

test('a widget the desktop layer refused reports its own error',()=>{
  assert.equal(validHostMessage(message({error:'layer'})),true);
  const state=reduce(initialState(),{type:'host',message:message({error:'layer'})});
  assert.equal(state.performanceStatus,'error');
  assert.match(performanceStatus(state),/桌面层/);
  assert.equal(validHostMessage(message({error:'layer-extra'})),false);
});
test('performance bridge accepts only complete defined commands and states',()=>{
  const sent=[];const bridge=createBridge({postMessage:v=>sent.push(v)});
  const commands=[['enabled',true],['enabled',false],['material','liquid-glass'],['material','frosted-glass'],['reset']];
  for(const c of commands)assert.equal(bridge.send('performance',...c),true);
  assert.deepEqual(sent,commands.map(c=>['v1','performance',...c].join('|')));
  for(const c of [['enabled',1],['enabled','true'],['reset',true],['material','solid'],['reset\0']])
    assert.equal(bridge.send('performance',...c),false);
  assert.equal(validHostMessage(message()),true);
  assert.equal(validHostMessage({version:1,type:'navigate',page:'performance'}),true);
  for(const fields of [{width:319},{height:203},{width:721},{height:721},{enabled:1},{extra:true},{saved:true},
    {result:'other'},{error:'arbitrary'},{materialStatus:'glass'},{material:'solid'},{loadStatus:'backup'}])
    assert.equal(validHostMessage(message(fields)),false,JSON.stringify(fields));
  assert.equal(validHostMessage(message({result:'applied',saved:true,loadStatus:'loaded'})),true);
  assert.equal(validHostMessage(message({result:'applied',saved:true,error:'save'})),false);
});
test('asynchronous material snapshots cannot complete a pending performance save',()=>{
  let state=reduce(initialState(),{type:'request',scope:'performance'});
  assert.equal(state.performanceStatus,'saving');assert.equal(state.status,'loading');assert.equal(state.calendarStatus,'loading');
  state=reduce(state,{type:'host',message:message({materialStatus:'native-ready'})});
  assert.equal(state.performanceStatus,'saving');
  state=reduce(state,{type:'host',message:message({enabled:true,result:'applied',saved:true,loadStatus:'loaded'})});
  assert.equal(state.performanceStatus,'saved');assert.equal(state.performance.enabled,true);
  state=reduce(state,{type:'request',scope:'performance'});
  state=reduce(state,{type:'host',message:message({error:'save',result:'applied'})});
  assert.equal(state.performanceStatus,'error');assert.equal(state.performanceError,'save');
  assert.equal(reduce(initialState(),{type:'unavailable',scope:'all'}).performanceStatus,'unavailable');
  assert.equal(reduce(initialState(),{type:'host',message:message({loadStatus:'failed'})}).performanceStatus,'error');
  assert.equal(reduce(initialState(),{type:'navigate',view:'performance'}).view,'performance');
});
test('performance catalog toggle follows confirmed native state with an independent pending flag',()=>{
  const state={...initialState(),performance:{enabled:true},performanceStatus:'saving'};
  const html=card({id:'performance',title:'性能',description:'实时状态'},state,true);
  assert.match(html,/data-action="performance-toggle" disabled/);assert.match(html,/aria-checked="true"/);
});
test('性能设置保留材质和错误恢复，主开关仅在卡片显示确认状态',()=>{
  let state=reduce(initialState(),{type:'host',message:message({enabled:true,running:true})});
  let html=performancePage(state);
  assert.match(html,/data-settings-scope="performance"/);
  assert.match(html,/aria-label="性能设置"/);
  assert.doesNotMatch(html,/data-action="performance-toggle"/);
  assert.match(card({id:'performance',title:'性能'},state,true),/aria-checked="true"[^>]*data-action="performance-toggle"/);
  assert.match(html,/data-select-value="liquid-glass"/);
  assert.match(html,/data-select-value="frosted-glass"/);
  assert.match(html,/data-action="performance-reset"/);
  assert.doesNotMatch(html,/未接入/);
  state=reduce(state,{type:'request',scope:'performance'});
  assert.match(card({id:'performance',title:'性能'},state,true),/data-action="performance-toggle" disabled/);
  assert.match(performancePage(state),/data-select="performance-material" disabled/);
  assert.match(performancePage(state),/data-action="performance-reset" disabled/);
  state=reduce(state,{type:'host',message:message({result:'applied',error:'save'})});
  assert.match(performancePage(state),/保存失败/);
  assert.match(performanceNotice(state),/保存失败/);
  assert.equal(performanceNotice(state,{hasInlineStatus:true}),null);
  assert.doesNotMatch(performancePage(state,'details'),/data-action="performance-toggle"/);
});
test('resource snapshots are accepted and rendered as one grouped overview',()=>{
  const snapshot=resourceMessage({
    processes:[
      {pid:10,parentPid:1,name:'LumeShell.exe',role:'Shell',workingSetBytes:1024,privateBytes:512,
        gpuDedicatedBytes:null,gpuSharedBytes:null},
      {pid:11,parentPid:10,name:'msedgewebview2.exe',role:'WebViewBrowser',workingSetBytes:2048,privateBytes:1024,
        gpuDedicatedBytes:null,gpuSharedBytes:null},
    ],
  });
  assert.equal(validHostMessage(snapshot),true);
  const state=reduce(initialState(),{type:'host',message:snapshot});
  assert.equal(state.resourceSnapshot.nonWallpaper.workingSetBytes,128*1024*1024);
  const html=performancePage(state);
  for(const text of ['Lume','Lume 组件','Lume 主程序','音乐','GPU','壁纸']) assert.match(html,new RegExp(text));
  assert.doesNotMatch(html,/webview2/i);
  assert.match(html,/resource-lume-group/);
  assert.match(html,/resource-lume-summary/);
  assert.doesNotMatch(html,/<details class="resource-lume-group"[^>]*open/);
  assert.match(html,/resource-processes/);
});
test('unavailable resource counters render as dashes instead of zero',()=>{
  const snapshot=resourceMessage({
    nonWallpaper:resourceGroup({workingSetBytes:null,privateBytes:null,coverage:'unavailable',processCount:0}),
    wallpaper:resourceGroup({workingSetBytes:null,coverage:'unavailable',processCount:0}),
  });
  const state=reduce(initialState(),{type:'host',message:snapshot});
  const html=performancePage(state);
  assert.match(html,/data-resource-value="nonWallpaper-workingSet">--/);
  assert.match(html,/data-resource-value="wallpaper-workingSet">--/);
  assert.match(html,/不可用|Unavailable/);
});
