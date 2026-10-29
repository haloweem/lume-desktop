import nodeTest from "node:test";
import assert from "node:assert/strict";
import {existsSync, readFileSync} from "node:fs";
import {createRequire} from "node:module";
import {initialMusicState} from "../music.js";
import {DEFAULT_MUSIC_PREFERENCES} from '../music-preferences.js';
import {rendererModule} from "./music-view-dom.mjs";

const jsdomPath = process.env.LUME_JSDOM_PATH;
const {JSDOM} = jsdomPath ? createRequire(import.meta.url)(jsdomPath) : {};
const test = (name, run) => nodeTest(name, {skip:!jsdomPath}, run);
const controllerUrl = new URL("../music-view.js", import.meta.url);
const rendererUrl = `data:text/javascript;base64,${Buffer.from(rendererModule).toString("base64")}`;
const renderer = await import(rendererUrl);

// Only the concurrently implemented renderer is substituted. The controller,
// native snapshot normalization and command validation run unchanged.
async function mountFunction() {
  assert.ok(existsSync(controllerUrl), "music-view.js must implement the controller");
  const source = readFileSync(controllerUrl, "utf8").replaceAll('"./music.js"', JSON.stringify(rendererUrl))
    .replaceAll("'./music.js'", JSON.stringify(rendererUrl))
    .replaceAll('"./music-visualizer.js"', JSON.stringify(new URL('../music-visualizer.js',import.meta.url).href))
    .replaceAll("'./music-collections.js'", JSON.stringify(new URL('../music-collections.js',import.meta.url).href))
    .replaceAll("'./music-preferences.js'", JSON.stringify(new URL('../music-preferences.js',import.meta.url).href))
    .replaceAll("'./music-preference-values.js'", JSON.stringify(new URL('../music-preference-values.js',import.meta.url).href))
    .replaceAll("'./music-settings.js'", JSON.stringify(new URL('../music-settings.js',import.meta.url).href))
    .replaceAll("'./music-player-panel.js'", JSON.stringify(new URL('../music-player-panel.js',import.meta.url).href))
    .replaceAll("'./music-background-view.js'", JSON.stringify(new URL('../music-background-view.js',import.meta.url).href))
    .replaceAll("'./music-effects.js'", JSON.stringify(new URL('../music-effects.js',import.meta.url).href))
    .replaceAll("'./music-cover-actions.js'", JSON.stringify(new URL('../music-cover-actions.js',import.meta.url).href))
    .replaceAll("'./music-provider-account.js'", JSON.stringify(new URL('../music-provider-account.js',import.meta.url).href))
    .replaceAll("'./music-wallpaper-gallery.js'", JSON.stringify(new URL('../music-wallpaper-gallery.js',import.meta.url).href))
    .replaceAll("'./music-online.js'", JSON.stringify(new URL('../music-online.js',import.meta.url).href))
    .replaceAll("'./music-playlist-selection.js'", JSON.stringify(new URL('../music-playlist-selection.js',import.meta.url).href));
  const module = await import(`data:text/javascript;base64,${Buffer.from(source).toString("base64")}`);
  assert.equal(typeof module.mountMusicView, "function");
  return module.mountMusicView;
}

const tracks = [
  {id:"one", title:"First Light", artist:"M83", album:"Morning", source:"C:/one.mp3", artwork:"", duration:120},
  {id:"two", title:"Second", artist:"Other", album:"Night", source:"C:/two.mp3", artwork:"", duration:90},
];
const nativeState = (fields = {}) => ({...initialMusicState(), session:7, revision:1, status:"paused",
  current:tracks[0], queue:tracks, currentIndex:0, timeline:{position:20, duration:120},
  capabilities:{canToggle:true, canPrevious:true, canNext:true, canSeek:true}, ...fields});

async function setup(t, state = nativeState(), production = false, extraOptions = {}) {
  const mount = production ? (await import(controllerUrl.href)).mountMusicView : await mountFunction();
  const dom = new JSDOM('<main id="host"></main><button id="outside" data-music-action="toggle">Outside</button>',
    {url:"https://lume.local/", pretendToBeVisual:true});
  const {document} = dom.window, root = document.querySelector("main");
  const sent = [], imported = [], notices = [], commits = [];
  let accepted = true, importAccepted = true;
  const options = {snapshot:state, createBackground:()=>({update(){},dispose(){}}),
    preferences:{preferences:DEFAULT_MUSIC_PREFERENCES,loaded:true,pending:false},
    send:(action, fields) => {sent.push([action, fields]); return accepted;},
    onPreferenceCommit:patch => commits.push(patch),
    onImport:files => {imported.push(files); return importAccepted;}, onNotice:message => notices.push(message),...extraOptions};
  const controller = mount(root, options);
  t.after(() => {controller.dispose(); dom.window.close();});
  const find = selector => {const element = root.querySelector(selector); assert.ok(element, selector); return element;};
  const fire = (element, type, fields = {}) => {
    const event = type.startsWith("key") ? new dom.window.KeyboardEvent(type, {bubbles:true, cancelable:true, ...fields})
      : new dom.window.Event(type, {bubbles:true, cancelable:true, ...fields});
    element.dispatchEvent(event);
    return event;
  };
  const click = action => find(`[data-music-action="${action}"]`).click();
  const input = (selector, value) => {const element=find(selector); element.value=value; fire(element,"input");};
  return {mount,dom,document,root,controller,options,sent,imported,notices,commits,find,fire,click,input,
    reject:() => {accepted=false;}, rejectImport:() => {importAccepted=false;}};
}

test("mount exposes independent local view state and retains the native product account", async t => {
  const f = await setup(t, nativeState({account:{signedIn:true}}));
  assert.deepEqual(f.controller.snapshot().view, {nav:"recommendations", screen:"home", panel:"", query:"",
    search:"", listQuery:"", listSearchOpen:false, playlistGroup:"owned",visualizer:"partita",panelTab:'globalSettings'});
  assert.equal(f.controller.snapshot().account.signedIn, true);
  assert.deepEqual(f.sent, [], "mount does not invent an online request or a native lifecycle owner");
  const copy = f.controller.snapshot();
  copy.view.nav = "albums";
  copy.queue[0].title = "mutated";
  assert.equal(f.controller.snapshot().view.nav, "recommendations");
  assert.equal(f.controller.snapshot().queue[0].title, "First Light");
});

