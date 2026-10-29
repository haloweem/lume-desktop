import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {mountMusicView} from '../music-view.js';
import {initialMusicState} from '../music.js';
import {DEFAULT_MUSIC_PREFERENCES} from '../music-preference-values.js';
import {createMusicBackground,createMusicBackgroundCatalogQuery} from '../music-background.js';
import {createBridge} from '../bridge.js';

const jsdomPath=process.env.LUME_JSDOM_PATH;
const {JSDOM}=jsdomPath?createRequire(import.meta.url)(jsdomPath):{};
const flush=()=>new Promise(resolve=>setImmediate(resolve));
const catalog=(request,patch={})=>({version:1,type:'music-background-catalog',request,status:'ready',error:'',
  total:1,items:[{id:'local-fixture',name:'Downloaded fixture',type:'image',thumbnailUrl:''}],selectedId:'',...patch});
const state=(request=0,revision=0,patch={})=>({version:1,type:'music-background-state',request,revision,
  status:'ready',error:'',media:null,...patch});
const media={id:'music-copy',name:'Selected fixture',kind:'image',mimeType:'image/png',
  url:'https://lume-music.local/music-copy.png'};

test('real bridge catalog busy waits for terminal error and busy-only timeout releases subscription',async()=>{
  const listeners=new Set(),sent=[];
  const bridge=createBridge({postMessage:command=>sent.push(command),
    addEventListener:(_,listener)=>listeners.add(listener),removeEventListener:(_,listener)=>listeners.delete(listener)});
  const reply=message=>{for(const listener of [...listeners])listener({data:message});};
  const query=createMusicBackgroundCatalogQuery(bridge,{timeout:25});
  const first=assert.rejects(query(),/^Error: load$/);
  reply(catalog(1,{status:'busy',total:0,items:[]}));
  assert.equal(listeners.size,1,'initial busy must not resolve or unsubscribe');
  reply(catalog(1,{status:'error',error:'load',total:0,items:[]}));
  await first;assert.equal(listeners.size,0);
  const second=assert.rejects(query(),/catalog-timeout/);
  reply(catalog(2,{status:'busy',total:0,items:[]}));
  await second;assert.equal(listeners.size,0);
  assert.equal(sent.length,2,'query never polls or retries without a user action');
});

function fixture(t) {
  const dom=new JSDOM('<main></main><button id="outside">Outside</button>',{url:'https://lume.local/',pretendToBeVisual:true});
  const {document}=dom.window,root=document.querySelector('main');
  const listeners=new Set(),commands=[],signals=[],visualizers=[],backgrounds=[];
  const bridge=createBridge({postMessage:command=>commands.push(command),
    addEventListener:(_,listener)=>listeners.add(listener),removeEventListener:(_,listener)=>listeners.delete(listener)});
  let view;
  const background=createMusicBackground({send:bridge.musicBackground,onChange:next=>view?.updateBackground(next)});
  background.receive(state());
  const unlisten=bridge.subscribe(message=>background.receive(message));
  const query=createMusicBackgroundCatalogQuery(bridge,{timeout:1000});
  const track={id:'fixture',title:'Fixture',artist:'Test',album:'',source:'C:/fixture.wav',artwork:'',duration:60};
  const snapshot={...initialMusicState(),session:1,revision:1,pageOpen:true,pageVisible:true,status:'paused',
    current:track,currentIndex:0,queue:[track],timeline:{position:3,duration:60},
    capabilities:{canToggle:true,canSeek:true,canNext:true,canPrevious:true}};
  view=mountMusicView(root,{snapshot,send:bridge.music,
    preferences:{loaded:true,pending:false,preferences:DEFAULT_MUSIC_PREFERENCES},
    background:background.snapshot(),onBackgroundAction:action=>background[action](),
    queryBackgrounds:options=>{signals.push(options.signal);return query(options);},
    onBackgroundSelect:id=>background.select(id),
    createVisualizer:stage=>{
      const owner={stage,updates:[],disposed:0,update(...args){this.updates.push(args);},dispose(){this.disposed++;}};
      visualizers.push(owner);return owner;
    },
    createBackground:canvas=>{
      const owner={canvas,updates:[],update(value){this.updates.push(value);},dispose(){}};
      backgrounds.push(owner);return owner;
    },
    createEffects:()=>({update(){},dispose(){}}),createMaterial:()=>({update(){},dispose(){}})});
  const reply=message=>{for(const listener of [...listeners])listener({data:message});};
  const find=selector=>{const node=document.querySelector(selector);assert.ok(node,selector);return node;};
  const key=(node,key)=>node.dispatchEvent(new dom.window.KeyboardEvent('keydown',{key,bubbles:true,cancelable:true}));
  root.querySelector('[data-music-open-player]').click();
  root.querySelector('[data-music-action=lyrics]').click();
  const picker=root.querySelector('[data-music-background-picker]');
  picker.getClientRects=()=>[picker.getBoundingClientRect()];
  const open=()=>{
    find('[data-music-background-trigger]').click();
    const button=find('[data-music-background-action=gallery]');button.focus();button.click();
  };
  t.after(()=>{view.dispose();background.dispose();unlisten();dom.window.close();});
  return {dom,document,root,view,background,commands,signals,visualizers,backgrounds,snapshot,reply,find,key,open};
}

