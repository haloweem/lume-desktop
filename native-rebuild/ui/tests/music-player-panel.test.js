import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {mountMusicView} from '../music-view.js';
import {initialMusicState} from '../music.js';
import {DEFAULT_MUSIC_PREFERENCES as defaults} from '../music-preferences.js';
import {mountMusicVisualizer} from '../music-visualizer.js';

const jsdomPath=process.env.LUME_JSDOM_PATH;
const {JSDOM}=jsdomPath?createRequire(import.meta.url)(jsdomPath):{};
function fixture(t,viewState,extra={}) {
  const dom=new JSDOM('<main></main>',{url:'https://lume.local/',pretendToBeVisual:true});
  Object.defineProperty(dom.window,'isSecureContext',{value:true,configurable:true});
  const root=dom.window.document.querySelector('main'),commits=[],previews=[],updates=[];
  const track={id:'one',title:'First light',artist:'Artist',album:'Album',source:'C:/one.mp3',artwork:'',duration:60};
  const snapshot={...initialMusicState(),session:1,pageVisible:true,current:track,queue:[track],account:{signedIn:true}};
  const preferences={preferences:defaults,loaded:true,pending:false};
  const controller=mountMusicView(root,{snapshot,viewState,preferences,
    createBackground:()=>({update(){},dispose(){}}),
    onPreferencePreview:patch=>previews.push(patch),onPreferenceCommit:patch=>commits.push(patch),
    createVisualizer:()=>({update:(state,mode)=>updates.push({state,mode}),dispose(){}}),...extra});
  t.after(()=>{controller.dispose();dom.window.close();});
  const find=selector=>{const node=root.querySelector(selector);assert.ok(node,selector);return node;};
  const click=action=>find(`[data-music-action="${action}"]`).click();
  const tab=id=>find(`[data-music-panel-tab="${id}"]`).click();
  const fire=(node,type)=>node.dispatchEvent(new dom.window.Event(type,{bubbles:true}));
  if(!viewState){click('open-player');click('lyrics');}
  return {dom,root,find,click,tab,fire,controller,commits,previews,updates,snapshot,preferences};
}

test('player panel follows the four original tabs and exposes only real settings',{skip:!jsdomPath},t=>{
  const f=fixture(t);
  assert.equal(f.find('[data-music-action="lyrics"]').getAttribute('aria-label'),'音乐设置');
  assert.equal(f.find('.MusicPlayerPanel').getAttribute('aria-label'),'播放器面板');
  assert.deepEqual([...f.root.querySelectorAll('[data-music-panel-tab]')].map(node=>node.dataset.musicPanelTab),
    ['cover','controls','globalSettings','account']);
  assert.equal(f.controller.captureView().panelTab,'globalSettings');
  const picker=f.find('[data-music-visualizer-picker]');
  const trigger=f.find('[data-music-visualizer-trigger]');
  const proxy=f.find('select[data-music-preference="mode"]');
  assert.equal(picker.closest('[data-music-panel-content]').dataset.musicPanelContent,'globalSettings');
  assert.equal(trigger.getAttribute('aria-label'),'歌词样式');
  assert.equal(proxy.getAttribute('aria-hidden'),'true');
  assert.equal(proxy.getAttribute('tabindex'),'-1');
  assert.deepEqual([...picker.querySelectorAll('[data-music-visualizer]')].map(option=>option.dataset.musicVisualizer),
    ['partita','fume','sonnet','gallery','3d','lumiere','cadenza','tilt','cappella','diorama','pendolo','classic','tempera']);
  assert.equal(picker.querySelector('[data-music-visualizer="motif"]'),null,'removed visualizer is not user-visible');
  trigger.click();f.find('[data-music-visualizer="partita"]').click();
  assert.deepEqual(f.commits,[{mode:'partita'}]);
  assert.equal(f.find('[data-music-preference="lyricsFontColor"]').closest('[data-music-panel-content]').dataset.musicPanelContent,'controls');
  assert.equal(f.root.querySelector('.MusicLyricsBody'),null);
  for(const control of f.root.querySelectorAll('[data-music-unavailable]'))assert.ok(control.disabled);
  f.tab('account');assert.match(f.find('[data-music-panel-content="account"]').textContent,/游客/);
  assert.equal(f.root.querySelector('[data-music-action="logout"]'),null,'Lume sign-in is not a provider account');
});

