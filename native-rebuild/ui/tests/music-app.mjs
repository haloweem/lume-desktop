import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {installStartupBrowserApis} from './startup-dom.mjs';
import {initialMusicState} from '../music.js';
import {validHostMessage} from '../bridge.js';

const {JSDOM, VirtualConsole} = createRequire(import.meta.url)(process.argv[2]);
const failures = [], virtualConsole = new VirtualConsole();
virtualConsole.on('jsdomError', error => failures.push(error.message));
const dom = new JSDOM('<div id="app"><header id="titlebar"></header><main id="content"></main><div id="notice" hidden></div></div>',
  {url:'https://lume.local/', pretendToBeVisual:true, virtualConsole});
globalThis.window = dom.window;
globalThis.document = dom.window.document;
globalThis.File = dom.window.File;
globalThis.matchMedia = () => ({matches:true});
const finishStartup = installStartupBrowserApis(window);
const listeners = new Set(), commands = [], filesSent = [];
const receive = message => {
  assert.equal(validHostMessage(message), true, 'fixture must pass the real bridge validator');
  listeners.forEach(listener => listener({data:message}));
};
window.chrome = {webview:{
  addEventListener(_, listener) {listeners.add(listener);},
  removeEventListener(_, listener) {listeners.delete(listener);},
  postMessage(command) {commands.push(command);},
  postMessageWithAdditionalObjects(command, files) {filesSent.push([command, files]);},
}};
const find = selector => {const element=document.querySelector(selector); assert.ok(element, selector); return element;};
const click = selector => find(selector).click();
const fire = (element, type) => element.dispatchEvent(new window.Event(type, {bubbles:true,cancelable:true}));
const musicCommands = action => commands.filter(command => command?.scope==='music' && command.action===action);
const track = {id:'fixture-1', title:'First Light', artist:'M83', album:'Morning', artwork:'', duration:180, source:'C:/Music/one.mp3'};
const {query, nav, lyrics, ...nativeState} = initialMusicState();
let snapshot = {...nativeState, version:1, type:'music-state', session:7, revision:1, status:'paused',
  current:track, queue:[track], pageOpen:true, pageVisible:true, timeline:{position:20,duration:180},
  capabilities:{canToggle:true,canNext:true,canPrevious:true,canSeek:true}};
