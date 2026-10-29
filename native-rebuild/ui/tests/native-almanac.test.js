import test from 'node:test';
import assert from 'node:assert/strict';
import {createBridge,validHostMessage} from '../bridge.js';
import {initialState,reduce} from '../state.js';
import {card} from '../pages.js';
import {almanacPage,almanacNotice,almanacStatus} from '../almanac.js';

const message=(fields={})=>({version:1,type:'almanac-state',enabled:false,running:false,locked:false,
  material:'liquid-glass',materialStatus:'inactive',loadStatus:'fresh',error:'',width:320,height:152,
  result:'snapshot',saved:false,...fields});

test('a widget the desktop layer refused reports its own error',()=>{
  assert.equal(validHostMessage(message({error:'layer'})),true);
  const state=reduce(initialState(),{type:'host',message:message({error:'layer'})});
  assert.equal(state.almanacStatus,'error');
  assert.match(almanacStatus(state),/桌面层/);
  assert.equal(validHostMessage(message({error:'layer-extra'})),false);
});
test('almanac bridge accepts only complete defined commands and states',()=>{
  const sent=[];const bridge=createBridge({postMessage:v=>sent.push(v)});
  const commands=[['enabled',true],['enabled',false],['material','liquid-glass'],['material','frosted-glass'],['reset']];
  for(const c of commands)assert.equal(bridge.send('almanac',...c),true);
  assert.deepEqual(sent,commands.map(c=>['v1','almanac',...c].join('|')));
  for(const c of [['enabled',1],['enabled','true'],['reset',true],['material','solid'],['reset\0']])
    assert.equal(bridge.send('almanac',...c),false);
  assert.equal(validHostMessage(message()),true);
  assert.equal(validHostMessage({version:1,type:'navigate',page:'almanac'}),true);
  for(const fields of [{width:319},{height:151},{width:721},{height:721},{enabled:1},{extra:true},{saved:true},
    {result:'other'},{error:'arbitrary'},{materialStatus:'glass'},{material:'solid'},{loadStatus:'backup'}])
    assert.equal(validHostMessage(message(fields)),false,JSON.stringify(fields));
  assert.equal(validHostMessage(message({result:'applied',saved:true,loadStatus:'loaded'})),true);
  assert.equal(validHostMessage(message({result:'applied',saved:true,error:'save'})),false);
});
test('asynchronous material snapshots cannot complete a pending almanac save',()=>{
  let state=reduce(initialState(),{type:'request',scope:'almanac'});
  assert.equal(state.almanacStatus,'saving');assert.equal(state.status,'loading');assert.equal(state.calendarStatus,'loading');
  state=reduce(state,{type:'host',message:message({materialStatus:'native-ready'})});
  assert.equal(state.almanacStatus,'saving');
  state=reduce(state,{type:'host',message:message({enabled:true,result:'applied',saved:true,loadStatus:'loaded'})});
  assert.equal(state.almanacStatus,'saved');assert.equal(state.almanac.enabled,true);
  state=reduce(state,{type:'request',scope:'almanac'});
  state=reduce(state,{type:'host',message:message({error:'save',result:'applied'})});
  assert.equal(state.almanacStatus,'error');assert.equal(state.almanacError,'save');
  assert.equal(reduce(initialState(),{type:'unavailable',scope:'all'}).almanacStatus,'unavailable');
  assert.equal(reduce(initialState(),{type:'host',message:message({loadStatus:'failed'})}).almanacStatus,'error');
  assert.equal(reduce(initialState(),{type:'navigate',view:'almanac'}).view,'almanac');
});
test('almanac catalog toggle follows confirmed native state with an independent pending flag',()=>{
  const state={...initialState(),almanac:{enabled:true},almanacStatus:'saving'};
  const html=card({id:'almanac',title:'黄历',description:'实时状态'},state,true);
  assert.match(html,/data-action="almanac-toggle" disabled/);assert.match(html,/aria-checked="true"/);
});
test('黄历设置保留材质和错误恢复，主开关仅在卡片显示确认状态',()=>{
  let state=reduce(initialState(),{type:'host',message:message({enabled:true,running:true})});
  let html=almanacPage(state);
  assert.match(html,/data-settings-scope="almanac"/);
  assert.match(html,/aria-label="黄历设置"/);
  assert.doesNotMatch(html,/data-action="almanac-toggle"/);
  assert.match(card({id:'almanac',title:'黄历'},state,true),/aria-checked="true"[^>]*data-action="almanac-toggle"/);
  assert.match(html,/data-select-value="liquid-glass"/);
  assert.match(html,/data-select-value="frosted-glass"/);
  assert.match(html,/data-action="almanac-reset"/);
  assert.doesNotMatch(html,/未接入/);
  state=reduce(state,{type:'request',scope:'almanac'});
  assert.match(card({id:'almanac',title:'黄历'},state,true),/data-action="almanac-toggle" disabled/);
  assert.match(almanacPage(state),/data-select="almanac-material" disabled/);
  assert.match(almanacPage(state),/data-action="almanac-reset" disabled/);
  state=reduce(state,{type:'host',message:message({result:'applied',error:'save'})});
  assert.match(almanacPage(state),/保存失败/);
  assert.match(almanacNotice(state),/保存失败/);
  assert.equal(almanacNotice(state,{hasInlineStatus:true}),null);
  assert.doesNotMatch(almanacPage(state,'details'),/data-action="almanac-toggle"/);
});