test('Lumiere uses its original colors without changing other style preferences',{skip:!jsdomPath},t=>{
  const f=fixture(t);
  const custom={...defaults,mode:'lumiere',lyricsFontColor:'#ff0000',lyricsKaraokeFillColor:'#00ff00'};
  f.controller.updatePreferences({...f.preferences,preferences:custom});f.tab('controls');
  for(const group of f.root.querySelectorAll('[data-music-color-group]'))assert.equal(group.hidden,true);
  assert.equal(f.find('[data-music-lumiere-colors]').hidden,false);
  assert.deepEqual(f.commits,[],'changing style does not overwrite saved colors');
  f.controller.updatePreferences({...f.preferences,preferences:{...custom,mode:'partita'}});
  assert.equal(f.find('[data-music-color-group="global"]').hidden,false);
  assert.equal(f.find('[data-music-preference="lyricsFontColor"]').value,'#ff0000');
  assert.equal(f.find('[data-music-preference="lyricsKaraokeFillColor"]').value,'#00ff00');
  assert.equal(f.find('[data-music-lumiere-colors]').hidden,true);
});

test('effect selection uses preferences and a separate view owner without remounting lyrics',{skip:!jsdomPath},t=>{
  const updates=[],disposed=[];
  const f=fixture(t,undefined,{createEffects:host=>({
    update:state=>updates.push({host,state}),dispose:()=>disposed.push(host)})});
  const effect=f.find('[data-music-preference="effectMode"]');
  assert.equal(effect.disabled,false);
  assert.deepEqual([...effect.options].map(option=>option.value),['none','rain-window','snow','star-river']);
  const stage=f.find('[data-music-stage]');
  effect.value='snow';f.fire(effect,'change');
  assert.deepEqual(f.commits,[{effectMode:'snow'}]);
  f.controller.updatePreferences({...f.preferences,preferences:{...defaults,effectMode:'snow'}});
  assert.equal(f.find('[data-music-stage]'),stage);
  assert.equal(updates.at(-1).state.mode,'snow');
  assert.equal(updates.at(-1).state.visualizer,'partita');
  f.controller.setVisible(false);assert.equal(updates.at(-1).state.visible,false);
  f.controller.setVisible(true);assert.equal(updates.at(-1).state.visible,true);
  f.click('back-home');assert.equal(disposed.length,1);
  f.click('open-player');
  assert.equal(updates.at(-1).state.mode,'snow');
  f.controller.dispose();assert.equal(disposed.length,2);
});

test('effects use the original anchored quick picker and keep mode selection independent',{skip:!jsdomPath},t=>{
  const f=fixture(t),trigger=f.find('[data-music-effect-trigger]');
  const key=(node,value)=>node.dispatchEvent(new f.dom.window.KeyboardEvent('keydown',{key:value,bubbles:true,cancelable:true}));
  assert.equal(trigger.getAttribute('aria-haspopup'),'listbox');
  assert.equal(f.find('[data-music-preference="effectMode"]').getAttribute('aria-hidden'),'true');
  trigger.click();
  assert.equal(trigger.getAttribute('aria-expanded'),'true');
  assert.deepEqual([...f.root.querySelectorAll('[data-music-effect]')].map(node=>node.textContent),['关闭','雨窗','雪天','星河']);
  key(trigger,'ArrowDown');
  assert.equal(f.dom.window.document.activeElement.dataset.musicEffect,'none');
  key(f.dom.window.document.activeElement,'End');
  assert.equal(f.dom.window.document.activeElement.dataset.musicEffect,'star-river');
  f.find('[data-music-effect="snow"]').click();
  assert.deepEqual(f.commits,[{effectMode:'snow'}]);
  assert.equal(trigger.getAttribute('aria-expanded'),'false');
  f.controller.updatePreferences({...f.preferences,preferences:{...defaults,effectMode:'snow'}});
  assert.equal(trigger.textContent,'雪天');
  assert.equal(f.find('[data-music-effect="snow"]').getAttribute('aria-selected'),'true');
  assert.equal(f.find('[data-music-visualizer-trigger]').textContent,'云阶');
  trigger.click();key(trigger,'Escape');
  assert.equal(trigger.getAttribute('aria-expanded'),'false');
  assert.equal(f.dom.window.document.activeElement,trigger);
  trigger.click();
  f.dom.window.document.body.dispatchEvent(new f.dom.window.Event('pointerdown',{bubbles:true}));
  assert.equal(trigger.getAttribute('aria-expanded'),'false');
  trigger.click();f.find('[data-music-visualizer-trigger]').click();
  assert.equal(trigger.getAttribute('aria-expanded'),'false');
  f.controller.updatePreferences({...f.preferences,loaded:false});
  assert.equal(trigger.disabled,true);
});

