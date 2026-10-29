import test from 'node:test';
import assert from 'node:assert/strict';
import {createBridge,validHostMessage} from '../bridge.js';
import {initialState,reduce} from '../state.js';
import {card} from '../pages.js';

export const calendar={enabled:false,theme:'spacious',material:'liquid-glass',textColor:'#ffffff',highlightColor:'#0a84ff',locked:false,running:false};
const message=(fields={})=>({version:1,type:'calendar-settings',calendar:{...calendar},saved:false,error:'',materialStatus:'transparent-fallback',loadStatus:'loaded',result:'snapshot',...fields});

test('calendar commands are an exact narrow allowlist including native color editor commands',()=>{
  const sent=[];const bridge=createBridge({postMessage:value=>sent.push(value)});
  const commands=[['enabled',true],['enabled',false],['theme','spacious'],['theme','split'],['theme','todo'],['material','liquid-glass'],['material','frosted-glass'],['textColor'],['highlightColor'],['reset']];
  for(const command of commands) assert.equal(bridge.send('calendar',...command),true,command.join('|'));
  assert.deepEqual(sent,commands.map(command=>['v1','calendar',...command].join('|')));
  for(const command of [['enabled','true'],['theme','Split'],['material','glass'],['reset','extra'],['textColor','#ffffff'],['highlightcolor'],['theme','split\0']]) assert.equal(bridge.send('calendar',...command),false);
  assert.equal(validHostMessage({version:1,type:'navigate',page:'calendar'}),true);
});

test('calendar host state validates fixed enums, strict RGBA colors and exact fields',()=>{
  assert.equal(validHostMessage(message()),true);
  for(const materialStatus of ['native-ready','native-starting','inactive','transparent-fallback']) {
    assert.equal(validHostMessage(message({materialStatus})),true);
    assert.equal(reduce(initialState(),{type:'host',message:message({materialStatus})}).calendarMaterialStatus,materialStatus);
  }
  assert.equal(validHostMessage({version:1,type:'reminder-status',available:false}),true);
  assert.equal(validHostMessage({version:1,type:'reminder-status',available:'false'}),false);
  assert.equal(reduce(initialState(),{type:'host',message:{version:1,type:'reminder-status',available:false}}).reminderAvailable,false);
  for(const color of ['#0102af','#0102af80','#FFFFFF','#FFFFFF00']) assert.equal(validHostMessage(message({calendar:{...calendar,textColor:color}})),true);
  for(const color of ['red','#fff','#ffffff00 ','#fffffffff','#12345g','"/><script>','#ffffff\0','#ffffff\n','#ffffff00\n']) assert.equal(validHostMessage(message({calendar:{...calendar,textColor:color}})),false,color);
  for(const fields of [{extra:true},{error:'arbitrary'},{loadStatus:'backup'},{result:'ok'},{saved:true},{materialStatus:'glass'},{calendar:{...calendar,theme:'Split'}},{calendar:{...calendar,locked:0}},{calendar:{...calendar,extra:1}}]) assert.equal(validHostMessage(message(fields)),false,JSON.stringify(fields));
  for(const error of ['invalid','runtime','save','rollback','load']) assert.equal(validHostMessage(message({error,loadStatus:error==='load'?'failed':'loaded'})),true);
  assert.equal(validHostMessage(message({saved:true,result:'applied'})),true);
  assert.equal(validHostMessage(message({saved:true,result:'cancelled'})),false);
  assert.equal(validHostMessage(message({saved:true,result:'applied',error:'save'})),false);
  assert.equal(validHostMessage(message({loadStatus:'failed',error:'runtime'})),true,'runtime failure can coexist with load failure');
  const legacy=message();delete legacy.result;delete legacy.loadStatus;
  assert.equal(validHostMessage(legacy),true,'predefined protocol without optional fields stays supported');
});

test('calendar and clock pending errors remain independent and cancellation is never saved',()=>{
  let state=reduce(initialState(),{type:'request',scope:'calendar'});
  assert.equal(state.calendarStatus,'saving');assert.equal(state.status,'loading');
  state=reduce(state,{type:'host',message:{version:1,type:'settings',clock:{enabled:true,style:'mond'},saved:false,error:'runtime'}});
  assert.equal(state.calendarStatus,'saving');assert.equal(state.error,'runtime');
  state=reduce(state,{type:'host',message:message({result:'cancelled'})});
  assert.equal(state.calendarStatus,'idle');assert.equal(state.status,'error');assert.equal(state.error,'runtime');
  state=reduce(state,{type:'host',message:message({error:'load',loadStatus:'failed'})});
  assert.equal(state.calendarStatus,'error');assert.equal(state.calendarError,'load');
  state=reduce(state,{type:'request',scope:'clock'});
  assert.equal(state.calendarError,'load');assert.equal(state.calendarStatus,'error');assert.equal(state.status,'saving');
  state=reduce(state,{type:'host',message:message({loadStatus:'recovered-backup'})});
  assert.equal(state.calendarStatus,'recovered');assert.equal(state.status,'saving');
  assert.equal(reduce(initialState(),{type:'unavailable',scope:'all'}).calendarStatus,'unavailable');
  state=reduce(state,{type:'host',message:message({error:'save',result:'applied'})});
  state=reduce(state,{type:'request',scope:'calendar'});
  state=reduce(state,{type:'host',message:message({result:'cancelled'})});
  assert.equal(state.calendarError,'save','cancel preserves an earlier error');
  assert.equal(state.calendarStatus,'error');
});

test('calendar catalog and search cards expose an independently confirmed switch',()=>{
  const state={...initialState(),calendar:{...calendar,enabled:true},calendarStatus:'saving'};
  const html=card({id:'calendar',title:'日历',description:'月视图'},state,true);
  assert.match(html,/data-action="calendar-toggle" disabled/);assert.match(html,/aria-checked="true"/);
});
