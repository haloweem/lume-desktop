import test from 'node:test';
import assert from 'node:assert/strict';
import {existsSync,readFileSync} from 'node:fs';
import {createBridge,validHostMessage} from '../bridge.js';
import {initialState,reduce} from '../state.js';
import {modules,localizedItem} from '../catalog.js';
import {catalog,card,search} from '../pages.js';
import {setLanguage} from '../localization.js';
import {mountRadialShortcut} from '../radial.js';

const snapshot=(fields={})=>({version:1,type:'radial-state',enabled:false,running:false,visible:false,
  material:'liquid-glass',shortcutCode:9,materialStatus:'inactive',loadStatus:'fresh',error:'',
  result:'snapshot',saved:false,...fields});
const allowed=code=>{
  if(!Number.isSafeInteger(code)||code<1||code>4095)return false;
  const vk=code%256,mods=Math.floor(code/256);
  const key=[8,9,13,19,20,32,44,45,46,144,145,186,187,188,189,190,191,192,219,220,221,222,226].includes(vk)||
    (vk>=33&&vk<=40)||(vk>=48&&vk<=57)||(vk>=65&&vk<=90)||
    (vk>=96&&vk<=111)||(vk>=112&&vk<=135);
  return key&&!((vk===9&&(mods&9))||(vk===46&&(mods&3)===3)||(vk===76&&(mods&8)));
};

test('radial bridge sends only exact v1 commands and supported canonical shortcut codes',()=>{
  const sent=[],bridge=createBridge({postMessage:value=>sent.push(value)});
  for(const parts of [['enabled',true],['enabled',false],['material','liquid-glass'],
    ['material','frosted-glass'],['shortcut',9],['shortcut',834],['reset']]) {
    assert.equal(bridge.send('radial',...parts),true,parts.join('|'));
  }
  assert.deepEqual(sent,['v1|radial|enabled|true','v1|radial|enabled|false',
    'v1|radial|material|liquid-glass','v1|radial|material|frosted-glass',
    'v1|radial|shortcut|9','v1|radial|shortcut|834','v1|radial|reset']);
  for(const parts of [[],['reset',true],['enabled','true'],['enabled',1],['material','solid'],
    ['shortcut','9'],['shortcut','09'],['shortcut',9.5],['shortcut',NaN],['shortcut',Infinity],
    ['shortcut',0],['shortcut',4096],['shortcut',-9],['shortcut',9,'extra'],['snapshot'],['reset\0']])
    assert.equal(bridge.send('radial',...parts),false,JSON.stringify(parts));
  for(let code=1;code<=4095;code++)
    assert.equal(bridge.send('radial','shortcut',code),allowed(code),`code ${code}`);
  assert.equal(createBridge({postMessage(){throw new Error('offline');}}).send('radial','reset'),false);
  assert.equal(createBridge(null).send('radial','reset'),false);
});

test('radial capture bridge accepts only exact boolean commands without changing preferences',()=>{
  const sent=[],bridge=createBridge({postMessage:value=>sent.push(value)});
  assert.equal(bridge.send('radial','capture',true),true);
  assert.equal(bridge.send('radial','capture',false),true);
  assert.deepEqual(sent,['v1|radial|capture|true','v1|radial|capture|false']);
  for(const parts of [[],[true,'extra'],['true'],['false'],[1],[0],[null],[undefined]])
    assert.equal(bridge.send('radial','capture',...parts),false,JSON.stringify(parts));
  assert.equal(createBridge(null).send('radial','capture',false),false);
  assert.equal(createBridge({postMessage(){throw new Error('offline');}}).send('radial','capture',false),false);
});

function recorderFixture({status='idle',present=true,focusable=true,onCaptureChange}={}) {
  const window=new EventTarget(),root=new EventTarget(),attributes=new Map();
  const state={...initialState(),radialStatus:status};
  const document={activeElement:null,defaultView:window};
  const recorder={textContent:'Tab',disabled:false,
    focus(){if(focusable)document.activeElement=recorder;},
    setAttribute(key,value){attributes.set(key,value);},
  };
  const feedback={textContent:'',hidden:true};
  root.ownerDocument=document;
  root.querySelector=selector=>selector==='[data-radial-shortcut]'?(present?recorder:null):feedback;
  const events=[];
  const controller=mountRadialShortcut(root,{getState:()=>state,
    onShortcut:code=>events.push(['shortcut',code]),
    ...(onCaptureChange===false?{}:{onCaptureChange:active=>events.push(['capture',active])})});
  const key=(type,fields={})=>{
    const event=new Event(type,{cancelable:true});
    Object.defineProperty(event,'target',{value:recorder});
    Object.assign(event,{key:'Tab',code:'Tab',...fields});root.dispatchEvent(event);
    return event;
  };
  return {state,root,recorder,attributes,window,events,controller,key};
}

