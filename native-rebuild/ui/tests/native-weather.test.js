import test from 'node:test';
import assert from 'node:assert/strict';
import {createBridge,validHostMessage} from '../bridge.js';
import {initialState,reduce} from '../state.js';
import {card} from '../pages.js';
import {weatherPage,weatherNotice,weatherStatus,weatherCities} from '../weather.js';

const message=(fields={})=>({version:1,type:'weather-state',enabled:false,running:false,locked:false,
  material:'liquid-glass',materialStatus:'inactive',loadStatus:'fresh',error:'',width:320,height:152,
  result:'snapshot',saved:false,city:'auto',loading:false,weatherError:'',cacheWriteFailed:false,hasWeather:false,systemLocation:'on',...fields});
const data={hasWeather:true,source:'met-norway',fetchedAt:1790401977566,sourceUpdatedAt:1790399929000,validAt:1790400000000,remainingDay:true};

test('a widget the desktop layer refused reports its own error',()=>{
  assert.equal(validHostMessage(message({error:'layer'})),true);
  const state=reduce(initialState(),{type:'host',message:message({error:'layer'})});
  assert.equal(state.weatherStatus,'error');
  assert.match(weatherStatus(state),/桌面层/);
  assert.equal(validHostMessage(message({error:'layer-extra'})),false);
});
test('weather bridge restricts cities and separates data failures from settings results',()=>{
  const sent=[];const bridge=createBridge({postMessage:v=>sent.push(v)});
  for(const [city] of weatherCities)assert.equal(bridge.send('weather','city',city),true);
  for(const c of [['enabled',true],['enabled',false],['material','frosted-glass'],['material','liquid-glass'],['reset'],['refresh']])
    assert.equal(bridge.send('weather',...c),true);
  for(const c of [['city','other'],['city','上海'],['city','shanghai',1],['enabled','true'],['refresh',1],['material','solid']])
    assert.equal(bridge.send('weather',...c),false);
  assert.equal(sent[0],'v1|weather|city|auto');
  assert.equal(validHostMessage(message()),true);
  assert.equal(validHostMessage({version:1,type:'navigate',page:'weather'}),true);
  assert.equal(validHostMessage(message(data)),true);
  for(const fields of [{city:'other'},{width:319},{height:151},{loading:1},{weatherError:'other'},{extra:1},{saved:true},
    {source:'met-norway'},{hasWeather:true},{...data,validAt:-1},{...data,source:'open-meteo'},{...data,remainingDay:1},
    {systemLocation:'maybe'},{systemLocation:true}])
    assert.equal(validHostMessage(message(fields)),false,JSON.stringify(fields));
  assert.equal(validHostMessage(message({saved:true,result:'applied',loadStatus:'loaded',weatherError:'unavailable'})),true);
});
test('weather keeps pending saves through background updates and uses confirmed city after rollback',()=>{
  let state=reduce(initialState(),{type:'request',scope:'weather'});
  assert.equal(state.weatherStatus,'saving');assert.equal(state.almanacStatus,'loading');
  state=reduce(state,{type:'host',message:message({loading:true})});
  assert.equal(state.weatherStatus,'saving');
  state=reduce(state,{type:'host',message:message({city:'shanghai',saved:true,result:'applied',loadStatus:'loaded',enabled:true,weatherError:'unavailable'})});
  assert.equal(state.weatherStatus,'saved');assert.equal(state.weather.city,'shanghai');
  assert.match(weatherStatus(state),/天气不可用/);
  assert.equal(weatherNotice(state,{previousStatus:'saving'}),'天气设置已应用并保存');
  state=reduce(state,{type:'host',message:message({city:'shanghai',result:'applied',error:'save'})});
  assert.equal(state.weatherStatus,'error');assert.match(weatherStatus(state),/保存失败/);
  assert.equal(state.weather.city,'shanghai');
  assert.equal(reduce(initialState(),{type:'unavailable',scope:'all'}).weatherStatus,'unavailable');
});
test('weather settings preserve original controls and expose source attribution',()=>{
  const state=reduce(initialState(),{type:'host',message:message({enabled:true,city:'shanghai'})});
  const html=weatherPage(state);
  assert.match(html,/data-settings-scope="weather"/);
  assert.doesNotMatch(html,/data-action="weather-toggle"/);
  assert.match(card({id:'weather',title:'天气'},state,true),/aria-checked="true"[^>]*data-action="weather-toggle"/);
  for(const [city] of weatherCities)assert.ok(html.includes(`data-select-value="${city}"`));
  assert.match(html,/data-action="weather-reset"/);
  assert.match(weatherPage(state,'details'),/CC BY 4.0/);
  assert.doesNotMatch(weatherPage(state,'details'),/右上角刷新|刷新按钮的提示|top-right button|refresh button tooltip/);
  assert.match(weatherPage(state,'details'),/右键菜单或 F5/);
  assert.doesNotMatch(html,/未接入/);
  const pending=reduce(state,{type:'request',scope:'weather'});
  assert.match(card({id:'weather',title:'天气'},pending,true),/data-action="weather-toggle" disabled/);
  for(const control of ['weather-material','weather-city'])
    assert.match(weatherPage(pending),new RegExp(`data-select="${control}" disabled`));
  assert.match(weatherPage(pending),/data-action="weather-reset" disabled/);
});
test('automatic location with Windows location off offers to open location settings',async()=>{
  const {weatherPage}=await import('../weather.js');
  const base=reduce(initialState(),{type:'host',message:message({enabled:true,running:true})});
  const off=reduce(initialState(),{type:'host',message:message({enabled:true,running:true,systemLocation:'off'})});
  assert.equal(off.weatherData.systemLocation,'off');
  assert.match(weatherPage(off),/data-action="weather-location-settings"/);
  assert.doesNotMatch(weatherPage(base),/weather-location-settings/);
  const fixed=reduce(initialState(),{type:'host',message:message({enabled:true,running:true,systemLocation:'off',city:'beijing'})});
  assert.doesNotMatch(weatherPage(fixed),/weather-location-settings/);
  const sent=[];const bridge=createBridge({postMessage:value=>sent.push(value)});
  assert.equal(bridge.send('weather','location-settings'),true);
  assert.equal(sent.at(-1),'v1|weather|location-settings');
});
