import test from 'node:test';
import assert from 'node:assert/strict';
import {createComponentSettingsCommands} from '../component-settings-commands.js';
import {initialState} from '../state.js';
import {createBridge} from '../bridge.js';
import {setLanguage} from '../localization.js';

function fixture() {
  const state=initialState(),commands=[],direct=[],notices=[];
  return {state,commands,direct,notices,controller:createComponentSettingsCommands({getState:()=>state,
    request:(...args)=>commands.push(args),send:(...args)=>direct.push(args),showNotice:text=>notices.push(text)})};
}
function target(id,value,kind='') {
  return {id,value:String(value),valueAsNumber:Number(value),dataset:{},
    matches:selector=>kind&&selector.includes(`[data-${kind}]`)};
}

test('component selections keep exact command keys and numeric conversions',()=>{
  const f=fixture();
  const strings={
    'clock-style':['clock','style'],'calendar-theme':['calendar','theme'],'calendar-material':['calendar','material'],
    'performance-material':['performance','material'],'spectrum-style':['spectrum','style'],
    'todayTodo-material':['todayTodo','material'],'almanac-material':['almanac','material'],
    'weather-material':['weather','material'],
    'organizer-material':['organizer','material'],'minimize-engine':['minimize','engine'],
    'taskbar-hide-behavior':['taskbar','hideBehavior'],'radial-material':['radial','material'],
    'dock-material':['dock','material'],'dynamicIsland-size':['dynamicIsland','size'],'dynamicIsland-material':['dynamicIsland','material'],
  };
  for(const [id,prefix] of Object.entries(strings)) {
    assert.equal(f.controller.select(id,'selected'),true);
    assert.deepEqual(f.commands.at(-1),[...prefix,'selected']);
  }
  for(const [id,key] of Object.entries({'dock-icon-scale':'iconScale','dock-position':'position',
    'dock-bottom-offset':'bottomOffset','dock-icon-spacing':'iconSpacing'})) {
    f.controller.select(id,'42');assert.deepEqual(f.commands.at(-1),['dock',key,42]);
  }
  for(const [value,expected] of [['desktop',false],['above',true]]) {
    f.controller.select('dynamicIsland-layer',value);assert.deepEqual(f.commands.at(-1),['dynamicIsland','topmost',expected]);
  }
  const before=f.commands.length;
  assert.equal(f.controller.select('dynamicIsland-layer','unknown'),true);
  assert.equal(f.controller.select('wallpaper-kind','image'),false);
  assert.equal(f.controller.select('weather-city','shanghai'),false);
  assert.equal(f.commands.length,before);
});

test('component buttons toggle the current confirmed property and preserve special command names',()=>{
  const f=fixture();
  for(const scope of ['clock','calendar','performance','spectrum','todayTodo','almanac','weather',
    'organizer','icons','taskbar','minimize','radial','dock']) {
    for(const enabled of [false,true]) {
      f.state[scope].enabled=enabled;
      assert.equal(f.controller.action(scope==='clock'?'toggle':`${scope}-toggle`),true);
      assert.deepEqual(f.commands.at(-1),[scope,'enabled',!enabled]);
    }
    assert.equal(f.controller.action(scope==='clock'?'reset':`${scope}-reset`),true);
    assert.deepEqual(f.commands.at(-1),[scope,'reset']);
  }
  for(const [scope,action,key,property] of [
    ['dock','reflection','reflection','reflection'],['dock','show-running','showRunning','showRunning'],
    ['dock','system-tray','systemTray','systemTray'],
    ['icons','autoHide','autoHide','enableAutoHide'],['icons','restoreOnInput','restoreOnInput','showIconsOnAnyInput'],
    ['taskbar','transparent','transparent','transparentTaskbar'],['taskbar','autoHide','autoHide','autoHideTaskbar'],
    ...['restore','launch','multiMonitor','unhideTaskbar'].map(key=>['minimize',key,key,key]),
  ]) for(const value of [true,false]) {
    f.state[scope][property]=value;f.controller.action(`${scope}-${action}`);
    assert.deepEqual(f.commands.at(-1),[scope,key,!value]);
  }
  for(const key of ['textColor','highlightColor']) {
    f.controller.action(`calendar-${key}`);assert.deepEqual(f.commands.at(-1),['calendar',key]);
  }
  assert.equal(f.controller.action('weather-location-settings'),true);
  assert.deepEqual(f.direct,[['weather','location-settings']]);
  for(const key of ['toggle','lock','hideFullscreen','topmost','hover','autoHide']) {
    const property=key==='toggle'?'enabled':key==='lock'?'locked':key;
    f.controller.action(`dynamicIsland-${key}`);assert.deepEqual(f.commands.at(-1),['dynamicIsland',property,!f.state.dynamicIsland[property]]);
  }
  for(const key of ['reset','resetAppearance','expand','collapse','focus','stopFocus']) {
    f.controller.action(`dynamicIsland-${key}`);assert.deepEqual(f.commands.at(-1),['dynamicIsland',key]);
  }
  for(const action of [undefined,'music-toggle','notes-toggle','back','clear','wallpaper-apply'])assert.equal(f.controller.action(action),false);
});

