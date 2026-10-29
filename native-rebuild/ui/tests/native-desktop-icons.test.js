import test from 'node:test';
import assert from 'node:assert/strict';
import {createBridge,validHostMessage} from '../bridge.js';
import {initialState,reduce} from '../state.js';
import {card} from '../pages.js';
import {desktopIconsPage,desktopIconsStatus,desktopIconsNotice} from '../desktop-icons.js';
export const iconsMessage=(fields={})=>({
  version:1,type:'icons-state',enabled:false,running:false,enableAutoHide:true,
  autoHideDelay:5,showIconsOnAnyInput:true,manuallyHidden:false,
  desktops:0,autoHidden:0,pendingRestores:0,loadStatus:'fresh',error:'',result:'snapshot',saved:false,...fields
});
test('desktop icon bridge accepts source settings and rejects malformed protocol',()=>{
  const sent=[];const bridge=createBridge({postMessage:v=>sent.push(v)});
  for(const key of ['enabled','autoHide','restoreOnInput'])
    for(const value of [true,false])assert.equal(bridge.send('icons',key,value),true);
  for(let delay=3;delay<=60;delay++)assert.equal(bridge.send('icons','delay',delay),true);
  assert.equal(bridge.send('icons','reset'),true);
  assert.equal(sent[6],'v1|icons|delay|3');assert.equal(sent.at(-2),'v1|icons|delay|60');
  for(const value of [2,61,4.5,'5',null,NaN,Infinity])
    assert.equal(bridge.send('icons','delay',value),false);
  assert.equal(bridge.send('icons','enabled','true'),false);
  assert.equal(bridge.send('icons','reset','extra'),false);
  assert.equal(validHostMessage({version:1,type:'navigate',page:'icons'}),true);
  assert.equal(validHostMessage(iconsMessage()),true);
  for(const fields of [{autoHideDelay:2},{autoHideDelay:5.5},{enabled:1},{running:true},{desktops:9},
    {autoHidden:1},{pendingRestores:-1},{loadStatus:'unknown'},{error:'untrusted'},{saved:true},{extra:true},
    {saved:true,result:'applied',loadStatus:'loaded',error:'save'}])
    assert.equal(validHostMessage(iconsMessage(fields)),false,JSON.stringify(fields));
  for(const error of ['save','rollback','runtime','visibility','input-busy'])
    assert.equal(validHostMessage(iconsMessage({error})),true);
});
test('native snapshots do not complete pending saves and errors remain independent',()=>{
  let state=reduce(initialState(),{type:'request',scope:'icons'});
  state=reduce(state,{type:'host',message:iconsMessage({enabled:true,running:true,desktops:1})});
  assert.equal(state.iconsStatus,'saving');assert.equal(state.weatherStatus,'loading');
  state=reduce(state,{type:'host',message:iconsMessage({enabled:true,running:true,desktops:1,
    saved:true,result:'applied',loadStatus:'loaded'})});
  assert.equal(state.iconsStatus,'saved');assert.equal(state.icons.enabled,true);
  assert.match(desktopIconsNotice(state,{previousStatus:'saving'}),/保存/);
  state=reduce(state,{type:'host',message:iconsMessage({enabled:true,error:'rollback',pendingRestores:1,result:'applied'})});
  assert.equal(state.iconsStatus,'error');assert.match(desktopIconsStatus(state),/恢复/);
  assert.equal(reduce(initialState(),{type:'unavailable',scope:'all'}).iconsStatus,'unavailable');
});
test('a persisted setting with a visibility failure is accepted without claiming application success',()=>{
  const message=iconsMessage({enabled:true,running:true,desktops:1,enableAutoHide:true,autoHideDelay:3,
    result:'applied',saved:true,loadStatus:'loaded',error:'visibility'});
  assert.equal(validHostMessage(message),true,'persistence succeeds before the desktop visibility acknowledgement');
  let state=reduce(initialState(),{type:'request',scope:'icons'});
  state=reduce(state,{type:'host',message});
  assert.equal(state.iconsStatus,'error');
  assert.equal(state.icons.autoHideDelay,3,'the interface keeps the actual saved preference');
  assert.equal(state.icons.enabled,true);
  assert.equal(state.iconsError,'visibility');
  assert.match(desktopIconsNotice(state,{previousStatus:'saving'}),/桌面.*响应/);
  assert.doesNotMatch(desktopIconsNotice(state,{previousStatus:'saving'}),/已应用|已保存/);
  assert.doesNotMatch(desktopIconsPage(state),/\bdisabled\b/,'close and retry controls are available');
  for(const fields of [{error:'save'},{error:'input-busy'},{error:'runtime'},{error:'rollback'},{loadStatus:'recovered'},
    {loadStatus:'failed'},{result:'snapshot'},{enabled:false},{running:false}])
    assert.equal(validHostMessage({...message,...fields}),false,JSON.stringify(fields));
});
test('error snapshots complete pending requests and preserve the reported preferences',()=>{
  const pending=reduce(initialState(),{type:'request',scope:'icons'});
  for(const fields of [
    {enabled:true,running:true,desktops:1,error:'visibility',autoHideDelay:9},
    {enabled:true,error:'rollback',pendingRestores:1},
    {error:'save'},
    {loadStatus:'failed'},
    {enabled:true},
  ]) {
    const message=iconsMessage(fields);
    assert.equal(validHostMessage(message),true);
    const state=reduce(pending,{type:'host',message});
    assert.equal(state.iconsStatus,'error',JSON.stringify(fields));
    assert.equal(state.iconsError,message.error);
    assert.equal(state.icons.autoHideDelay,message.autoHideDelay);
    assert.doesNotMatch(desktopIconsPage(state),/\bdisabled\b/);
  }
  const recovered=reduce(reduce(pending,{type:'host',message:iconsMessage({error:'visibility'})}),
    {type:'host',message:iconsMessage()});
  assert.equal(recovered.iconsStatus,'idle');
  assert.equal(recovered.iconsError,'');
});
test('settings and catalog show the source controls with truthful runtime feedback',()=>{
  let state=reduce(initialState(),{type:'host',message:iconsMessage({enabled:true,running:true,desktops:1})});
  const html=desktopIconsPage(state);
  assert.doesNotMatch(html,/data-action="icons-toggle"/);
  assert.match(card({id:'icons',title:'桌面图标',toggle:true},state),/aria-checked="true"[^>]*data-action="icons-toggle"/);
  for(const key of ['icons-autoHide','icons-restoreOnInput','icons-reset'])
    assert.match(html,new RegExp(`data-action="${key}"`));
  assert.match(html,/id="icons-delay"/);assert.match(html,/min="3"/);assert.match(html,/max="60"/);
  assert.match(html,/任意输入时恢复图标/);assert.match(html,/自动隐藏空闲桌面图标/);
  assert.match(html,/返回桌面美化/);
  assert.match(desktopIconsPage(state,'details'),/手动隐藏/);
  const pending=reduce(state,{type:'request',scope:'icons'});
  assert.match(card({id:'icons',title:'桌面图标',toggle:true},pending),/data-action="icons-toggle" disabled/);
  assert.equal(desktopIconsNotice(pending),null);
  state=reduce(state,{type:'host',message:iconsMessage({enabled:true,running:true,desktops:0})});
  assert.match(desktopIconsStatus(state),/等待桌面/);
});