test('material is player-only, retains its surface across controls and releases on home/dispose',async t=>{
  const owners=[];
  const f=await setup(t,nativeState({pageVisible:true}),true,{
    createVisualizer:()=>({update(){},dispose(){}}),
    createMaterial:(surface,options)=>{
      const owner={surface,options,updates:[],disposed:0,
        update(value){this.updates.push(value);},dispose(){this.disposed++;}};
      owners.push(owner);return owner;
    },
  });
  const settle=()=>new Promise(resolve=>setImmediate(resolve));
  await settle();assert.equal(owners.length,0);
  f.click('open-player');await settle();assert.equal(owners.length,1);
  const footer=f.find('.MusicPlayerBar'),stage=f.find('[data-music-stage]');
  f.click('lyrics');f.click('close-panel');
  f.controller.update(nativeState({pageVisible:true,current:{...tracks[0],title:'Changed'}}));
  await settle();
  assert.equal(f.find('.MusicPlayerBar'),footer);assert.equal(f.find('[data-music-stage]'),stage);
  assert.equal(owners.length,1);
  f.controller.setVisible(false);assert.equal(owners[0].updates.at(-1).visible,false);
  f.controller.setVisible(true);assert.equal(owners[0].updates.at(-1).visible,true);
  f.click('back-home');assert.equal(owners[0].disposed,1);
  f.click('open-player');await settle();assert.equal(owners.length,2);
  f.controller.dispose();assert.equal(owners[1].disposed,1);
});

test('late material load cannot attach a renderer after leaving player',async t=>{
  let finish,disposed=0;
  const f=await setup(t,nativeState({pageVisible:true}),true,{
    createVisualizer:()=>({update(){},dispose(){}}),
    createMaterial:()=>new Promise(resolve=>{finish=resolve;}),
  });
  f.click('open-player');await new Promise(resolve=>setImmediate(resolve));
  f.click('back-home');
  finish({dispose(){disposed++;}});
  await new Promise(resolve=>setImmediate(resolve));
  assert.equal(disposed,1);assert.equal(f.root.querySelector('.MusicPlayerBar'),null);
});

test('quality menu opens above the glass, closes on empty-space input and keeps the player alive',async t=>{
  const f=await setup(t,nativeState({pageVisible:true,current:{...tracks[0],id:'qq:track'}}),true,{
    createVisualizer:()=>({update(){},dispose(){}}),
  });
  f.click('open-player');
  const bar=f.find('.MusicPlayerBar');
  const trigger=f.find('[data-music-quality-trigger]');
  assert.equal(trigger.disabled,false);
  trigger.click();
  const menu=f.find('[data-music-quality-menu]');
  const backdrop=f.find('[data-music-quality-dismiss]');
  assert.equal(menu.hidden,false);
  assert.equal(trigger.getAttribute('aria-expanded'),'true');
  assert.equal(menu.closest('.MusicPlayerBar'),bar);
  assert.ok(Number.parseFloat(menu.style.bottom)>0);
  assert.ok(Number.parseFloat(menu.style.maxHeight)>0);
  backdrop.dispatchEvent(new f.dom.window.MouseEvent('pointerdown',{bubbles:true,button:0}));
  assert.equal(menu.hidden,true);
  assert.equal(trigger.getAttribute('aria-expanded'),'false');
  assert.equal(f.find('.MusicPlayerBar'),bar);
  assert.equal(f.sent.length,0);
});

test('production quality state refreshes pending, rollback and success for every provider',async t=>{
  for(const provider of ['qq','netease','kugou']) {
    const current={...tracks[0],id:`${provider}:track`,source:`online:${provider}:track`};
    const base=nativeState({pageVisible:true,current,audio:{requestedQuality:'high',quality:'high',bitrate:320000,switching:false}});
    const f=await setup(t,base,true,{createVisualizer:()=>({update(){},dispose(){}})});
    f.click('open-player');
    const stage=f.find('[data-music-stage]');
    const update=audio=>f.controller.update({...base,audio});
    update({requestedQuality:'lossless',quality:'high',bitrate:320000,switching:true});
    assert.equal(f.find('[data-music-quality-trigger]').getAttribute('aria-busy'),'true');
    assert.match(f.find('[data-music-quality-trigger]').textContent,/切换中/);
    assert.equal(f.find('[data-music-quality-option="high"]').getAttribute('aria-checked'),'true');
    update({requestedQuality:'lossless',quality:'high',bitrate:320000,switching:false});
    assert.equal(f.find('[data-music-quality-trigger]').getAttribute('aria-busy'),'false');
    assert.match(f.find('[data-music-quality-trigger]').title,/暂不可用/);
    update({requestedQuality:'lossless',quality:'lossless',bitrate:900000,switching:false});
    assert.equal(f.find('[data-music-quality-option="lossless"]').getAttribute('aria-checked'),'true');
    assert.doesNotMatch(f.find('[data-music-quality-trigger]').title,/暂不可用|正在切换/);
    assert.equal(f.find('[data-music-stage]'),stage);
  }
});

test('volume opens on click without muting, excludes quality and closes outside or on Escape',async t=>{
  const f=await setup(t,nativeState({pageVisible:true,current:{...tracks[0],id:'qq:track'}}),true,{
    createVisualizer:()=>({update(){},dispose(){}}),
  });
  f.click('open-player');
  const trigger=f.find('[data-music-volume-trigger]'),menu=f.find('[data-music-volume-menu]');
  assert.equal(menu.hidden,true);
  trigger.click();assert.equal(menu.hidden,false);assert.equal(f.sent.length,0);
  trigger.click();assert.equal(menu.hidden,true);
  trigger.click();f.find('[data-music-quality-trigger]').click();assert.equal(menu.hidden,true);
  trigger.click();assert.equal(f.find('[data-music-quality-menu]').hidden,true);
  f.fire(f.find('[data-music-volume-dismiss]'),'pointerdown');assert.equal(menu.hidden,true);
  trigger.click();f.fire(f.find('[data-music-volume]'),'keydown',{key:'Escape'});
  assert.equal(menu.hidden,true);assert.equal(f.document.activeElement,trigger);
  trigger.click();f.input('[data-music-volume]','0.4');f.fire(f.find('[data-music-volume]'),'change');
  assert.equal(f.sent.at(-1)[0],'setVolume');assert.equal(f.sent.at(-1)[1].value,0.4);
  f.controller.update(nativeState({pageVisible:true,muted:true,current:{...tracks[0],id:'qq:track'}}));
  f.input('[data-music-volume]','0.5');f.fire(f.find('[data-music-volume]'),'change');
  assert.equal(f.sent.at(-1)[0],'setMuted');assert.equal(f.sent.at(-1)[1].value,false);
  f.controller.setVisible(false);assert.equal(menu.hidden,true);
  f.click('lyrics');
  for(const mode of ['lumiere','classic','tempera'])
    assert.ok(f.root.querySelector(`option[value="${mode}"]`),`${mode} is offered`);
});

