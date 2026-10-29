import test from 'node:test';
import assert from 'node:assert/strict';
import {createBridge,validHostMessage} from '../bridge.js';
import {initialState,reduce} from '../state.js';
import {card,catalog,search} from '../pages.js';
import {modules,widgets} from '../catalog.js';
import {organizerPage,organizerNotice,organizerStatus} from '../organizer.js';
const message=(fields={})=>{
  const value={version:1,type:'organizer-state',enabled:false,running:false,locked:false,
  material:'liquid-glass',materialStatus:'inactive',loadStatus:'fresh',error:'',width:420,height:392,
  result:'snapshot',saved:false,title:'桌面收纳',count:0,mode:'expanded',layout:'icons',...fields};
  value.collections=fields.collections??[{id:'default',title:value.title,enabled:value.enabled,running:value.running,count:value.count}];
  return value;
};
test('temporarily hidden radial menu and notes stay out of catalogs and search',()=>{
  assert.equal(modules.find(item=>item.id==='radial').hidden,true);
  assert.equal(widgets.find(item=>item.id==='notes').hidden,true);
  const state=initialState();
  const desktop=catalog(state,false),widgetCatalog=catalog(state,true);
  assert.doesNotMatch(desktop,/轮盘菜单|Radial menu/);
  assert.doesNotMatch(widgetCatalog,/便签|Notes/);
  assert.doesNotMatch(search({...state,query:'轮盘菜单'}),/data-open="radial"/);
  assert.doesNotMatch(search({...state,query:'便签'}),/data-open="notes"/);
});
test('organizer bridge and state use a strict native contract',()=>{
  const sent=[];const bridge=createBridge({postMessage:value=>sent.push(value)});
  for(const c of [['enabled',true],['enabled',false],['material','frosted-glass'],['material','liquid-glass'],['reset']])
    assert.equal(bridge.send('organizer',...c),true);
  assert.equal(validHostMessage(message()),true);assert.equal(validHostMessage({version:1,type:'navigate',page:'organizer'}),true);
  for(const fields of [{width:119},{width:1201},{height:215},{count:-1},{count:121},{mode:'other'},{layout:'grid'},{title:''},{extra:true},{saved:true},
    {material:'solid'},{error:'arbitrary'}])assert.equal(validHostMessage(message(fields)),false,JSON.stringify(fields));
  assert.deepEqual(sent,['v1|organizer|enabled|true','v1|organizer|enabled|false','v1|organizer|material|frosted-glass','v1|organizer|material|liquid-glass','v1|organizer|reset']);
  let state=reduce(initialState(),{type:'request',scope:'organizer'});assert.equal(state.organizerStatus,'saving');
  state=reduce(state,{type:'host',message:message({enabled:true,running:true,count:3,title:'资料'})});
  assert.equal(state.organizerStatus,'saving');
  state=reduce(state,{type:'host',message:message({enabled:true,running:true,count:3,title:'资料',saved:true,result:'applied',loadStatus:'loaded'})});
  assert.equal(state.organizerStatus,'saved');assert.equal(state.organizer.count,3);
  state=reduce(state,{type:'host',message:message({saved:false,error:'save',result:'applied'})});
  assert.equal(state.organizerStatus,'error');assert.match(organizerStatus(state),/保存失败/);
  assert.equal(reduce(initialState(),{type:'unavailable',scope:'all'}).organizerStatus,'unavailable');
});
test('narrow native organizer snapshots and applied replies reach the real bridge subscriber',()=>{
  let receive;const sent=[];
  const bridge=createBridge({postMessage:value=>sent.push(value),addEventListener:(_,listener)=>{receive=listener;},removeEventListener:()=>{}});
  let state=initialState(),delivered=0;
  const unsubscribe=bridge.subscribe(host=>{++delivered;state=reduce(state,{type:'host',message:host});});
  for(const width of [120,121,160,180,181,319,359,360,420,720,1200]) {
    for(const layout of ['icons','list']) {
      const snapshot=message({width,height:569,layout,enabled:true,running:true,loadStatus:'loaded'});
      receive({data:snapshot});
      assert.equal(state.organizer.width,width,'host geometry must not be silently rejected');
      assert.equal(state.organizerStatus,'idle');
      assert.equal(state.organizer.enabled,true,'running widget must show enabled before any click');
      for(const enabled of [false,true]) {
        assert.equal(bridge.send('organizer','enabled',enabled),true);
        state=reduce(state,{type:'request',scope:'organizer',key:'enabled',value:enabled});
        const before=delivered;
        receive({data:message({width,height:569,layout,enabled,running:enabled,
          loadStatus:'loaded',result:'applied',saved:true})});
        assert.equal(delivered,before+1,'native success must reach the page');
        assert.equal(state.organizer.enabled,enabled);
        assert.equal(state.organizerStatus,'saved','a real reply must end Starting or Stopping');
        assert.equal(state.organizerPending,null);
      }
    }
  }
  for(const width of [119,1201,120.5])assert.equal(validHostMessage(message({width})),false);
  unsubscribe();
});
test('organizer accepts collapsed native geometry and complete collection snapshots without relaxing other widgets',()=>{
  const collapsed=message({mode:'collapsed',width:180,height:216});
  assert.equal(validHostMessage(collapsed),true);
  const second={id:'12345678-1234-1234-1234-1234567890ab',title:'工作资料',enabled:true,running:true,count:8};
  const multi=message({enabled:true,running:true,collections:[{id:'default',title:'桌面收纳',enabled:true,running:true,count:0},second]});
  assert.equal(validHostMessage(multi),true);
  const state=reduce(initialState(),{type:'host',message:multi});
  assert.deepEqual(state.organizer.collections,multi.collections);
  for(const fields of [{mode:'collapsed',width:180,height:215},{mode:'collapsed',width:181,height:216},
    {mode:'expanded',width:119,height:216},{mode:'expanded',width:1201,height:216},{mode:'expanded',width:180,height:215},
    {collections:[]},{collections:[second]},
    {collections:[...multi.collections,second]},
    {collections:[{...multi.collections[0],extra:true}]},
    {collections:[{...multi.collections[0],enabled:'true'}]},
    {collections:[{...multi.collections[0],count:121}]},
    {collections:[{...multi.collections[0],id:'not-a-guid'}]}])
    assert.equal(validHostMessage(message(fields)),false,JSON.stringify(fields));
  for(const error of ['editor','open','invalid','target','limit']) {
    assert.equal(validHostMessage(message({error})),true,error);
    const failure=reduce(initialState(),{type:'host',message:message({error})});
    assert.doesNotMatch(organizerStatus(failure),/配置读取失败/);
  }
  for(const type of ['performance-state','almanac-state']) {
    const other={version:1,type,enabled:false,running:false,locked:false,material:'liquid-glass',
      materialStatus:'inactive',loadStatus:'fresh',error:'',width:180,height:216,result:'snapshot',saved:false};
    assert.equal(validHostMessage(other),false,'small organizer bounds do not weaken unrelated widgets');
  }
});
test('organizer settings explain reference-only removal and confirmed state',()=>{
  const state=reduce(initialState(),{type:'host',message:message({enabled:true,running:true,count:7,locked:true,material:'frosted-glass',width:520,height:400})});
  const html=organizerPage(state);
  assert.doesNotMatch(html,/data-action="organizer-toggle"/);
  assert.match(card({id:'organizer',title:'图标收纳'},state,true),/aria-checked="true"[^>]*data-action="organizer-toggle"/);
  assert.match(html,/data-select-value="frosted-glass"/);
  assert.match(organizerPage(state,'details'),/移除项目只移除引用/);
  assert.doesNotMatch(html,/未接入/);
  const pending=reduce(state,{type:'request',scope:'organizer'});
  assert.match(card({id:'organizer',title:'图标收纳'},pending,true),/data-action="organizer-toggle" disabled/);
  assert.equal(organizerNotice(pending),null);
  const closed=reduce(pending,{type:'host',message:message({enabled:false,running:false,result:'applied',saved:true,loadStatus:'loaded'})});
  assert.match(organizerNotice(closed,{previousStatus:'saving'}),/已关闭/);
  assert.equal(organizerNotice(closed,{previousStatus:'saved'}),null,'no repeated close notice on routine snapshots');
});
test('organizer icon size is bounded, acknowledged and exposed as a slider',()=>{
  assert.equal(initialState().organizer.iconScale,70);
  assert.match(organizerPage(initialState(),'details'),/两个标签.*70%/);
  const sent=[];const bridge=createBridge({postMessage:value=>sent.push(value)});
  for(const scale of [50,51,100,119,120]){
    assert.equal(bridge.send('organizer','iconScale',scale),true);
    const host=message({iconScale:scale,width:120});assert.equal(validHostMessage(host),true);
    const state=reduce(initialState(),{type:'host',message:host});
    assert.equal(state.organizer.iconScale,scale);
    assert.match(organizerPage(state),new RegExp(`id="organizer-icon-scale"[^>]*min="50"[^>]*max="120"[^>]*value="${scale}"`));
    assert.match(organizerPage(state),/id="organizer-icon-scale-number" type="number"/);
  }
  for(const scale of [49,121,90.5,'100',null]){
    assert.equal(bridge.send('organizer','iconScale',scale),false);
    assert.equal(validHostMessage(message({iconScale:scale})),false);
  }
  assert.equal(reduce(initialState(),{type:'host',message:message()}).organizer.iconScale,100);
  assert.doesNotMatch(organizerPage(initialState()),/收缩模式|展开模式/);
});