test('cover title copies bounded song information without exposing its source path',{skip:!jsdomPath},async t=>{
  const f=fixture(t,undefined,{createVisualizer:()=>({update(){},dispose(){}})});
  f.tab('cover');
  const title=f.find('[data-music-cover-title]');
  const copied=[];
  Object.defineProperty(f.dom.window.navigator,'clipboard',{value:{writeText:async value=>copied.push(value)},configurable:true});
  title.click();
  await new Promise(resolve=>setTimeout(resolve,0));
  assert.deepEqual(copied,['First light - Artist - Album']);
  assert.equal(f.root.textContent.includes('C:/one.mp3'),false);
  f.controller.update({...f.snapshot,current:{...f.snapshot.current,title:'下一首',artist:'',album:''}});
  f.find('[data-music-cover-title]').click();
  await new Promise(resolve=>setTimeout(resolve,0));
  assert.deepEqual(copied,['First light - Artist - Album','下一首 -  - ']);
});

test('cover copy keyboard activation is single-flight and ignores completion after track change or dispose',{skip:!jsdomPath},async t=>{
  const notices=[],copied=[],resolvers=[];
  const f=fixture(t,undefined,{onNotice:value=>notices.push(value)});
  Object.defineProperty(f.dom.window.navigator,'clipboard',{value:{
    writeText:value=>{copied.push(value);return new Promise(resolve=>resolvers.push(resolve));}}});
  f.tab('cover');
  const key=(value,repeat=false)=>f.find('[data-music-cover-title]').dispatchEvent(
    new f.dom.window.KeyboardEvent('keydown',{key:value,repeat,bubbles:true,cancelable:true}));
  const flush=()=>new Promise(resolve=>setTimeout(resolve,0));
  key('Enter');key('Enter',true);key(' ');
  assert.deepEqual(copied,['First light - Artist - Album']);
  resolvers.shift()();await flush();
  assert.deepEqual(notices,['已复制歌曲信息']);
  key(' ');
  assert.equal(copied.length,2);
  f.controller.update({...f.snapshot,current:{...f.snapshot.current,id:'two',source:'C:/two.mp3'}});
  resolvers.shift()();await flush();
  assert.equal(notices.length,1,'old track completion is silent');
  f.find('[data-music-cover-title]').click();
  assert.equal(copied.length,3);
  f.controller.dispose();resolvers.shift()();await flush();
  assert.equal(notices.length,1,'disposed view cannot report');
});

test('failed cover copy reports failure without closing the panel',{skip:!jsdomPath},async t=>{
  const notices=[];
  const f=fixture(t,undefined,{onNotice:value=>notices.push(value)});
  Object.defineProperty(f.dom.window.navigator,'clipboard',{value:{writeText:async()=>{throw new Error('denied');}}});
  f.tab('cover');f.find('[data-music-cover-title]').click();
  await new Promise(resolve=>setTimeout(resolve,0));
  assert.deepEqual(notices,['复制歌曲信息失败']);
  assert.equal(f.controller.captureView().panelTab,'cover');
  assert.ok(f.find('.MusicPlayerPanel'));
});