test('immersive mode retains the mounted material owner while controls are hidden',async t=>{
  const owners=[],immersiveReports=[];
  const f=await setup(t,nativeState({pageVisible:true}),true,{
    onImmersiveChange:value=>immersiveReports.push(value),
    createVisualizer:()=>({update(){},dispose(){}}),
    createMaterial:(surface,options)=>{const owner={surface,options,updates:[],disposed:0,
      update(value){this.updates.push(value);},dispose(){this.disposed++;}};owners.push(owner);return owner;},
  });
  const settle=()=>new Promise(resolve=>setImmediate(resolve));
  f.click('open-player');await settle();
  const bar=f.find('.MusicPlayerBar'),owner=owners[0];
  f.click('immersive');
  assert.equal(f.root.querySelector('.MusicRoot').classList.contains('is-immersive'),true);
  assert.equal(f.root.querySelector('.MusicRoot').classList.contains('is-immersive-revealed'),false);
  assert.equal(owner.disposed,0);
  assert.equal(owners.length,1);
  assert.equal(owner.updates.at(-1).visible,true);
  bar.dispatchEvent(new f.dom.window.MouseEvent('pointerout',{bubbles:true,relatedTarget:null}));
  f.find('[data-music-immersive-zone]').dispatchEvent(new f.dom.window.MouseEvent('pointerover',{bubbles:true,relatedTarget:null}));
  assert.equal(f.root.querySelector('.MusicRoot').classList.contains('is-immersive-revealed'),true);
  // The settings button comes up with the bar and moving onto it keeps both up.
  const toggle=f.find('[data-music-settings-toggle]');
  assert.equal(toggle.hidden,false);
  f.find('[data-music-immersive-zone]').dispatchEvent(new f.dom.window.MouseEvent('pointerout',{bubbles:true,relatedTarget:toggle}));
  assert.equal(f.root.querySelector('.MusicRoot').classList.contains('is-immersive-revealed'),true);
  toggle.dispatchEvent(new f.dom.window.MouseEvent('pointerout',{bubbles:true,relatedTarget:null}));
  assert.equal(f.root.querySelector('.MusicRoot').classList.contains('is-immersive-revealed'),false);
  assert.equal(toggle.hidden,true);
  assert.deepEqual(immersiveReports,[true],'the page is told once that the window bar can tuck away');
  assert.equal(f.find('.MusicPlayerBar'),bar);
  assert.equal(owner.disposed,0);
  assert.equal(owners.length,1);
  f.controller.dispose();assert.equal(owner.disposed,1);
});

test("captureView restores only bounded display state and scroll without copying playback data", async t => {
  const f=await setup(t,nativeState(),true,{createVisualizer:()=>({update(){},dispose(){}})});
  f.find('[data-music-nav="local"]').click();
  f.input('[data-music-search]','draft title');
  f.fire(f.find('[data-music-search-form]'),'submit');
  f.find('[data-music-home-content]').scrollTop=246;
  f.click('open-player');
  f.click('queue');
  f.find('.MusicPanelBody').scrollTop=137;
  f.click('close-panel');
  f.click('lyrics');
  f.find('[data-music-visualizer-trigger]').click();
  f.find('[data-music-visualizer="gallery"]').click();
  assert.deepEqual(f.commits,[{mode:'gallery'}]);
  f.controller.updatePreferences({...f.options.preferences,
    preferences:{...f.options.preferences.preferences,mode:'gallery'}});
  f.click('close-panel');
  f.click('queue');
  f.find('.MusicPanelBody').scrollTop=137;
  const saved=f.controller.captureView();
  assert.equal(saved.screen,'player');
  assert.equal(saved.panel,'queue');
  assert.equal(saved.nav,'local');
  assert.equal(saved.search,'draft title');
  assert.ok(saved.scrollPositions.length>0);
  assert.ok(saved.scrollPositions.some(([,top])=>top===137),JSON.stringify(saved.scrollPositions));
  assert.equal(Object.hasOwn(saved,'queue'),false);
  assert.equal(Object.hasOwn(saved,'lyrics'),false);
  f.controller.dispose();
  const dom=new JSDOM('<main></main>',{url:'https://lume.local/',pretendToBeVisual:true});
  const root=dom.window.document.querySelector('main');
  const controller=f.mount(root,{snapshot:nativeState({session:8,current:tracks[1],lyrics:[]}),viewState:saved,
    createBackground:()=>({update(){},dispose(){}}),
    createVisualizer:()=>({update(){},dispose(){}})});
  assert.equal(root.querySelector('[data-music-root]').dataset.screen,'player');
  assert.ok(root.querySelector('[data-music-panel="queue"]'));
  assert.equal(root.querySelector('[data-music-panel="queue"] .MusicPanelBody').scrollTop,137,
    JSON.stringify(saved.scrollPositions.filter(([,top])=>top===137)));
  assert.equal(root.querySelector('[data-music-progress]').value,'20');
  assert.match(root.querySelector('[data-music-panel]').textContent,/歌单 \/ 队列/);
  assert.equal(controller.snapshot().session,8);
  assert.equal(controller.snapshot().view.visualizer,'gallery');
  assert.equal(controller.captureView().search,'draft title');
  root.querySelector('[data-music-action="close-panel"]').click();
  root.querySelector('[data-music-action="lyrics"]').click();
  assert.equal(root.querySelector('[data-music-visualizer="gallery"]').getAttribute('aria-selected'),'true');
  assert.equal(root.querySelector('[data-music-preference="mode"]').getAttribute('aria-hidden'),'true');
  root.querySelector('[data-music-action="back-home"]').click();
  assert.equal(root.querySelector('[data-music-home-content]').scrollTop,246);
  assert.equal(root.querySelector('[data-music-search]').value,'draft title');
  controller.dispose();dom.window.close();
});

test("navigation, panels and player return preserve home search and scroll", async t => {
  const f = await setup(t);
  f.find('[data-music-nav="local"]').click();
  f.input("[data-music-search]", " M83 ");
  assert.equal(f.controller.snapshot().view.search, "");
  f.fire(f.find("[data-music-search-form]"), "submit");
  assert.equal(f.controller.snapshot().view.search, "M83");
  f.find("[data-music-home-content]").scrollTop = 173;
  f.click("queue");
  assert.equal(f.controller.snapshot().view.panel, "queue");
  f.click("close-panel");
  assert.equal(f.find("[data-music-home-content]").scrollTop, 173);
  f.click("open-player");
  assert.equal(f.controller.snapshot().view.screen, "player");
  f.click("lyrics");
  assert.equal(f.controller.snapshot().view.panel, "lyrics");
  assert.equal(f.find("[data-music-panel=lyrics]").querySelector(".is-active"), null);
  assert.equal(f.sent.length, 0, "empty lyrics do not call an unconnected provider");
  f.click("back-home");
  assert.equal(f.controller.snapshot().view.nav, "local");
  assert.equal(f.controller.snapshot().view.panel, "");
  assert.equal(f.controller.snapshot().view.search, "M83");
  assert.equal(f.find("[data-music-home-content]").scrollTop, 173);
  f.click("clear-search");
  assert.equal(f.controller.snapshot().view.query, "");
  assert.equal(f.controller.snapshot().view.search, "");
});

