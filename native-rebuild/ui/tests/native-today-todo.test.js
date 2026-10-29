import test from 'node:test';
import {widgets} from '../catalog.js';
import assert from 'node:assert/strict';
import {createBridge,validHostMessage} from '../bridge.js';
import {initialState,reduce} from '../state.js';
import {card} from '../pages.js';
import {todayTodoPage,todayTodoNotice,todayTodoStatus} from '../today-todo.js';

const message=(fields={})=>({version:1,type:'todayTodo-state',enabled:false,running:false,locked:false,
  material:'liquid-glass',materialStatus:'inactive',loadStatus:'fresh',error:'',width:320,height:152,
  result:'snapshot',saved:false,...fields});

test('a widget the desktop layer refused reports its own error',()=>{
  assert.equal(validHostMessage(message({error:'layer'})),true);
  const state=reduce(initialState(),{type:'host',message:message({error:'layer'})});
  assert.equal(state.todayTodoStatus,'error');
  assert.match(todayTodoStatus(state),/桌面层/);
  assert.equal(validHostMessage(message({error:'layer-extra'})),false);
});
test('todayTodo bridge accepts only complete defined commands and states',()=>{
  const sent=[];const bridge=createBridge({postMessage:v=>sent.push(v)});
  const commands=[['enabled',true],['enabled',false],['material','liquid-glass'],['material','frosted-glass'],['reset']];
  for(const c of commands)assert.equal(bridge.send('todayTodo',...c),true);
  assert.deepEqual(sent,commands.map(c=>['v1','todayTodo',...c].join('|')));
  for(const c of [['enabled',1],['enabled','true'],['reset',true],['material','solid'],['reset\0']])
    assert.equal(bridge.send('todayTodo',...c),false);
  assert.equal(validHostMessage(message()),true);
  assert.equal(validHostMessage({version:1,type:'navigate',page:'todayTodo'}),true);
  for(const fields of [{width:319},{height:151},{width:721},{height:721},{enabled:1},{extra:true},{saved:true},
    {result:'other'},{error:'arbitrary'},{materialStatus:'glass'},{material:'solid'},{loadStatus:'backup'}])
    assert.equal(validHostMessage(message(fields)),false,JSON.stringify(fields));
  assert.equal(validHostMessage(message({result:'applied',saved:true,loadStatus:'loaded'})),true);
  assert.equal(validHostMessage(message({result:'applied',saved:true,error:'save'})),false);
});
test('asynchronous material snapshots cannot complete a pending todayTodo save',()=>{
  let state=reduce(initialState(),{type:'request',scope:'todayTodo'});
  assert.equal(state.todayTodoStatus,'saving');assert.equal(state.status,'loading');assert.equal(state.calendarStatus,'loading');
  state=reduce(state,{type:'host',message:message({materialStatus:'native-ready'})});
  assert.equal(state.todayTodoStatus,'saving');
  state=reduce(state,{type:'host',message:message({enabled:true,result:'applied',saved:true,loadStatus:'loaded'})});
  assert.equal(state.todayTodoStatus,'saved');assert.equal(state.todayTodo.enabled,true);
  state=reduce(state,{type:'request',scope:'todayTodo'});
  state=reduce(state,{type:'host',message:message({error:'save',result:'applied'})});
  assert.equal(state.todayTodoStatus,'error');assert.equal(state.todayTodoError,'save');
  assert.equal(reduce(initialState(),{type:'unavailable',scope:'all'}).todayTodoStatus,'unavailable');
  assert.equal(reduce(initialState(),{type:'host',message:message({loadStatus:'failed'})}).todayTodoStatus,'error');
  assert.equal(reduce(initialState(),{type:'navigate',view:'todayTodo'}).view,'todayTodo');
});
test('todayTodo catalog toggle follows confirmed native state with an independent pending flag',()=>{
  const state={...initialState(),todayTodo:{enabled:true},todayTodoStatus:'saving'};
  const html=card({id:'todayTodo',title:'待办清单',description:'实时状态'},state,true);
  assert.match(html,/data-action="todayTodo-toggle" disabled/);assert.match(html,/aria-checked="true"/);
});
test('待办清单设置保留材质和错误恢复，主开关仅在卡片显示确认状态',()=>{
  let state=reduce(initialState(),{type:'host',message:message({enabled:true,running:true})});
  let html=todayTodoPage(state);
  assert.match(html,/data-settings-scope="todayTodo"/);
  assert.match(html,/aria-label="待办清单设置"/);
  assert.doesNotMatch(html,/data-action="todayTodo-toggle"/);
  assert.match(card({id:'todayTodo',title:'待办清单'},state,true),/aria-checked="true"[^>]*data-action="todayTodo-toggle"/);
  assert.match(html,/data-select-value="liquid-glass"/);
  assert.match(html,/data-select-value="frosted-glass"/);
  assert.match(html,/data-action="todayTodo-reset"/);
  assert.doesNotMatch(html,/未接入/);
  state=reduce(state,{type:'request',scope:'todayTodo'});
  assert.match(card({id:'todayTodo',title:'待办清单'},state,true),/data-action="todayTodo-toggle" disabled/);
  assert.match(todayTodoPage(state),/data-select="todayTodo-material" disabled/);
  assert.match(todayTodoPage(state),/data-action="todayTodo-reset" disabled/);
  state=reduce(state,{type:'host',message:message({result:'applied',error:'save'})});
  assert.match(todayTodoPage(state),/保存失败/);
  assert.match(todayTodoNotice(state),/保存失败/);
  assert.equal(todayTodoNotice(state,{hasInlineStatus:true}),null);
  assert.doesNotMatch(todayTodoPage(state,'details'),/data-action="todayTodo-toggle"/);
});

test('待办清单紧跟天气，简介说明桌面上的管理操作',()=>{
  const index=widgets.findIndex(item=>item.id==='weather');
  assert.equal(widgets[index+1].id,'todayTodo');
  assert.equal(widgets[index+1].description,'在桌面管理今日待办，支持勾选、编辑和删除。');
});