for(const finish of ['dispose','track-change','back-home','native-hidden','page-hidden','document-hidden',
  'panel-close','panel-tab','panel-tab-keyboard']) {
  test(`pending clipboard denial cannot start a fallback after ${finish}`,{skip:!jsdomPath},async t=>{
    const notices=[],f=fixture(t,undefined,{onNotice:value=>notices.push(value)});
    let reject,fallbacks=0;
    Object.defineProperty(f.dom.window.navigator,'clipboard',{value:{
      writeText:()=>new Promise((resolve,fail)=>{reject=fail;})}});
    f.dom.window.document.execCommand=()=>{fallbacks++;return true;};
    f.tab('cover');f.find('[data-music-cover-title]').click();
    if(finish==='dispose')f.controller.dispose();
    else if(finish==='track-change')f.controller.update({...f.snapshot,current:{...f.snapshot.current,id:'new'}});
    else if(finish==='native-hidden')f.controller.setVisible(false);
    else if(finish==='page-hidden')f.controller.update({...f.snapshot,pageVisible:false});
    else if(finish==='document-hidden') {
      Object.defineProperty(f.dom.window.document,'hidden',{value:true,configurable:true});
      f.dom.window.document.dispatchEvent(new f.dom.window.Event('visibilitychange'));
    } else if(finish==='panel-close')f.click('close-panel');
    else if(finish==='panel-tab')f.tab('controls');
    else if(finish==='panel-tab-keyboard')f.find('[data-music-panel-tab="cover"]').dispatchEvent(
      new f.dom.window.KeyboardEvent('keydown',{key:'ArrowRight',bubbles:true,cancelable:true}));
    else f.click('back-home');
    reject(new Error('denied'));
    await new Promise(resolve=>setTimeout(resolve,0));
    assert.equal(fallbacks,0);
    assert.deepEqual(notices,[]);
  });
}

test('panel tabs, snapshots and preference preview retain connected input and stage nodes',{skip:!jsdomPath},t=>{
  const f=fixture(t),panel=f.find('.MusicPlayerPanel'),stage=f.find('[data-music-stage]');
  const proxy=f.find('[data-music-preference="mode"]'),color=f.find('[data-music-preference="lyricsFontColor"]');
  const observer=new f.dom.window.MutationObserver(()=>{});observer.observe(f.root,{childList:true,subtree:true});
  f.tab('controls');color.focus();color.value='#ff2200';f.fire(color,'input');
  assert.deepEqual(f.previews,[{lyricsFontColor:'#ff2200'}]);assert.equal(f.commits.length,0);
  f.controller.updatePreferences({...f.preferences,preferences:{...defaults,lyricsFontColor:'#ff2200'}});
  f.fire(color,'change');assert.deepEqual(f.commits,[{lyricsFontColor:'#ff2200'}]);
  f.controller.update({...f.snapshot,current:{...f.snapshot.current,title:'Next title'}});
  assert.equal(f.dom.window.document.activeElement,color);
  f.tab('globalSettings');f.tab('controls');
  assert.equal(f.find('.MusicPlayerPanel'),panel);assert.equal(f.find('[data-music-stage]'),stage);
  assert.equal(f.find('[data-music-preference="mode"]'),proxy);assert.equal(f.find('[data-music-preference="lyricsFontColor"]'),color);
  assert.equal(observer.takeRecords().some(record=>[...record.removedNodes].some(node=>node===panel||node.contains?.(color)||node.contains?.(stage))),false);
  observer.disconnect();
  const saved=f.controller.captureView();assert.equal(saved.panelTab,'controls');
  const restored=fixture(t,saved);assert.equal(restored.find('[data-music-panel-tab="controls"]').getAttribute('aria-pressed'),'true');
  f.find('[data-music-panel-tab="controls"]').dispatchEvent(new f.dom.window.KeyboardEvent('keydown',{key:'Escape',bubbles:true}));
  assert.equal(f.root.querySelector('.MusicPlayerPanel'),null);
  assert.equal(f.dom.window.document.activeElement,f.find('[data-music-action="lyrics"]'));
  f.click('lyrics');f.find('.MusicPanelDismiss').click();
  assert.equal(f.root.querySelector('.MusicPlayerPanel'),null);
  assert.equal(f.dom.window.document.activeElement,f.find('[data-music-action="lyrics"]'));
});