test("list filtering preserves caret, focus, original queue indices and draft through snapshots", async t => {
  const f = await setup(t);
  f.find('[data-music-nav="local"]').click();
  f.click("list-search");
  let field = f.find("[data-music-list-search]");
  assert.equal(f.document.activeElement, field);
  field.value="night";
  field.setSelectionRange(2, 4);
  f.fire(field, "input");
  field = f.find("[data-music-list-search]");
  assert.equal(f.document.activeElement, field);
  assert.deepEqual([field.selectionStart,field.selectionEnd], [2,4]);
  assert.equal(f.controller.snapshot().view.listQuery, "night");
  assert.equal(f.root.querySelectorAll('[data-music-action="play-now"]').length, 1);
  f.click("play-now");
  assert.deepEqual(f.sent.at(-1), ["playNow", {index:1, id:'two', session:7}]);
  f.controller.update(nativeState({error:"playback-failed", revision:2}));
  assert.equal(f.find("[data-music-list-search]").value, "night");
  f.click("close-list-search");
  assert.equal(f.controller.snapshot().view.listSearchOpen, false);
  assert.equal(f.controller.snapshot().view.listQuery, "");
});

test("all tabs and playlist groups are local state, never native provider commands", async t => {
  const f = await setup(t);
  for (const nav of ["albums","favorites","recent","local","playlists","recommendations"]) {
    f.find(`[data-music-nav="${nav}"]`).click();
    assert.equal(f.controller.snapshot().view.nav, nav);
  }
  f.find('[data-music-playlist-group="subscribed"]').click();
  assert.equal(f.controller.snapshot().view.playlistGroup, "subscribed");
  f.find('[data-music-playlist-group="owned"]').click();
  assert.equal(f.controller.snapshot().view.playlistGroup, "owned");
  assert.equal(f.sent.length, 0);
});

test("playback-only updates preserve DOM, scroll and focus while calling the playback updater", async t => {
  const f = await setup(t);
  const tree = f.root.firstElementChild, search = f.find("[data-music-search]");
  search.focus();
  f.find("[data-music-home-content]").scrollTop=91;
  const renders=renderer.renderCount, updates=renderer.playbackCount;
  for (let i=2; i<45; i++) f.controller.update(nativeState({revision:i, playing:i%2===0,
    status:i%2===0?"playing":"paused", timeline:{position:i,duration:120}}));
  assert.equal(f.root.firstElementChild, tree);
  assert.equal(f.document.activeElement, search);
  assert.equal(f.find("[data-music-home-content]").scrollTop, 91);
  assert.equal(renderer.renderCount, renders);
  assert.equal(renderer.playbackCount, updates+43);
  assert.equal(f.find("[data-music-progress]").value, "44");
});

test("metadata, current track, queue, capabilities and errors rebuild structure without losing drafts", async t => {
  const f = await setup(t);
  f.input("[data-music-search]", "unsent draft");
  let current=nativeState();
  const changes=[{current:tracks[1],currentIndex:1},
    {queue:[tracks[0],{...tracks[1],title:"Changed"}]},
    {capabilities:{...current.capabilities,canNext:false}}, {error:"load-failed"}];
  for (const fields of changes) {
    const search=f.find("[data-music-search]"); search.focus(); search.setSelectionRange(3,5);
    f.find("[data-music-home-content]").scrollTop=61;
    const tree=f.root.firstElementChild;
    current={...current,...fields,revision:current.revision+1}; f.controller.update(current);
    assert.notEqual(f.root.firstElementChild,tree);
    assert.equal(f.find("[data-music-search]").value,"unsent draft");
    assert.equal(f.document.activeElement,f.find("[data-music-search]"));
    assert.deepEqual([f.document.activeElement.selectionStart,f.document.activeElement.selectionEnd],[3,5]);
    assert.equal(f.find("[data-music-home-content]").scrollTop,61);
  }
});

test("seek owns the initial session, preserves thumb during updates and rejects changed tracks", async t => {
  const f = await setup(t);
  let range=f.find("[data-music-progress]");
  f.fire(range,"pointerdown"); range.value="70"; f.fire(range,"input");
  f.controller.update(nativeState({revision:2,timeline:{position:21,duration:120}}));
  assert.equal(f.find("[data-music-progress]"),range);
  assert.equal(range.value,"70");
  assert.equal(renderer.lastSeeking,true);
  f.fire(range,"change");
  assert.deepEqual(f.sent.at(-1),["seek",{position:70,session:7}]);
  for (const fields of [{session:8}, {current:tracks[1],currentIndex:1}]) {
    f.controller.update(nativeState()); range=f.find("[data-music-progress]");
    f.fire(range,"pointerdown"); range.value="80"; f.fire(range,"input");
    const count=f.sent.length;
    f.controller.update(nativeState({...fields,revision:3}));
    f.fire(f.find("[data-music-progress]"),"change");
    assert.equal(f.sent.length,count,"a stale drag must not seek the new resource");
  }
});

test("cancelled progress gestures never commit on a later change", async t => {
  const f = await setup(t);
  const range=f.find("[data-music-progress]");
  f.fire(range,"pointerdown"); range.value="60"; f.fire(range,"input");
  f.fire(range,"pointercancel"); f.fire(range,"change");
  assert.equal(f.sent.length,0);
  f.fire(range,"pointerdown"); range.value="50"; f.fire(range,"input"); f.fire(range,"change");
  assert.deepEqual(f.sent.at(-1),["seek",{position:50,session:7}]);
});

test("playback mode cycles through current-list shuffle and clears it on return to ordered", async t => {
  const f = await setup(t);
  for (const action of ["toggle","previous","next","clear-queue"]) f.click(action);
  assert.deepEqual(f.sent,[['toggle',{session:7}],['previous',{session:7}],['next',{session:7}],['clearQueue',{session:7}]]);
  for (const [repeat,next] of [["none","all"],["all","one"]]) {
    f.controller.update(nativeState({repeat})); f.click("play-mode");
    assert.deepEqual(f.sent.at(-1),["setRepeat",{value:next,session:7}]);
  }
  f.controller.update(nativeState({repeat:"one"})); f.click("play-mode");
  assert.deepEqual(f.sent.slice(-2),[["setRepeat",{value:"all",session:7}],["setShuffle",{value:true,session:7}]]);
  f.controller.update(nativeState({repeat:"all",shuffle:true}));
  f.click("play-mode");
  assert.deepEqual(f.sent.slice(-2),[["setShuffle",{value:false,session:7}],["setRepeat",{value:"none",session:7}]]);
  f.reject(); f.click("toggle"); assert.equal(f.notices.length,1);
  assert.equal(f.controller.snapshot().playing,false,"send acknowledgement cannot invent native playback");
});