test('radial recorder ends transient capture before persisting a complete logical shortcut',()=>{
  const f=recorderFixture();
  try {
    assert.equal(f.controller.start(),true);assert.deepEqual(f.events,[['capture',true]]);
    assert.equal(f.attributes.get('aria-pressed'),'true');
    f.key('keydown',{key:'Control',code:'ControlLeft'});
    f.key('keyup',{key:'Control',code:'ControlLeft'});
    assert.deepEqual(f.events,[['capture',true]],'modifiers do not end capture or save');
    const key={key:'a',code:'KeyQ',keyCode:65,ctrlKey:true};
    f.key('keydown',key);assert.deepEqual(f.events,[['capture',true]],'keydown alone never persists');
    f.key('keyup',key);
    assert.deepEqual(f.events,[['capture',true],['capture',false],['shortcut',577]]);
    f.controller.cancel();f.controller.dispose();
    assert.equal(f.events.length,3,'completed capture ends only once');
  } finally {f.controller.dispose();}
});

test('radial recorder emits capture end exactly once on every cancellation path',()=>{
  for(const cancel of [f=>f.controller.cancel(),f=>f.key('keydown',{key:'Escape',code:'Escape'}),
    f=>f.key('blur'),f=>f.window.dispatchEvent(new Event('blur')),
    f=>f.key('compositionstart'),f=>f.controller.dispose()]) {
    const f=recorderFixture();
    try {
      f.controller.start();f.key('keydown',{key:'a',code:'KeyA'});cancel(f);
      f.key('keyup',{key:'a',code:'KeyA'});f.controller.cancel();
      assert.deepEqual(f.events,[['capture',true],['capture',false]]);
      assert.equal(f.attributes.get('aria-pressed'),'false');assert.equal(f.recorder.textContent,'Tab');
    } finally {f.controller.dispose();}
  }
});

test('radial recorder does not announce failed, deferred or ordinary page starts',()=>{
  for(const options of [{present:false},{focusable:false},{status:'loading'},{status:'saving'}]) {
    const f=recorderFixture(options);
    try {
      assert.equal(f.controller.start(),false,JSON.stringify(options));f.controller.sync();
      f.controller.cancel();f.controller.dispose();assert.deepEqual(f.events,[]);
    } finally {f.controller.dispose();}
  }
  const f=recorderFixture({status:'loading'});
  try {
    assert.equal(f.controller.start({waitForState:true}),false);assert.deepEqual(f.events,[]);
    f.controller.sync();assert.deepEqual(f.events,[],'still loading');
    f.state.radialStatus='idle';f.controller.sync();
    assert.deepEqual(f.events,[['capture',true]],'only successful deferred start announces capture');
    f.controller.start();assert.deepEqual(f.events,[['capture',true],['capture',false],['capture',true]]);
  } finally {f.controller.dispose();}
  const ordinary=recorderFixture();
  ordinary.controller.sync();ordinary.controller.cancel();ordinary.controller.dispose();
  assert.deepEqual(ordinary.events,[],'mount, render, sync and disposal do not initiate capture');
  const optional=recorderFixture({onCaptureChange:false});
  optional.controller.start();optional.key('keydown');optional.key('keyup');optional.controller.dispose();
  assert.deepEqual(optional.events,[['shortcut',9]],'callback remains optional');
});