test('panel tabs are keyboard reachable and outside dismissal restores the settings opener',{skip:!jsdomPath},t=>{
  const f=fixture(t,undefined,{createVisualizer:()=>({update(){},dispose(){}})});
  const opener=f.find('[data-music-action="lyrics"]');
  const panel=f.find('.MusicPlayerPanel');
  assert.equal(panel.dataset.panelTab,'globalSettings');

  const tabs=[...f.root.querySelectorAll('[data-music-panel-tab]')];
  assert.deepEqual(tabs.map(tab=>tab.dataset.musicPanelTab),['cover','controls','globalSettings','account']);
  const activeTab=()=>f.root.querySelector('[data-music-panel-tab][aria-pressed="true"]');
  const key=(node,key)=>node.dispatchEvent(new f.dom.window.KeyboardEvent('keydown',{key,bubbles:true,cancelable:true}));

  tabs[1].focus();
  key(tabs[1],'ArrowRight');
  assert.equal(activeTab().dataset.musicPanelTab,'globalSettings');
  assert.equal(f.dom.window.document.activeElement?.dataset.musicPanelTab,'globalSettings');
  key(activeTab(),'Home');
  assert.equal(activeTab().dataset.musicPanelTab,'cover');
  key(activeTab(),'End');
  assert.equal(activeTab().dataset.musicPanelTab,'account');

  f.find('.MusicPanelDismiss').click();
  assert.equal(f.root.querySelector('.MusicPlayerPanel'),null);
  assert.equal(f.dom.window.document.activeElement?.dataset.musicAction,'lyrics');

  f.find('[data-music-action="lyrics"]').click();
  assert.equal(f.find('.MusicPlayerPanel').dataset.panelTab,'account');
  assert.equal(f.find('[data-music-panel-content="account"]').hidden,false);
});

test('visualizer picker preserves all modes and commits through the existing preference boundary',{skip:!jsdomPath},t=>{
  const f=fixture(t,undefined,{createVisualizer:()=>({update(){},dispose(){}})});
  const picker=f.find('[data-music-visualizer-picker]');
  const trigger=f.find('[data-music-visualizer-trigger]');
  const ids=['partita','fume','sonnet','gallery','3d','lumiere','cadenza','tilt','cappella','diorama','pendolo','classic','tempera'];

  assert.equal(trigger.getAttribute('aria-haspopup'),'listbox');
  assert.equal(trigger.getAttribute('aria-expanded'),'false');
  trigger.click();
  assert.equal(trigger.getAttribute('aria-expanded'),'true');
  assert.deepEqual([...picker.querySelectorAll('[data-music-visualizer]')].map(node=>node.dataset.musicVisualizer),ids);
  assert.equal(picker.querySelector('[data-music-visualizer="motif"]'),null,'removed visualizer is not user-visible');
  assert.equal(picker.querySelector('[data-music-visualizer="partita"]').getAttribute('aria-selected'),'true');

  f.find('[data-music-visualizer="gallery"]').click();
  assert.deepEqual(f.commits,[{mode:'gallery'}]);
  assert.equal(trigger.getAttribute('aria-expanded'),'false');

  trigger.click();
  trigger.dispatchEvent(new f.dom.window.KeyboardEvent('keydown',{key:'Escape',bubbles:true}));
  assert.equal(trigger.getAttribute('aria-expanded'),'false');
  trigger.click();
  f.dom.window.document.body.dispatchEvent(new f.dom.window.Event('pointerdown',{bubbles:true}));
  assert.equal(trigger.getAttribute('aria-expanded'),'false');
});