test('production playback button labels current-list shuffle', async t => {
  const f = await setup(t, nativeState({repeat:'all',shuffle:true}), true);
  f.click('open-player');
  assert.equal(f.find('[data-music-action="play-mode"]').getAttribute('aria-label'),'随机播放（当前列表）');
});

test("disabled capabilities, invalid indices and stale rendered controls cannot send", async t => {
  const f = await setup(t, nativeState({capabilities:{canToggle:false,canPrevious:false,canNext:false,canSeek:false}}));
  for (const action of ["toggle","previous","next"]) f.click(action);
  const row=f.find('[data-music-action="play-now"]');
  for (const index of ["-1","NaN","","9"]) {row.dataset.musicIndex=index;row.click();}
  assert.equal(f.sent.length,0);
  row.dataset.musicIndex="0";row.dataset.musicSession="6";row.click();
  assert.equal(f.sent.length,0);
});

test("file imports validate count, retain File identity and reset the input for repeat selection", async t => {
  const f=await setup(t), field=f.find("[data-music-file-input]");
  const file=new f.dom.window.File(["audio"],"one.mp3",{type:"audio/mpeg"});
  const choose=files=>{Object.defineProperty(field,"files",{configurable:true,value:files});f.fire(field,"change");};
  choose([]); assert.equal(f.imported.length,0);
  choose(Array(33).fill(file)); assert.equal(f.imported.length,0); assert.match(f.notices.at(-1),/32/);
  choose([file]); assert.equal(f.imported[0][0],file); assert.equal(field.value,"");
  choose([file]); assert.equal(f.imported.length,2);
  f.rejectImport();choose([file]);assert.equal(f.notices.length,2);
  let opened=0;field.addEventListener("click",event=>{event.preventDefault();opened++;});
  f.click("import-local");assert.equal(opened,1);
  assert.equal(f.sent.length,0);
});

test("scoped keyboard controls respect editing, modifiers and panel precedence", async t => {
  const f=await setup(t), surface=()=>f.find("[data-music-root]");
  f.fire(surface(),"keydown",{key:" "}); assert.deepEqual(f.sent.at(-1),["toggle",{session:7}]);
  f.fire(surface(),"keydown",{key:" ",repeat:true});assert.equal(f.sent.length,1);
  f.fire(surface(),"keydown",{key:"ArrowRight"}); assert.deepEqual(f.sent.at(-1),["seek",{position:25,session:7}]);
  f.fire(surface(),"keyup",{key:"ArrowRight"});
  f.fire(surface(),"keydown",{key:"ArrowLeft"});assert.deepEqual(f.sent.at(-1),["seek",{position:15,session:7}]);
  f.fire(surface(),"keyup",{key:"ArrowLeft"});
  const count=f.sent.length,search=f.find("[data-music-search]");search.focus();
  for(const key of [" ","ArrowRight"]) f.fire(search,"keydown",{key});
  f.fire(surface(),"keydown",{key:"ArrowRight",altKey:true});
  assert.equal(f.sent.length,count);
  f.click("open-player"); f.click("queue");
  f.fire(surface(),"keydown",{key:"Escape"});assert.equal(f.controller.snapshot().view.panel,"");
  assert.equal(f.controller.snapshot().view.screen,"player");
  f.fire(surface(),"keydown",{key:"Escape"});assert.equal(f.controller.snapshot().view.screen,"home");
  const key=f.fire(surface(),"keydown",{key:"k",ctrlKey:true});assert.equal(key.defaultPrevented,true);
  assert.equal(f.document.activeElement,f.find("[data-music-search]"));
  f.fire(f.document.querySelector("#outside"),"keydown",{key:" "});assert.equal(f.sent.length,count);
});

test("dispose removes every delegated listener and supports remount without duplicate commands", async t => {
  const f=await setup(t), removed=[], added=[];
  f.controller.dispose();
  const add=f.root.addEventListener.bind(f.root),remove=f.root.removeEventListener.bind(f.root);
  f.root.addEventListener=(...args)=>{added.push(args);return add(...args);};
  f.root.removeEventListener=(...args)=>{removed.push(args);return remove(...args);};
  const controller=f.mount(f.root,f.options);
  f.click("toggle");assert.equal(f.sent.length,1);
  controller.dispose();controller.dispose();
  assert.equal(removed.length,added.length);
  for(const [name,handler] of added) assert.ok(removed.some(([n,h])=>n===name&&h===handler));
  const tree=f.root.firstElementChild;
  controller.update(nativeState({error:"late"}));assert.equal(f.root.firstElementChild,tree);
  f.click("toggle");assert.equal(f.sent.length,1);
  const remount=f.mount(f.root,f.options);f.click("toggle");assert.equal(f.sent.length,2);remount.dispose();
});

test("new session refreshes rendered control identity and rejects a pointer action begun before it", async t => {
  const f=await setup(t);
  f.controller.update(nativeState({session:8,revision:2}));
  f.click("toggle");
  assert.deepEqual(f.sent.at(-1),["toggle",{session:8}]);
  f.fire(f.find('[data-music-action="toggle"]'),"pointerdown");
  f.controller.update(nativeState({session:9,revision:3}));
  const count=f.sent.length;
  f.click("toggle");assert.equal(f.sent.length,count,"press and release may straddle a resource replacement");
  f.click("toggle");assert.deepEqual(f.sent.at(-1),["toggle",{session:9}]);
});

test("structural refresh preserves a valid drag and moving focus off the range cancels it", async t => {
  const f=await setup(t);
  let range=f.find("[data-music-progress]");range.focus();
  f.fire(range,"pointerdown");range.value="74";f.fire(range,"input");
  f.controller.update(nativeState({error:"transient",revision:2}));
  range=f.find("[data-music-progress]");
  assert.equal(range.value,"74");
  f.fire(range,"change");assert.deepEqual(f.sent.at(-1),["seek",{position:74,session:7}]);
  f.fire(range,"pointerdown");range.value="90";f.fire(range,"input");range.focus();
  f.find("[data-music-search]").focus();
  const count=f.sent.length;f.fire(range,"change");assert.equal(f.sent.length,count);
});