test('organizer switch immediately shows the requested direction, then settles on host success or failure',()=>{
  for(const enabled of [true,false]) {
    const confirmed=reduce(initialState(),{type:'host',message:message({enabled:!enabled,running:!enabled})});
    const pending=reduce(confirmed,{type:'request',scope:'organizer',key:'enabled',value:enabled});
    assert.equal(pending.organizer.enabled,!enabled,'request must not overwrite confirmed runtime');
    assert.match(card({id:'organizer',title:'图标收纳'},pending,true),new RegExp(`aria-checked="${enabled}"`));
    assert.match(card({id:'organizer',title:'图标收纳'},pending,true),/aria-busy="true"/);
    assert.match(organizerStatus(pending),enabled?/正在开启/:/正在关闭/);
    const snapshot=reduce(pending,{type:'host',message:message({enabled:!enabled,running:!enabled})});
    assert.match(card({id:'organizer',title:'图标收纳'},snapshot,true),new RegExp(`aria-checked="${enabled}"`));
    for(const error of ['', 'save','runtime']) {
      const actual=error?!enabled:enabled;
      const applied=reduce(snapshot,{type:'host',message:message({enabled:actual,running:actual,error,result:'applied',saved:!error})});
      assert.equal(applied.organizerPending,null);
      assert.match(card({id:'organizer',title:'图标收纳'},applied,true),new RegExp(`aria-checked="${actual}"`));
      assert.doesNotMatch(card({id:'organizer',title:'图标收纳'},applied,true),/aria-busy="true"/);
    }
    for(const scope of ['organizer','all']) {
      const disconnected=reduce(pending,{type:'unavailable',scope});
      assert.equal(disconnected.organizerPending,null);
      assert.match(card({id:'organizer',title:'图标收纳'},disconnected,true),new RegExp(`aria-checked="${!enabled}"`));
    }
  }
});