test('Sonnet controls expose three original colors without changing global controls or stage identity',{skip:!jsdomPath},t=>{
  const f=fixture(t),stage=f.find('[data-music-stage]');
  const preferences={...defaults,mode:'sonnet',sonnetColors:{
    lyricsFontColor:'#ffffff',karaokeFillColor:'#38bdf8',glowColor:'#60a5fa'}};
  f.controller.updatePreferences({...f.preferences,preferences});
  f.tab('controls');
  assert.deepEqual([...f.root.querySelectorAll('[data-music-sonnet-color]')].map(node=>node.dataset.musicSonnetColor),
    ['lyricsFontColor','karaokeFillColor','glowColor']);
  const color=f.find('[data-music-sonnet-color="karaokeFillColor"]');
  assert.equal(color.closest('[data-music-color-group]').hidden,false);
  assert.equal(f.find('[data-music-preference="lyricsFontColor"]').closest('[data-music-color-group]').hidden,true);
  color.focus();color.value='#ff2200';f.fire(color,'input');
  assert.deepEqual(f.previews,[{sonnetColors:{karaokeFillColor:'#ff2200'}}]);
  assert.equal(f.commits.length,0);
  f.controller.updatePreferences({...f.preferences,preferences:{...preferences,
    sonnetColors:{...preferences.sonnetColors,karaokeFillColor:'#ff2200'}}});
  f.fire(color,'change');
  assert.deepEqual(f.commits,[{sonnetColors:{karaokeFillColor:'#ff2200'}}]);
  f.find('[data-music-sonnet-reset="glowColor"]').click();
  assert.deepEqual(f.commits.at(-1),{sonnetColors:{glowColor:'#ffffff'}});
  assert.equal(f.find('[data-music-stage]'),stage);
  assert.equal(f.find('[data-music-sonnet-color="karaokeFillColor"]'),color);
  f.controller.updatePreferences(f.preferences);
  assert.equal(f.find('[data-music-preference="lyricsFontColor"]').closest('[data-music-color-group]').hidden,false);
  assert.equal(color.closest('[data-music-color-group]').hidden,true);
  assert.equal(f.find('[data-music-preference="lyricsFontColor"]').value,'#ffffff');
});

test('preference acknowledgements and panel redraws never rewind playing lyrics',{skip:!jsdomPath},async t=>{
  let now=0,id=0;
  const frames=new Map(),paint=[];
  const f=fixture(t,undefined,{createVisualizer:host=>{
    const win=host.ownerDocument.defaultView;
    Object.defineProperty(win.performance,'now',{configurable:true,value:()=>now});
    win.requestAnimationFrame=fn=>{frames.set(++id,fn);return id;};
    win.cancelAnimationFrame=key=>frames.delete(key);
    return mountMusicVisualizer(host,{load:async()=>()=>({update:value=>paint.push(value),dispose(){}})});
  }});
  const flush=async()=>{for(let i=0;i<8;i++)await Promise.resolve();};
  const snapshot={...f.snapshot,playing:true,status:'playing',timeline:{position:4.8,duration:60},
    lyrics:[0,5,10].map(startTime=>({startTime,endTime:startTime+5,fullText:'A line',words:[]}))};
  f.controller.update(snapshot);await flush();
  now=500;
  const callbacks=[...frames.values()];frames.clear();callbacks.forEach(fn=>fn(now));
  assert.equal(paint.at(-1).currentTime,5.3);
  const renders=paint.length;
  f.controller.updatePreferences({...f.preferences,preferences:structuredClone(defaults),pending:true});
  assert.equal(f.find('[data-music-settings]').getAttribute('aria-busy'),'true');
  assert.equal(paint.length,renders,'equal nested preferences only update control status');
  f.controller.updatePreferences({...f.preferences,preferences:{...defaults,lyricsFontColor:'#ff2200'}});
  assert.equal(paint.at(-1).currentTime,5.3,'changed preferences preserve the projected clock');
  assert.equal(paint.at(-1).activeLineIndex,1);
  f.click('close-panel');f.click('lyrics');
  assert.equal(paint.at(-1).currentTime,5.3,'panel chrome cannot rewind lyrics');
  f.find('[data-music-visualizer-trigger]').click();f.find('[data-music-visualizer="sonnet"]').click();
  await flush();
  assert.equal(paint.at(-1).currentTime,5.3,'mode switching keeps the same native clock anchor');
  f.controller.update({...snapshot,account:{signedIn:false}},{syncClock:false});
  assert.equal(paint.at(-1).currentTime,5.3,'account projection cannot reset the playback clock');
  f.controller.update({...snapshot,lyrics:[...snapshot.lyrics,
    {startTime:15,endTime:20,fullText:'Late lyric',words:[]}]},{syncClock:false});
  assert.equal(paint.at(-1).currentTime,5.3,'late lyric content cannot reset the playback clock');
  assert.equal(paint.at(-1).lines.length,4);
  f.controller.update({...snapshot,timeline:{position:2,duration:60}});
  assert.equal(paint.at(-1).currentTime,2,'an actual native seek remains authoritative');
});