test("real renderer filters local title, artist and album, preserves focus and returns to home scroll", async t => {
  const f=await setup(t,nativeState({account:{signedIn:true}}),true);
  assert.equal(f.root.querySelectorAll(".MusicRecommendationCard").length,0,"native account is not a provider login");
  f.find('[data-music-nav="local"]').click();
  const rows=()=>[...f.root.querySelectorAll("[data-music-track-id]")].map(row=>row.dataset.musicTrackId);
  f.click("list-search");
  for(const query of ["first","m83","morning"]) {
    f.input("[data-music-list-search]",query);assert.deepEqual(rows(),["one"]);
    assert.equal(f.document.activeElement,f.find("[data-music-list-search]"));
  }
  f.click("close-list-search");assert.deepEqual(rows(),["one","two"]);
  f.input("[data-music-search]","Second");
  assert.deepEqual(rows(),["one","two"],"global search waits for submit");
  assert.ok(f.root.querySelector('[data-music-action="clear-search"]'),"clear is available while editing the query");
  f.fire(f.find("[data-music-search-form]"),"submit");assert.deepEqual(rows(),["two"]);
  f.find(".MusicContent").scrollTop=148;
  f.click("open-player");
  const player=f.find("[data-music-root]"),progress=f.find("[data-music-progress]");
  f.controller.update(nativeState({account:{signedIn:true},revision:2,playing:true,status:"playing",timeline:{position:45,duration:120}}));
  assert.equal(f.find("[data-music-root]"),player);
  assert.equal(f.find("[data-music-progress]"),progress);assert.equal(progress.value,"45");
  f.click("lyrics");assert.equal(f.root.querySelector('.MusicLyricsBody'),null);
  f.click("back-home");assert.deepEqual(rows(),["two"]);
  assert.equal(f.find(".MusicContent").scrollTop,148);
});

test("IME composition preserves both search inputs until composition commits", async t => {
  const f=await setup(t);
  f.click("list-search");
  for (const selector of ["[data-music-search]","[data-music-list-search]"]) {
    const field=f.find(selector);field.focus();field.value="\u6d4b";
    field.dispatchEvent(new f.dom.window.InputEvent("input",{bubbles:true,isComposing:true,data:"\u6d4b"}));
    assert.equal(f.find(selector),field,"composition must keep the native editor node alive");
    field.value="\u6d4b\u8bd5";f.fire(field,"compositionend");
    assert.equal(f.document.activeElement,f.find(selector));
    assert.equal(f.find(selector).value,"\u6d4b\u8bd5");
  }
  assert.equal(f.controller.snapshot().view.query,"\u6d4b\u8bd5");
  assert.equal(f.controller.snapshot().view.listQuery,"\u6d4b\u8bd5");
});

test("production search keeps its editor and list intact while drafting and ignores an IME submit", async t => {
  const f=await setup(t,nativeState(),true);
  f.find('[data-music-nav="local"]').click();
  const field=f.find('[data-music-search]'), table=f.find('.MusicTrackList');
  field.focus();field.value='Second';field.setSelectionRange(2,4);
  f.fire(field,'input');
  assert.equal(f.find('[data-music-search]'),field);
  assert.equal(f.find('.MusicTrackList'),table);
  assert.deepEqual([field.selectionStart,field.selectionEnd],[2,4]);
  assert.equal(f.find('[data-music-action="clear-search"]').hidden,false);
  f.fire(field,'compositionstart');
  f.fire(f.find('[data-music-search-form]'),'submit');
  assert.equal(f.controller.snapshot().view.search,'');
  f.fire(field,'compositionend');
  f.fire(f.find('[data-music-search-form]'),'submit');
  assert.deepEqual([...f.root.querySelectorAll('[data-music-track-id]')].map(row=>row.dataset.musicTrackId),['two']);
  assert.equal(f.find('.MusicSongTab span').textContent,'1');
});

test("native metadata arriving during composition waits for the editor to commit", async t => {
  const f=await setup(t,nativeState(),true);
  const field=f.find('[data-music-search]');field.focus();
  f.fire(field,'compositionstart');field.value='测试';
  field.dispatchEvent(new f.dom.window.InputEvent('input',{bubbles:true,isComposing:true}));
  f.controller.update(nativeState({revision:2,current:tracks[1],error:'playback-failed'}));
  assert.equal(f.find('[data-music-search]'),field);
  f.fire(field,'compositionend');
  assert.equal(f.find('[data-music-search]').value,'测试');
  assert.equal(f.document.activeElement,f.find('[data-music-search]'));
  assert.equal(f.find('.MusicPlayerStatus').textContent,'音乐操作未成功');
});

test("production navigation clears both committed search and the previous list filter", async t => {
  const f=await setup(t,nativeState(),true);
  f.input('[data-music-search]','First');
  f.fire(f.find('[data-music-search-form]'),'submit');
  f.click('list-search');f.input('[data-music-list-search]','Morning');
  f.find('[data-music-nav="local"]').click();
  const view=f.controller.captureView();
  assert.equal(view.search,'');assert.equal(view.query,'');
  assert.equal(view.listQuery,'');assert.equal(view.listSearchOpen,false);
  assert.equal(f.root.querySelectorAll('[data-music-track-id]').length,2);
});

test("supplied playlist cards open collection details and restore only their identity", async t => {
  const library={signedIn:true,recommendations:[{id:'curated',title:'Evening',tracks:[tracks[1]]}],
    playlists:[{id:'owned',title:'Morning',owned:true,tracks:[tracks[0]]}]};
  const f=await setup(t,nativeState(),true,{library});
  f.find('.MusicRecommendationCard').click();
  assert.equal(f.find('.MusicSectionHeader h1').textContent,'Evening');
  assert.equal(f.controller.captureView().playlistId,JSON.stringify(['','playlist','curated']));
  assert.deepEqual([...f.root.querySelectorAll('[data-music-track-id]')].map(row=>row.dataset.musicTrackId),['two']);
  f.find('.MusicPlaylistItem').click();
  assert.equal(f.find('.MusicPlaylistItem').getAttribute('aria-current'),'page');
  assert.equal(f.find('.MusicSectionHeader h1').textContent,'Morning');
  f.click('open-player');f.click('back-home');
  assert.equal(f.find('.MusicSectionHeader h1').textContent,'Morning');
  assert.equal(f.sent.length,0,'opening a collection never invents native playback');
});