test('radial state schema has exact keys, types, enums, safe shortcuts and truthful saved receipts',()=>{
  assert.equal(validHostMessage(snapshot()),true);
  const keys=Object.keys(snapshot());
  for(const key of keys) {
    const missing=snapshot();delete missing[key];
    assert.equal(validHostMessage(missing),false,`missing ${key}`);
  }
  for(const fields of [{version:2},{extra:0},{enabled:1},{running:'false'},{visible:null},
    {material:'glass'},{shortcutCode:'9'},{shortcutCode:0},{shortcutCode:4096},{shortcutCode:9.1},
    {shortcutCode:27},{shortcutCode:265},{shortcutCode:814},{shortcutCode:2124},{shortcutCode:2057},
    {shortcutCode:229},{shortcutCode:231},{shortcutCode:16},{shortcutCode:17},{shortcutCode:18},
    {materialStatus:'ready'},{loadStatus:'backup'},{error:'invalid'},{result:'cancelled'},
    {saved:1},{saved:true},{result:'applied',saved:true,error:'save'},
    {result:'applied',saved:true,loadStatus:'failed'}])
    assert.equal(validHostMessage(snapshot(fields)),false,JSON.stringify(fields));
  for(const materialStatus of ['inactive','native-ready','native-starting','transparent-fallback'])
    assert.equal(validHostMessage(snapshot({materialStatus})),true);
  for(const loadStatus of ['fresh','loaded','recovered','failed'])
    assert.equal(validHostMessage(snapshot({loadStatus})),true);
  for(const error of ['','runtime','save','rollback'])assert.equal(validHostMessage(snapshot({error})),true);
  assert.equal(validHostMessage(snapshot({result:'applied',loadStatus:'loaded',saved:true})),true);
  assert.equal(validHostMessage({version:1,type:'navigate',page:'radial'}),true);
  assert.equal(validHostMessage({version:1,type:'navigate',page:'radial',section:'shortcut'}),true);
  for(const section of ['','settings','Shortcut',null,1])
    assert.equal(validHostMessage({version:1,type:'navigate',page:'radial',section}),false);
  assert.equal(validHostMessage({version:1,type:'navigate',page:'radial',section:'extra'}),false);
  const received=[],listeners=new Map(),bridge=createBridge({addEventListener:(k,v)=>listeners.set(k,v),
    removeEventListener:k=>listeners.delete(k)});
  const unsubscribe=bridge.subscribe(value=>received.push(value));
  listeners.get('message')({data:snapshot({extra:'payload'})});
  listeners.get('message')({data:snapshot()});
  assert.deepEqual(received,[snapshot()]);unsubscribe();assert.equal(listeners.size,0);
});

test('radial reducer preserves confirmed preferences until applied, reports failure without cross-scope changes',()=>{
  let state=initialState();
  assert.deepEqual(state.radial,{enabled:false,running:false,visible:false,material:'liquid-glass',shortcutCode:9});
  assert.equal(state.radialStatus,'loading');assert.equal(state.radialLoadStatus,'fresh');
  assert.equal(reduce(state,{type:'navigate',view:'radial'}).view,'radial');
  const before=state.radial;
  state=reduce(state,{type:'request',scope:'radial',key:'enabled',value:true});
  assert.equal(state.radialStatus,'saving');assert.equal(state.radial,before);
  state=reduce(state,{type:'host',message:snapshot({materialStatus:'native-starting'})});
  assert.equal(state.radialStatus,'saving');assert.equal(state.calendarStatus,'loading');
  state=reduce(state,{type:'host',message:snapshot({enabled:true,running:true,visible:true,
    material:'frosted-glass',shortcutCode:834,result:'applied',saved:true,loadStatus:'loaded'})});
  assert.equal(state.radialStatus,'saved');assert.equal(state.radial.shortcutCode,834);
  assert.equal(state.radial.visible,true);assert.equal(state.radial.material,'frosted-glass');
  state=reduce(state,{type:'request',scope:'radial'});
  state=reduce(state,{type:'host',message:snapshot({error:'save',result:'applied'})});
  assert.equal(state.radialStatus,'error');assert.equal(state.radialError,'save');
  assert.equal(state.radial.enabled,false);assert.equal(state.radial.shortcutCode,9);
  assert.equal(reduce(state,{type:'host',message:snapshot({loadStatus:'failed'})}).radialStatus,'error');
  assert.equal(reduce(state,{type:'host',message:snapshot({loadStatus:'recovered'})}).radialStatus,'recovered');
  assert.equal(reduce(state,{type:'unavailable',scope:'radial'}).radialStatus,'unavailable');
  assert.equal(reduce(initialState(),{type:'unavailable',scope:'all'}).radialStatus,'unavailable');
});

test('radial definition remains available for later work but is hidden from catalog and search',()=>{
  const index=modules.findIndex(item=>item.id==='radial');
  assert.ok(index>=0,'radial entry exists');assert.equal(modules[index+1].id,'widgets');
  assert.equal(modules[index].title,'轮盘菜单');assert.equal(modules[index].toggle,true);
  assert.equal(modules[index].hidden,true);
  const state={...initialState(),radial:{enabled:true},radialStatus:'saving'};
  assert.match(card(modules[index],state),/aria-checked="true"[^>]*data-action="radial-toggle" disabled/);
  assert.doesNotMatch(catalog(state,false),/data-open="radial"/);
  assert.doesNotMatch(search({...initialState(),query:'轮盘'}),/data-open="radial"/);
  try {setLanguage('en');assert.equal(localizedItem(modules[index]).title,'Radial menu');}
  finally {setLanguage('zh-CN');}
});

