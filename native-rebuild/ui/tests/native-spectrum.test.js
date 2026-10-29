import test from 'node:test';
import assert from 'node:assert/strict';
import {widgets} from '../catalog.js';
import {createBridge,validHostMessage} from '../bridge.js';
import {initialState,reduce} from '../state.js';
import {card} from '../pages.js';
import {spectrumPage,spectrumStatus} from '../spectrum.js';

const message=(fields={})=>({version:1,type:'spectrum-state',enabled:false,running:false,locked:false,
  material:'none',materialStatus:'inactive',loadStatus:'fresh',error:'',width:880,height:120,
  result:'snapshot',saved:false,style:'pulse',scale:100,pulse:{barWidth:8,barGap:2},blocks:{barWidth:9,barGap:1},waves:{barWidth:5,barGap:1},...fields});

test('the spectrum widget sits right after music',()=>{
  const ids=widgets.filter(item=>!item.hidden).map(item=>item.id);
  assert.equal(ids[ids.indexOf('music')+1],'spectrum');
  const html=card(widgets.find(item=>item.id==='spectrum'),initialState(),true);
  assert.match(html,/音频频谱/);
  assert.match(html,/data-action="spectrum-toggle"/);
});

test('spectrum state carries the blocks style settings',()=>{
  assert.equal(validHostMessage(message()),true);
  assert.equal(validHostMessage(message({width:2640,height:600,scale:300,style:'blocks',blocks:{barWidth:24,barGap:0}})),true);
  assert.equal(validHostMessage(message({style:'waves',waves:{barWidth:24,barGap:12}})),true);
  for(const fields of [{material:'liquid-glass'},{style:'ripples'},{waves:{barWidth:1,barGap:1}},{scale:49},{scale:301},{pulse:{barWidth:1,barGap:2}},
    {blocks:{barWidth:25,barGap:1}},{blocks:{barWidth:9,barGap:13}},{pulse:{barWidth:8}},{barWidth:9},
    {width:199},{height:59},{width:2641},{extra:true},{error:'other'}])
    assert.equal(validHostMessage(message(fields)),false,JSON.stringify(fields));
  const state=reduce(initialState(),{type:'host',message:message({enabled:true,running:true,style:'blocks',scale:150,blocks:{barWidth:12,barGap:3},result:'applied',saved:true,loadStatus:'loaded'})});
  assert.deepEqual(state.spectrum,{enabled:true,running:true,locked:false,width:880,height:120,style:'blocks',scale:150,
    pulse:{barWidth:8,barGap:2},blocks:{barWidth:12,barGap:3},waves:{barWidth:5,barGap:1}});
  assert.equal(state.spectrumStatus,'saved');
  const failed=reduce(initialState(),{type:'host',message:message({error:'runtime'})});
  assert.equal(failed.spectrumStatus,'error');
  assert.match(spectrumStatus(failed),/声音输出设备/);
});

test('the settings page offers style, size, bar width and spacing sliders',()=>{
  const state=reduce(initialState(),{type:'host',message:message()});
  const html=spectrumPage(state,'settings');
  assert.match(html,/音频样式/);assert.match(html,/样式一/);
  assert.match(html,/组件大小/);assert.match(html,/线条宽度/);assert.match(html,/线条间隔/);
  assert.match(html,/id="spectrum-size" type="range"[^>]*data-numeric-key="size"[^>]*min="50" max="300"[^>]*value="100"/);
  assert.match(html,/id="spectrum-bar-width" type="range"[^>]*data-numeric-key="pulse.barWidth"[^>]*value="8"/);
  assert.match(html,/id="spectrum-bar-gap" type="range"[^>]*data-numeric-key="pulse.barGap"[^>]*min="0" max="12"[^>]*value="2"/);
  // The sliders edit the style in use; 样式二 keeps its own numbers.
  const second=spectrumPage(reduce(initialState(),{type:'host',message:message({style:'blocks'})}),'settings');
  assert.match(second,/data-numeric-key="blocks.barWidth"[^>]*value="9"/);
  assert.match(html,/data-action="spectrum-reset"/);
});

test('spectrum commands are exact',()=>{
  const sent=[];const bridge=createBridge({postMessage:value=>sent.push(value)});
  assert.equal(bridge.send('spectrum','enabled',true),true);
  assert.equal(bridge.send('spectrum','blocks','barWidth',9),true);
  assert.equal(bridge.send('spectrum','blocks','barGap',0),true);
  assert.equal(bridge.send('spectrum','reset'),true);
  assert.equal(bridge.send('spectrum','style','pulse'),true);
  assert.equal(bridge.send('spectrum','size',300),true);
  assert.equal(bridge.send('spectrum','pulse','barGap',12),true);
  assert.equal(bridge.send('spectrum','style','waves'),true);
  assert.equal(bridge.send('spectrum','waves','barWidth',5),true);
  assert.deepEqual(sent,['v1|spectrum|enabled|true','v1|spectrum|blocks|barWidth|9','v1|spectrum|blocks|barGap|0','v1|spectrum|reset',
    'v1|spectrum|style|pulse','v1|spectrum|size|300','v1|spectrum|pulse|barGap|12','v1|spectrum|style|waves','v1|spectrum|waves|barWidth|5']);
  for(const parts of [['enabled','true'],['blocks','barWidth','9'],['blocks','barWidth',1.5],['blocks','size',9],
    ['style','ripples'],['size',49],['size','100'],['material','liquid-glass'],['reset',true]])
    assert.equal(bridge.send('spectrum',...parts),false,JSON.stringify(parts));
});