test("collection identities distinguish numeric IDs across provider and collection type", async t => {
  const library={signedIn:true,playlists:[{id:42,providerId:'qq',type:'playlist',title:'QQ list',owned:true,tracks:[tracks[0]]}],
    recommendations:[{id:42,providerId:'netease',type:'album',title:'Netease album',tracks:[tracks[1]]},
      {id:'42',providerId:'qq',type:'album',title:'QQ album',tracks:[]}]};
  const f=await setup(t,nativeState(),true,{library});
  f.find('.MusicRecommendationCard').click();
  assert.equal(f.find('.MusicSectionHeader h1').textContent,'Netease album');
  assert.equal(f.controller.captureView().playlistId,JSON.stringify(['netease','album','42']));
  assert.equal(f.find('.MusicPlaylistItem').getAttribute('aria-current'),'false');
  f.find('[data-music-nav="recommendations"]').click();
  f.root.querySelectorAll('.MusicRecommendationCard')[1].click();
  assert.equal(f.find('.MusicSectionHeader h1').textContent,'QQ album');
  f.find('.MusicPlaylistItem').click();
  assert.equal(f.find('.MusicSectionHeader h1').textContent,'QQ list');
});

test("closing an overlay returns keyboard focus to its opener", async t => {
  const f=await setup(t,nativeState(),true,{createVisualizer:()=>({update(){},dispose(){}})});
  f.click('open-player');
  const opener=f.find('[data-music-action="queue"]');opener.focus();opener.click();
  const close=f.find('aside [data-music-action="close-panel"]');close.focus();
  f.fire(close,'keydown',{key:'Escape'});
  assert.equal(f.document.activeElement,f.find('[data-music-action="queue"]'));
  f.click('back-home');f.find('[data-music-nav="local"]').click();f.click('list-search');
  f.click('close-list-search');
  assert.equal(f.document.activeElement,f.find('[data-music-action="list-search"]'));
});

test("transport refuses zero session and keyboard shortcuts leave buttons and other subtrees alone", async t => {
  const f=await setup(t,nativeState({session:0}));
  for(const action of ["toggle","play-now","play-mode","clear-queue"]) f.click(action);
  assert.equal(f.sent.length,0,"session zero cannot be included in native commands");
  f.controller.update(nativeState());
  const nav=f.find('[data-music-nav="local"]');nav.focus();
  const space=f.fire(nav,"keydown",{key:" "});
  assert.equal(space.defaultPrevented,false,"space activates a focused native button");
  assert.equal(f.sent.length,0);
  const foreign=f.document.createElement("div");
  foreign.innerHTML='<section data-music-root><button data-music-action="toggle">Other music owner</button></section>';
  f.find("[data-music-root]").append(foreign);
  foreign.querySelector("button").click();assert.equal(f.sent.length,0);
});

test("a held arrow key cannot seek another track before release", async t => {
  const f=await setup(t),surface=()=>f.find("[data-music-root]");
  f.fire(surface(),"keydown",{key:"ArrowRight"});
  f.fire(surface(),"keydown",{key:"ArrowRight",repeat:true});
  assert.deepEqual(f.sent.at(-1),["seek",{position:30,session:7}]);
  f.controller.update(nativeState({current:tracks[1],currentIndex:1,revision:2}));
  f.fire(surface(),"keydown",{key:"ArrowRight",repeat:true});assert.equal(f.sent.length,2);
  f.fire(surface(),"keyup",{key:"ArrowRight"});
  f.fire(surface(),"keydown",{key:"ArrowLeft"});assert.equal(f.sent.length,3);
});

test("real screen transitions keep keyboard focus inside music when the focused control disappears", async t => {
  const f=await setup(t,nativeState(),true);
  f.find('[data-music-action="open-player"]').focus();f.click("open-player");
  assert.ok(f.find("[data-music-root]").contains(f.document.activeElement));
  f.fire(f.document.activeElement,"keydown",{key:" "});
  assert.deepEqual(f.sent.at(-1),["toggle",{session:7}]);
  f.click("queue");f.find('[data-music-action="close-panel"]').focus();f.click("close-panel");
  assert.ok(f.find("[data-music-root]").contains(f.document.activeElement));
  f.find('[data-music-action="back-home"]').focus();f.click("back-home");
  assert.ok(f.find("[data-music-root]").contains(f.document.activeElement));
});

test("failed cover hides the broken image and teardown removes its capture listener", async t => {
  const cover={...tracks[0],artwork:"https://static.lumedesktop.com/music/missing.webp"};
  const f=await setup(t,nativeState({current:cover,queue:[cover]}),true);
  f.find('[data-music-nav="local"]').click();
  const image=f.find('[data-music-image]');
  image.dispatchEvent(new f.dom.window.Event('error'));
  assert.equal(image.hidden,true);
  assert.ok(image.parentElement.querySelector('.MusicIcon'));
  f.controller.dispose();
  image.hidden=false; image.dispatchEvent(new f.dom.window.Event('error'));
  assert.equal(image.hidden,false);
});

test('seek previews elapsed time and fill without sending until commit',async t=>{
  const f=await setup(t,nativeState(),true);
  f.click('open-player');
  const range=f.find('[data-music-progress]');
  f.fire(range,'pointerdown');range.value='90';f.fire(range,'input');
  assert.equal(f.find('[data-music-elapsed]').textContent,'1:30');
  assert.equal(range.style.getPropertyValue('--progress'),'75%');
  assert.equal(f.sent.length,0);
  f.fire(range,'pointercancel');
  assert.equal(f.find('[data-music-elapsed]').textContent,'0:20');
});

test('player retains renderer across panels and progress, mode selection is local, home disposes',async t=>{
  const calls=[],updates=[];
  const f=await setup(t,nativeState({pageVisible:true}),true,{createVisualizer:host=>{
    calls.push(['mount',host]);return {update:(state,mode)=>updates.push({state,mode}),dispose:()=>calls.push(['dispose'])};
  }});
  assert.equal(calls.length,0);
  f.click('open-player');const host=f.find('[data-music-stage]');
  const observer=new f.dom.window.MutationObserver(()=>{});
  observer.observe(f.root,{childList:true,subtree:true});
  f.click('queue');f.click('close-panel');f.click('lyrics');
  assert.equal(f.find('[data-music-stage]'),host);
  assert.equal(observer.takeRecords().some(record=>[...record.removedNodes].some(node=>node===host||node.contains?.(host))),false,
    'stage and its ancestors stay connected so iframe contexts are not reloaded');
  observer.disconnect();
  const picker=f.find('[data-music-visualizer-picker]');
  const proxy=f.find('select[data-music-preference="mode"]');
  assert.deepEqual([...picker.querySelectorAll('[data-music-visualizer]')].map(element=>element.dataset.musicVisualizer),
    ['partita','fume','sonnet','gallery','3d','lumiere','cadenza','tilt','cappella','diorama','pendolo','classic','tempera']);
  assert.equal(picker.querySelector('[data-music-visualizer="motif"]'),null,'removed visualizer is not user-visible');
  assert.equal(proxy.getAttribute('aria-hidden'),'true');
  assert.equal(proxy.getAttribute('tabindex'),'-1');
  f.find('[data-music-visualizer-trigger]').click();
  f.find('[data-music-visualizer="gallery"]').click();
  assert.deepEqual(f.commits,[{mode:'gallery'}]);
  f.controller.updatePreferences({...f.options.preferences,
    preferences:{...f.options.preferences.preferences,mode:'gallery'}});
  assert.equal(updates.at(-1).mode,'gallery');
  const lines=[{startTime:0,endTime:10,fullText:'First',words:[]}];
  f.controller.update(nativeState({pageVisible:true,lyrics:lines}));
  assert.equal(updates.at(-1).state.lines[0].fullText,'First');
  f.controller.update(nativeState({pageVisible:true,timeline:{position:22,duration:120}}));
  assert.equal(calls.filter(([kind])=>kind==='mount').length,1);
  assert.equal(f.sent.length,0,'visualizer selection never changes the audio host');
  f.click('back-home');assert.equal(calls.at(-1)[0],'dispose');
});