test('radial recorder uses Windows VK before logical ASCII and never infers layout-sensitive physical keys',async()=>{
  const {shortcutFromEvent}=await import('../radial.js');
  assert.equal(shortcutFromEvent({code:'KeyQ',key:'a',keyCode:65,ctrlKey:true}),577,'AZERTY native Ctrl+A');
  assert.equal(shortcutFromEvent({code:'KeyQ',key:'a',ctrlKey:true}),577,'AZERTY logical Ctrl+A fallback');
  assert.equal(shortcutFromEvent({code:'KeyQ',key:'A',keyCode:0,ctrlKey:true,shiftKey:true}),1601);
  assert.equal(shortcutFromEvent({code:'KeyZ',key:'y',keyCode:89,ctrlKey:true}),601,'QWERTZ native Ctrl+Y');
  assert.equal(shortcutFromEvent({code:'Digit1',key:'&',keyCode:49,ctrlKey:true}),561,'native VK wins over shifted display character');
  assert.equal(shortcutFromEvent({code:'Numpad1',key:'1',keyCode:97,ctrlKey:true}),609,'native numpad VK is preserved');
  assert.equal(shortcutFromEvent({code:'KeyA',key:'q',keyCode:65,ctrlKey:true}),577,'native VK wins over a differing logical key');
  assert.equal(shortcutFromEvent({code:'KeyQ',key:'a',keyCode:255,ctrlKey:true}),null,'unsupported native VK cannot fall back to a permitted character');
  for(const keyCode of [0,undefined,NaN,-1,256,'65'])
    assert.equal(shortcutFromEvent({code:'KeyQ',key:'a',keyCode,ctrlKey:true}),577,'invalid or absent VK uses logical fallback');
  for(const keyCode of [16,17,18,27,229,231])
    assert.equal(shortcutFromEvent({code:'KeyQ',key:'a',keyCode,ctrlKey:true}),null,'blocked native keys never become letters');
  for(const [key,vk] of [['a',65],['Z',90],['0',48],['9',57],[';',186],[':',186],
    ['=',187],['+',187],[',',188],['<',188],['-',189],['_',189],['.',190],['>',190],
    ['/',191],['?',191],['`',192],['~',192],['[',219],['{',219],['\\',220],['|',220],
    [']',221],['}',221],["'",222],['"',222],[' ',32]])
    assert.equal(shortcutFromEvent({key,ctrlKey:true}),512+vk,`logical ASCII ${key}`);
  for(const code of ['KeyQ','Digit1','Semicolon','Quote','IntlBackslash'])
    assert.equal(shortcutFromEvent({code,ctrlKey:true}),null,`no layout-sensitive ${code} fallback`);
  assert.equal(shortcutFromEvent({code:'KeyQ',key:'é',ctrlKey:true}),null,'non-ASCII key is not a physical ASCII shortcut');
  assert.equal(shortcutFromEvent({code:'KeyQ',key:'l',keyCode:76,metaKey:true}),null,'logical Win+L stays reserved');
  assert.equal(shortcutFromEvent({code:'KeyQ',key:'Tab',keyCode:9,altKey:true}),null,'native Alt+Tab stays reserved');
});

test('radial screenshot is available only in slot 6, with top empty and translation in slot 1',async()=>{
  const {radialPage}=await import('../radial.js');
  const state=reduce(initialState(),{type:'host',message:snapshot()});
  try {
    for(const [language,screenshot,translation,empty,available,unavailable] of [
      ['en','Screenshot','Translation','Empty','Available','Unavailable'],
      ['zh-CN','截图','翻译','待添加','可用','暂不可用'],
    ]) {
      setLanguage(language);
      const slots=[...radialPage(state).matchAll(/<li\b[^>]*data-radial-slot="(\d)"[^>]*>[\s\S]*?<\/li>/g)];
      assert.equal(slots.length,10);
      for(const [slot,index] of slots) {
        const active=index==='6',translate=index==='1';
        assert.equal(slot.includes('data-radial-available="true"'),active,`slot ${index} availability`);
        assert.equal(slot.includes('class="radial-slot active"'),active,`slot ${index} emphasis`);
        assert.equal(slot.includes('aria-disabled="true"'),!active,`slot ${index} disabled`);
        assert.equal(slot.includes('data-radial-placeholder'),!active&&!translate,`slot ${index} placeholder`);
        assert.ok(slot.includes(`class="radial-slot-label">${active?screenshot:translate?translation:empty}</span>`));
        assert.ok(slot.includes(`class="radial-slot-status">${active?available:translate?unavailable:'--'}</span>`));
      }
    }
  } finally {setLanguage('zh-CN');}
});