test('number submissions honor bounds, restore invalid values and keep busy duration fields unchanged',()=>{
  const f=fixture();f.state.minimizeStatus='idle';setLanguage('en');
  try {
    for(const [id,scope,key,min,max,property] of [
      ['minimize-durationMs','minimize','durationMs',50,2000,'durationMs'],
      ['minimize-unhideDurationMs','minimize','unhideDurationMs',0,5000,'unhideDurationMs'],
      ['icons-delay','icons','delay',3,60,'autoHideDelay'],
    ]) {
      for(const value of [min,max]){assert.equal(f.controller.change(target(id,value)),true);assert.deepEqual(f.commands.at(-1),[scope,key,value]);}
      for(const value of [min-1,max+1,min+.5,NaN,Infinity]) {
        const before=f.commands.length,input=target(id,value);
        f.controller.change(input);assert.equal(f.commands.length,before);
        assert.equal(input.value,String(f.state[scope][property]));assert.match(f.notices.at(-1),/must be an integer between/);
      }
    }
    for(const status of ['saving','loading']) {
      f.state.minimizeStatus=status;const input=target('minimize-durationMs',49),before=f.notices.length;
      f.controller.change(input);assert.equal(input.value,'49');assert.equal(f.notices.length,before);
    }
  } finally {setLanguage('zh-CN');}
});

test('range preview changes only local display; submissions keep domain ownership and bridge validation',()=>{
  const f=fixture(),output={textContent:''},attributes={},styles={};
  const range={...target('organizer-icon-scale',85,'organizer-range'),min:'50',max:'120',dataset:{rangeUnit:'%'},
    closest:selector=>selector==='[data-setting-range]'?{querySelector:()=>output}:range,
    setAttribute:(name,value)=>attributes[name]=value,style:{setProperty:(name,value)=>styles[name]=value}};
  assert.equal(f.controller.input(range),true);assert.equal(output.textContent,'85%');
  assert.equal(attributes['aria-valuetext'],'85%');assert.equal(styles['--range-progress'],'50%');assert.deepEqual(f.commands,[]);
  for(const value of [49,50,120,121,85.5]) {
    const before=f.commands.length;range.valueAsNumber=value;f.controller.change(range);
    assert.equal(f.commands.length-before,[50,120].includes(value)?1:0);
  }
  const sent=[],bridge=createBridge({postMessage:command=>sent.push(command)});
  const controller=createComponentSettingsCommands({getState:()=>f.state,request:(...args)=>bridge.send(...args),send:()=>{},showNotice:()=>{}});
  controller.change(target('dock-icon-scale',100,'dock-range'));
  assert.equal(sent.at(-1),'v1|dock|iconScale|100');
  const count=sent.length;
  controller.change(target('dock-icon-scale',10000,'dock-range'));
  controller.select('dock-icon-scale','invalid');
  assert.equal(sent.length,count,'bridge still rejects invalid or out-of-range native payloads');
  assert.equal(controller.change(target('wallpaper-kind','image')),false);
});