test('native preference updates preview in place and settings survive structural player snapshots',async t=>{
  const {DEFAULT_MUSIC_PREFERENCES:defaults}=await import('../music-preferences.js');
  const changes=[],commits=[],updates=[];
  const f=await setup(t,nativeState({pageVisible:true}),true,{
    preferences:{preferences:{...defaults,mode:'gallery'},loaded:true,pending:false},
    onPreferencePreview:patch=>changes.push(patch),onPreferenceCommit:patch=>commits.push(patch),
    createVisualizer:()=>({update:(value,mode)=>updates.push({value,mode}),dispose(){}}),
  });
  f.click('open-player');f.click('lyrics');
  assert.equal(f.find('[data-music-visualizer="gallery"]').getAttribute('aria-selected'),'true');
  f.find('[data-music-panel-tab="controls"]').click();
  const stage=f.find('[data-music-stage]'),color=f.find('[data-music-preference="lyricsFontColor"]');
  color.value='#ff0000';f.fire(color,'input');
  assert.deepEqual(changes,[{lyricsFontColor:'#ff0000'}]);assert.equal(commits.length,0);
  const next={preferences:{...defaults,mode:'gallery',lyricsFontColor:'#ff0000'},loaded:true,pending:false};
  f.controller.updatePreferences(next);
  assert.equal(f.find('[data-music-stage]'),stage);assert.equal(f.find('[data-music-preference="lyricsFontColor"]'),color);
  assert.equal(updates.at(-1).value.preferences.lyricsFontColor,'#ff0000');
  f.fire(color,'change');assert.deepEqual(commits,[{lyricsFontColor:'#ff0000'}]);
  color.focus();f.controller.update(nativeState({current:tracks[1],currentIndex:1,pageVisible:true}));
  assert.equal(f.find('[data-music-preference="lyricsFontColor"]'),color);
  assert.equal(f.document.activeElement,color);
  f.find('[data-music-setting="intensity"]').click();
  assert.deepEqual(commits.at(-1),{animationIntensity:'chaotic'});
  f.controller.updatePreferences({...next,preferences:{...next.preferences,mode:'sonnet'}});
  assert.ok(f.find('[data-music-preference="lyricsKaraokeFillColor"]').closest('[data-music-color-group]').hidden);
  assert.equal(f.root.querySelectorAll('[data-music-sonnet-color]').length,3);
  assert.equal(f.find('[data-music-color-group="sonnet"]').hidden,false);
  assert.equal(f.sent.length,0,'preferences never emit audio commands');
});

test('imported font metadata refreshes labels without remounting the player or changing its family',async t=>{
  const {DEFAULT_MUSIC_PREFERENCES:defaults}=await import('../music-preferences.js');
  const family=`LumeImportedFont_${'a'.repeat(32)}`;
  let label;
  const f=await setup(t,nativeState({pageVisible:true}),true,{
    preferences:{preferences:{...defaults,fontFamily:family},loaded:true,pending:false},
    getFontLabel:value=>value===family?label:undefined,
  });
  f.click('open-player');f.click('lyrics');
  const stage=f.find('[data-music-stage]'),field=f.find('[data-music-preference="fontFamily"]');
  assert.equal(field.selectedOptions[0].textContent,'已导入字体');
  const quick=f.find('[data-music-font-trigger="quick"]');
  assert.equal(quick.textContent,'已导入字体');
  assert.equal(quick.textContent.includes('LumeImportedFont_'),false);
  label='Imported <Test> Font';
  f.controller.refreshFontLabels();
  assert.equal(field.value,family);
  assert.equal(field.selectedOptions[0].textContent,label);
  assert.equal(field.querySelector('test'),null);
  assert.equal(quick.textContent,label);
  assert.equal(quick.title,label);
  assert.equal(quick.childElementCount,0,'font label remains text, never parsed HTML');
  f.find('[data-music-panel-tab="controls"]').click();
  f.find('[data-music-panel-tab="globalSettings"]').click();
  assert.equal(field.selectedOptions[0].textContent,label);
  assert.equal(quick.textContent,label);
  assert.equal(f.find('[data-music-stage]'),stage);
  assert.equal(f.commits.length,0);
});

test('default background uses its own descriptor channel and never replaces selected media',async t=>{
  const calls=[],fallback={id:'default-home-background',name:'Home',kind:'image',mimeType:'image/webp',
    url:'https://static.lumedesktop.com/lume/home.webp'};
  const chosen={id:'chosen',name:'Chosen',kind:'image',mimeType:'image/png',
    url:'http://127.0.0.1:1234/'+'a'.repeat(64)+'/media.png'};
  const f=await setup(t,nativeState({pageVisible:true}),true,{
    defaultBackground:fallback,
    createBackground:(_canvas,options)=>{calls.push(options);return {update:next=>calls.push(next),dispose(){}};},
  });
  assert.equal(calls[0].defaultMedia,fallback);
  assert.equal(calls[0].media,null);
  f.controller.updateBackground({media:chosen});
  assert.equal(calls.at(-1).media,chosen);
  const next={...fallback,url:'https://static.lumedesktop.com/lume/next.webp'};
  f.controller.updateDefaultBackground(next);
  assert.equal(calls.at(-1).defaultMedia,next);
  assert.equal(Object.hasOwn(calls.at(-1),'media'),false);
  f.controller.updateBackground({media:null});
  assert.equal(calls.at(-1).media,null);
  assert.equal(calls.at(-1).defaultMedia,next);
});