test('radial page reuses settings controls, shows ten fixed slots, localized controls and parent-owned actions',async()=>{
  assert.ok(existsSync(new URL('../radial.js',import.meta.url)),'dedicated radial page exists');
  const {radialPage,radialNotice,shortcutFromEvent,shortcutLabel}=await import('../radial.js');
  let state=reduce(initialState(),{type:'host',message:snapshot()});
  const html=radialPage(state);
  assert.match(html,/data-settings-scope="radial"/);
  assert.equal((html.match(/data-radial-slot="\d"/g)||[]).length,10);
  assert.match(html,/data-radial-slot="6"[^>]*data-radial-available="true"/);
  assert.match(html,/data-radial-slot="1"[^>]*aria-disabled="true"/);
  assert.match(html,/截图/);assert.match(html,/翻译/);
  assert.equal((html.match(/data-radial-placeholder/g)||[]).length,8);
  assert.match(html,/data-select="radial-material"/);
  assert.match(html,/data-select-value="liquid-glass"/);assert.match(html,/data-select-value="frosted-glass"/);
  assert.match(html,/data-radial-shortcut[^>]*title=/);assert.match(html,/Tab/);
  assert.match(html,/data-action="radial-reset"[^>]*title=/);
  assert.doesNotMatch(html,/data-action="radial-(screenshot|translate|context)/);
  assert.equal(shortcutFromEvent({code:'Tab',key:'Tab'}),9);
  assert.equal(shortcutFromEvent({code:'KeyB',key:'b',altKey:true,ctrlKey:true}),834);
  assert.equal(shortcutFromEvent({code:'Tab',key:'Tab',altKey:true}),null);
  assert.equal(shortcutFromEvent({code:'KeyL',key:'l',metaKey:true}),null);
  assert.equal(shortcutFromEvent({code:'KeyA',key:'a',isComposing:true}),null);
  for(const keyCode of [229,231])assert.equal(shortcutFromEvent({code:'KeyA',key:'a',keyCode}),null);
  for(const [code,value,fields={}] of [['Space',32],['Home',36],['ArrowRight',39],['Delete',46],
    ['Digit0',48,{key:'0'}],['KeyZ',90,{key:'z'}],['Numpad0',96],['NumpadEnter',13],['NumpadAdd',107],['Pause',19],
    ['CapsLock',20],['PrintScreen',44],['NumLock',144],['ScrollLock',145],['Semicolon',186,{key:';'}],
    ['Quote',222,{key:"'"}],['IntlBackslash',226,{keyCode:226}]])
    assert.equal(shortcutFromEvent({code,...fields}),value,code);
  for(let key=1;key<=24;key++)assert.equal(shortcutFromEvent({code:`F${key}`}),111+key);
  assert.equal(shortcutLabel(9),'Tab');assert.equal(shortcutLabel(834),'Ctrl + Alt + B');
  state=reduce(state,{type:'request',scope:'radial'});
  assert.match(radialPage(state),/data-select="radial-material" disabled/);
  assert.match(radialPage(state),/data-radial-shortcut[^>]*disabled/);
  assert.match(radialPage(state),/data-action="radial-reset"[^>]*disabled/);
  state=reduce(state,{type:'host',message:snapshot({result:'applied',error:'rollback'})});
  assert.match(radialNotice(state),/恢复失败/);assert.equal(radialNotice(state,{hasInlineStatus:true}),null);
  try {
    setLanguage('en');const english=radialPage(state);
    for(const text of ['Radial menu','Screenshot','Translation','Material','Shortcut','Restore defaults'])
      assert.ok(english.includes(text),text);
    assert.doesNotMatch(english,/轮盘|截图|翻译|材质|快捷键|恢复默认/);
  } finally {setLanguage('zh-CN');}
  const app=readFileSync(new URL('../app.js',import.meta.url),'utf8');
  assert.match(app,/case "radial"/);assert.match(app,/syncRadialControls/);assert.match(app,/mountRadialShortcut/);
});