test('production source menu handles keyboard, outside pointer and folder dispatch',{skip:!jsdomPath},t=>{
  const f=fixture(t),trigger=f.find('[data-music-background-trigger]');
  const folder=f.find('[data-music-background-action=choose]'),local=f.find('[data-music-background-action=gallery]');
  trigger.focus();f.key(trigger,'ArrowDown');
  assert.equal(trigger.getAttribute('aria-expanded'),'true');assert.equal(f.document.activeElement,folder);
  f.key(folder,'ArrowDown');assert.equal(f.document.activeElement,local);
  f.key(local,'ArrowDown');assert.equal(f.document.activeElement,folder);
  f.key(folder,'End');assert.equal(f.document.activeElement,local);
  f.key(local,'Home');assert.equal(f.document.activeElement,folder);
  f.key(folder,'Escape');assert.equal(f.find('[data-music-background-menu]').hidden,true);
  assert.equal(f.document.activeElement,trigger);
  trigger.click();f.find('#outside').dispatchEvent(new f.dom.window.Event('pointerdown',{bubbles:true}));
  assert.equal(f.find('[data-music-background-menu]').hidden,true);
  trigger.click();folder.click();
  assert.deepEqual(f.commands.filter(command=>command.scope==='music-background'),[
    {version:1,scope:'music-background',action:'choose',request:1,revision:0}]);
  assert.equal(trigger.disabled,true);assert.equal(f.find('[data-music-background-menu]').hidden,true);
  f.reply(state(1));assert.equal(trigger.disabled,false);
});

test('production gallery query/select leaves lyric stage, renderer and clock updates untouched',
  {skip:!jsdomPath},async t=>{
  const f=fixture(t),stage=f.find('[data-music-stage]'),owner=f.visualizers.at(-1);
  const beforeOpen=owner.updates.length,count=f.visualizers.length,canvas=f.backgrounds[0].canvas;
  f.open();
  assert.ok(owner.updates.slice(beforeOpen).every(([, ,options])=>options.syncClock===false),
    'focus transfer may clean up seeking but cannot resynchronize the lyric clock');
  const updates=owner.updates.length;
  const command=f.commands.at(-1);
  assert.deepEqual(command,{version:1,scope:'music-background',action:'catalog',request:1,offset:0,limit:120});
  f.reply(catalog(1,{status:'busy',items:[],total:0}));await flush();
  assert.match(f.find('dialog [role=status]').textContent,/正在读取/);
  f.reply(catalog(99));await flush();assert.equal(f.document.querySelector('[data-wallpaper-id]'),null);
  f.reply(catalog(1,{selectedId:'local-fixture'}));await flush();
  assert.equal(f.find('[data-wallpaper-id]').getAttribute('aria-pressed'),'true');
  f.find('[data-wallpaper-id]').click();
  assert.deepEqual(f.commands.at(-1),{version:1,scope:'music-background',action:'select',request:1,revision:0,id:'local-fixture'});
  f.reply(state(1,0,{status:'busy'}));await flush();assert.ok(f.find('dialog'));
  f.reply(state(1,1,{media}));await flush();
  assert.equal(f.document.querySelector('dialog'),null);
  assert.equal(f.find('[data-music-stage]'),stage);assert.equal(f.visualizers.length,count);
  assert.equal(owner.disposed,0);assert.equal(owner.updates.length,updates,'no lyric clock update caused by catalog/select');
  assert.equal(f.backgrounds[0].canvas,canvas);
  assert.equal(f.backgrounds[0].updates.at(-1).media.id,'music-copy');
  assert.equal(f.commands.some(command=>command.scope==='wallpaper'),false);
});

for(const action of ['dispose','hide','page-hidden'])
  test(`production gallery ${action} aborts catalog and rejects late callbacks`,{skip:!jsdomPath},async t=>{
    const f=fixture(t);f.open();assert.equal(f.signals.length,1);
    if(action==='dispose')f.view.dispose();
    if(action==='hide')f.view.setVisible(false);
    if(action==='page-hidden')f.view.update({...f.snapshot,revision:2,pageVisible:false});
    assert.equal(f.signals[0].aborted,true);
    assert.equal(f.document.querySelector('dialog'),null);
    f.reply(catalog(1));await flush();
    assert.equal(f.document.querySelector('dialog'),null);
    assert.equal(f.commands.some(command=>command.action==='select'),false);
  });

test('modal close permits native selection completion without reopening or changing playback',
  {skip:!jsdomPath},async t=>{
  const f=fixture(t);f.open();f.reply(catalog(1));await flush();
  f.find('[data-wallpaper-id]').click();
  f.key(f.find('[data-gallery-close]'),'Escape');
  assert.equal(f.document.querySelector('dialog'),null);
  assert.equal(f.background.snapshot().pending,true,'closing modal does not invent an unsupported native cancel command');
  f.reply(state(1,1,{media}));await flush();
  assert.equal(f.background.snapshot().media.id,'music-copy');
  assert.equal(f.document.querySelector('dialog'),null);
  assert.equal(f.commands.some(command=>command.scope==='music-background'&&command.action==='cancel'),false);
});