try {
  await import('../app.js');
  await finishStartup(commands, true);
  click('[data-titlebar-music]');
  assert.ok(find('[data-music-root]'));
  assert.equal(find('[data-titlebar-music]').getAttribute('aria-label'), '壁纸');
  assert.equal(find('[data-titlebar-music] path').getAttribute('d'),
    'M64 48H448Q512 48 512 112V400Q512 464 448 464H64Q0 464 0 400V112Q0 48 64 48ZM64 112V336L176 200Q184 192 192 200L288 320L344 264Q352 256 360 264L448 352V112ZM376 184A24 24 0 1 1 328 184A24 24 0 0 1 376 184Z');
  click('[data-titlebar-music]');
  assert.equal(document.querySelector('[data-music-root]'), null);
  assert.equal(find('[data-titlebar-music]').getAttribute('aria-label'), '音乐');
  assert.equal(musicCommands('open').length, 1);
  assert.equal(musicCommands('close').length, 1);
  commands.length=0;
  receive({version:1,type:'navigate',page:'music'});
  receive(snapshot);
  assert.equal(musicCommands('open').length,1);
  assert.equal(musicCommands('loadHome').length,1);
  assert.equal(musicCommands('loadLyrics').length,1);
  assert.deepEqual(musicCommands('loadLyrics')[0],{version:1,scope:'music',action:'loadLyrics',session:7,id:track.id});
  receive({version:1,type:'music-lyrics',session:6,trackId:track.id,lines:[]});
  receive({version:1,type:'music-lyrics',session:7,trackId:track.id,lines:[]});
  const search = find('[data-music-search]');
  search.value='First'; search.focus(); fire(search,'input');
  const root = find('[data-music-root]'), field=find('[data-music-search]');
  find('.MusicContent').scrollTop=81;
  receive({...snapshot,revision:2,timeline:{position:21,duration:180}});
  assert.equal(musicCommands('loadLyrics').length,1,'progress snapshots do not request lyrics again');
  assert.equal(find('[data-music-root]'),root,'progress cannot replace the music subtree');
  assert.equal(find('[data-music-search]'),field,'progress keeps the search input');
  assert.equal(field.value,'First');
  assert.equal(document.activeElement,field);
  assert.equal(find('.MusicContent').scrollTop,81);
  click('[data-music-action="open-player"]');
  assert.equal(find('[data-music-root]').dataset.screen,'player');
  const player=find('[data-music-root]');
  receive({...snapshot,revision:3,playing:true,status:'playing',timeline:{position:22,duration:180}});
  assert.equal(find('[data-music-root]'),player);
  assert.equal(find('[data-music-progress]').value,'22');
  click('[data-music-action="toggle"]');
  assert.equal(musicCommands('toggle').length,1,'one click sends one command');
  assert.equal(musicCommands('toggle')[0].session,7);
  click('[data-music-action="queue"]');
  click('[data-music-action="close-panel"]');
  const range=find('[data-music-progress]');
  range.value='50'; fire(range,'input'); fire(range,'change');
  assert.equal(musicCommands('seek').length,1);
  assert.equal(musicCommands('seek')[0].position,50);
  click('[data-music-action="back-home"]');
  assert.equal(find('[data-music-search]').value,'First');
  assert.equal(find('.MusicContent').scrollTop,81);
  const file=new window.File(['fixture'], 'song.mp3', {type:'audio/mpeg'});
  const input=find('[data-music-file-input]');
  Object.defineProperty(input,'files',{value:[file]}); fire(input,'change');
  assert.equal(filesSent.length,1);
  assert.equal(filesSent[0][1][0],file);
  click('[data-music-action="open-player"]');
  click('[data-music-action="queue"]');
  const detached=find('[data-music-action="play-now"]');
  receive({version:1,type:'navigate',page:'applicationSettings',section:'general'});
  assert.equal(musicCommands('close').length,1);
  const count=commands.length;
  detached.click();
  receive({...snapshot,revision:4});
  assert.equal(document.querySelector('[data-music-root]'),null);
  assert.equal(commands.length,count,'unmounted view cannot send commands');
  receive({version:1,type:'navigate',page:'music'});
  assert.equal(musicCommands('open').length,2);
  receive(snapshot);
  assert.equal(musicCommands('loadLyrics').length,2,'remount reloads lyrics released on close');
  assert.equal(find('[data-music-root]').dataset.screen,'player');
  click('[data-music-action="toggle"]');
  assert.equal(musicCommands('toggle').length,2,'remount has no duplicate handlers');
  for(let i=0;i<20;i++) {
    const opens=musicCommands('open').length,closes=musicCommands('close').length;
    click('[data-titlebar-music]');
    assert.equal(find('[data-titlebar-music]').getAttribute('aria-label'),'音乐');
    assert.equal(document.querySelector('[data-music-root]'),null);
    click('[data-titlebar-music]');
    assert.equal(find('[data-titlebar-music]').getAttribute('aria-label'),'壁纸');
    assert.equal(find('[data-music-root]').dataset.screen,'player');
    assert.equal(musicCommands('open').length,opens+1);
    assert.equal(musicCommands('close').length,closes+1);
    receive({...snapshot,revision:5+i,timeline:{position:25+i,duration:180}});
    assert.equal(find('[data-music-progress]').value,String(25+i));
  }
  assert.deepEqual(failures,[]);
  console.log('PASS: bidirectional titlebar navigation, 20 round trips, retained music view and one command per gesture');
} finally {
  window.dispatchEvent(new window.Event('beforeunload'));
  dom.window.close();
}
