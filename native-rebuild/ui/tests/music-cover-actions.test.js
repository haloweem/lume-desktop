import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {copySongInfo} from '../music-cover-actions.js';
import {musicPlayerPanel,updateMusicPlayerPanel} from '../music-player-panel.js';
import {DEFAULT_MUSIC_PREFERENCES} from '../music-preference-values.js';

const jsdomPath=process.env.LUME_JSDOM_PATH;
const {JSDOM}=jsdomPath?createRequire(import.meta.url)(jsdomPath):{};

const track={
  id:'song-1',
  title:'Title',
  artist:'Artist',
  album:'Album',
  source:'C:/private/song.mp3',
  artwork:'https://example.invalid/cover.jpg',
  duration:123,
};

function domFixture(html='<main></main>') {
  const dom=new JSDOM(html,{url:'https://lume.local/',pretendToBeVisual:true});
  return {dom,document:dom.window.document};
}

test('copySongInfo writes only the historical title-artist-album text', {skip:!jsdomPath}, async t => {
  const {dom,document}=domFixture();
  t.after(()=>dom.window.close());
  Object.defineProperty(dom.window,'isSecureContext',{configurable:true,value:true});
  const writes=[];
  Object.defineProperty(dom.window.navigator,'clipboard',{
    configurable:true,
    value:{writeText:async value=>writes.push(value)},
  });

  assert.equal(await copySongInfo(document,track),true);
  assert.deepEqual(writes,['Title - Artist - Album']);
  assert.equal(document.querySelector('textarea'),null);
});

test('copySongInfo preserves empty fields, Unicode and line breaks without adding extra DTO fields', {skip:!jsdomPath}, async t => {
  const {dom,document}=domFixture();
  t.after(()=>dom.window.close());
  Object.defineProperty(dom.window,'isSecureContext',{configurable:true,value:true});
  const writes=[];
  Object.defineProperty(dom.window.navigator,'clipboard',{
    configurable:true,
    value:{writeText:async value=>writes.push(value)},
  });
  assert.equal(await copySongInfo(document,{...track,title:'歌曲\n第二行',artist:'甲, 乙',album:''}),true);
  assert.equal(await copySongInfo(document,{...track,title:'',artist:'',album:''}),true);
  assert.deepEqual(writes,['歌曲\n第二行 - 甲, 乙 - ',' -  - ']);
});

test('copySongInfo returns false without a current track', {skip:!jsdomPath}, async t => {
  const {dom,document}=domFixture();
  t.after(()=>dom.window.close());
  let commands=0;
  document.execCommand=()=>{commands++;return true;};

  assert.equal(await copySongInfo(document,null),false);
  assert.equal(await copySongInfo(document,undefined),false);
  assert.equal(commands,0);
});

for(const secure of [true,false]) {
  test(`pre-aborted copy does not write or start fallback in ${secure?'secure':'insecure'} contexts`, {skip:!jsdomPath}, async t => {
    const {dom,document}=domFixture('<input value="keep this">');
    t.after(()=>dom.window.close());
    const controller=new dom.window.AbortController();
    controller.abort();
    const input=document.querySelector('input');
    input.focus();input.setSelectionRange(1,5,'backward');
    const before=document.body.innerHTML;
    let writes=0,commands=0,focuses=0;
    Object.defineProperty(dom.window,'isSecureContext',{configurable:true,value:secure});
    Object.defineProperty(dom.window.navigator,'clipboard',{
      configurable:true,
      value:{writeText:async()=>{writes++;}},
    });
    document.execCommand=()=>{commands++;return true;};
    document.addEventListener('focusin',()=>{focuses++;});

    assert.equal(await copySongInfo(document,track,{signal:controller.signal}),false);
    assert.equal(writes,0);
    assert.equal(commands,0);
    assert.equal(focuses,0);
    assert.equal(document.body.innerHTML,before);
    assert.equal(document.activeElement,input);
    assert.deepEqual([input.selectionStart,input.selectionEnd,input.selectionDirection],[1,5,'backward']);
  });
}

for(const outcome of ['resolve','reject']) {
  test(`aborting pending clipboard ${outcome} keeps the submitted result without fallback`, {skip:!jsdomPath}, async t => {
    const {dom,document}=domFixture('<input id="old"><input id="new" value="new selection">');
    t.after(()=>dom.window.close());
    const controller=new dom.window.AbortController();
    Object.defineProperty(dom.window,'isSecureContext',{configurable:true,value:true});
    const writes=[];
    let settle,commands=0,focuses=0;
    Object.defineProperty(dom.window.navigator,'clipboard',{
      configurable:true,
      value:{writeText:value=>{
        writes.push(value);
        return new Promise((resolve,reject)=>{settle=outcome==='resolve'?resolve:reject;});
      }},
    });
    document.execCommand=()=>{commands++;return true;};
    document.querySelector('#old').focus();
    const pending=copySongInfo(document,track,{signal:controller.signal});
    assert.deepEqual(writes,['Title - Artist - Album']);
    controller.abort();
    const input=document.querySelector('#new');
    input.focus();input.setSelectionRange(2,4,'backward');
    document.addEventListener('focusin',()=>{focuses++;});
    const before=document.body.innerHTML;
    settle(outcome==='reject'?new Error('denied'):undefined);

    assert.equal(await pending,outcome==='resolve');
    assert.deepEqual(writes,['Title - Artist - Album']);
    assert.equal(commands,0);
    assert.equal(focuses,0);
    assert.equal(document.body.innerHTML,before);
    assert.equal(document.activeElement,input);
    assert.deepEqual([input.selectionStart,input.selectionEnd,input.selectionDirection],[2,4,'backward']);
  });
}

for(const boundary of ['focus','select','copy','restore']) {
  test(`aborting during fallback ${boundary} cleans up without restoring stale focus or selections`, {skip:!jsdomPath}, async t => {
    const {dom,document}=domFixture('<input id="old" value="old selection"><input id="new" value="new selection"><p>old text</p><p>new text</p>');
    t.after(()=>dom.window.close());
    const controller=new dom.window.AbortController();
    const old=document.querySelector('#old'),input=document.querySelector('#new');
    const [oldText,newText]=Array.from(document.querySelectorAll('p'),p=>p.firstChild);
    const selection=document.getSelection();
    old.focus();old.setSelectionRange(1,5,'backward');
    selection.setBaseAndExtent(oldText,7,oldText,1);
    const before=document.body.innerHTML;
    let commands=0,selects=0;
    const cancel=()=>{
      controller.abort();
      old.setSelectionRange(0,0);
      input.focus();input.setSelectionRange(2,4,'backward');
      selection.setBaseAndExtent(newText,6,newText,2);
    };
    document.addEventListener('focusin',event=>{
      if(boundary==='focus' && event.target.tagName==='TEXTAREA')cancel();
      if(boundary==='restore' && event.target===old)cancel();
    });
    const select=dom.window.HTMLTextAreaElement.prototype.select;
    dom.window.HTMLTextAreaElement.prototype.select=function(){
      selects++;
      select.call(this);
      if(boundary==='select')cancel();
    };
    document.execCommand=()=>{
      commands++;
      if(boundary==='copy')cancel();
      return true;
    };

    assert.equal(await copySongInfo(document,track,{signal:controller.signal}),boundary==='copy' || boundary==='restore');
    assert.equal(commands,boundary==='copy' || boundary==='restore'?1:0);
    assert.equal(selects,boundary==='focus'?0:1);
    assert.equal(document.body.innerHTML,before);
    assert.equal(document.activeElement,input);
    assert.deepEqual([old.selectionStart,old.selectionEnd],[0,0]);
    assert.deepEqual([input.selectionStart,input.selectionEnd,input.selectionDirection],[2,4,'backward']);
    assert.equal(selection.anchorNode,newText);
    assert.equal(selection.anchorOffset,6);
    assert.equal(selection.focusNode,newText);
    assert.equal(selection.focusOffset,2);
  });
}

test('copySongInfo falls back after secure clipboard rejection and restores selection', {skip:!jsdomPath}, async t => {
  const {dom,document}=domFixture('<main><input value="keep this"></main>');
  t.after(()=>dom.window.close());
  const input=document.querySelector('input');
  input.focus();
  input.setSelectionRange(1,5,'backward');
  Object.defineProperty(dom.window,'isSecureContext',{configurable:true,value:true});
  Object.defineProperty(dom.window.navigator,'clipboard',{
    configurable:true,
    value:{writeText:async()=>{throw new Error('denied')}},
  });
  let commandText='';
  document.execCommand=(command)=>{
    assert.equal(command,'copy');
    commandText=document.querySelector('textarea')?.value || '';
    return true;
  };

  const controller=new dom.window.AbortController();
  assert.equal(await copySongInfo(document,track,{signal:controller.signal}),true);
  assert.equal(commandText,'Title - Artist - Album');
  assert.equal(document.querySelector('textarea'),null);
  assert.equal(document.activeElement,input);
  assert.deepEqual([input.selectionStart,input.selectionEnd,input.selectionDirection],[1,5,'backward']);
});

test('insecure contexts skip clipboard and use execCommand', {skip:!jsdomPath}, async t => {
  const {dom,document}=domFixture();
  t.after(()=>dom.window.close());
  Object.defineProperty(dom.window,'isSecureContext',{configurable:true,value:false});
  let writes=0,commands=0;
  Object.defineProperty(dom.window.navigator,'clipboard',{
    configurable:true,
    value:{writeText:async()=>{writes++;}},
  });
  document.execCommand=()=>{commands++;return true;};
  assert.equal(await copySongInfo(document,track),true);
  assert.equal(writes,0);
  assert.equal(commands,1);
});

for(const outcome of ['false','throw','missing','truthy']) {
  test(`fallback ${outcome} returns false and restores DOM, focus and selection`, {skip:!jsdomPath}, async t => {
    const {dom,document}=domFixture('<main><textarea id="editor">original</textarea></main>');
    t.after(()=>dom.window.close());
    const editor=document.querySelector('#editor');
    editor.focus();editor.setSelectionRange(2,6,'backward');
    const before=document.body.innerHTML;
    let commands=0;
    if(outcome!=='missing')document.execCommand=()=>{
      commands++;
      assert.equal(document.querySelectorAll('textarea').length,2);
      if(outcome==='throw')throw new Error('copy failed');
      return outcome==='truthy'?'true':false;
    };
    assert.equal(await copySongInfo(document,track),false);
    assert.equal(commands,outcome==='missing'?0:1);
    assert.equal(document.body.innerHTML,before);
    assert.equal(document.activeElement,editor);
    assert.deepEqual([editor.selectionStart,editor.selectionEnd,editor.selectionDirection],[2,6,'backward']);
  });
}

test('fallback restores a backward document selection', {skip:!jsdomPath}, async t => {
  const {dom,document}=domFixture('<main><p tabindex="0">selected text</p></main>');
  t.after(()=>dom.window.close());
  const paragraph=document.querySelector('p'),text=paragraph.firstChild;
  paragraph.focus();
  const selection=document.getSelection();
  selection.setBaseAndExtent(text,8,text,1);
  document.execCommand=()=>{
    selection.removeAllRanges();
    selection.selectAllChildren(document.querySelector('textarea'));
    return true;
  };
  assert.equal(await copySongInfo(document,track),true);
  assert.equal(document.activeElement,paragraph);
  assert.equal(selection.anchorNode,text);
  assert.equal(selection.anchorOffset,8);
  assert.equal(selection.focusNode,text);
  assert.equal(selection.focusOffset,1);
  assert.equal(document.querySelector('textarea'),null);
});

test('delayed clipboard rejection preserves the newer focus and selection at fallback time', {skip:!jsdomPath}, async t => {
  const {dom,document}=domFixture('<input id="old"><input id="new" value="new selection">');
  t.after(()=>dom.window.close());
  Object.defineProperty(dom.window,'isSecureContext',{configurable:true,value:true});
  let reject;
  Object.defineProperty(dom.window.navigator,'clipboard',{
    configurable:true,
    value:{writeText:()=>new Promise((resolve,rejectCopy)=>{reject=rejectCopy;})},
  });
  document.querySelector('#old').focus();
  document.execCommand=()=>true;
  const pending=copySongInfo(document,track);
  const input=document.querySelector('#new');
  input.focus();input.setSelectionRange(2,4,'backward');
  assert.equal(typeof reject,'function');
  reject(new Error('denied'));
  assert.equal(await pending,true);
  assert.equal(document.activeElement,input);
  assert.deepEqual([input.selectionStart,input.selectionEnd,input.selectionDirection],[2,4,'backward']);
});

test('fallback preparation failure still removes the temporary textarea', {skip:!jsdomPath}, async t => {
  const {dom,document}=domFixture('<button>focused</button>');
  t.after(()=>dom.window.close());
  const button=document.querySelector('button');
  button.focus();
  dom.window.HTMLTextAreaElement.prototype.select=function(){throw new Error('select failed');};
  document.execCommand=()=>true;
  assert.equal(await copySongInfo(document,track),false);
  assert.equal(document.querySelector('textarea'),null);
  assert.equal(document.activeElement,button);
});

test('cover title is copy-activatable only when a current track exists', {skip:!jsdomPath}, t => {
  const {dom,document}=domFixture();
  t.after(()=>dom.window.close());
  const preferences={preferences:DEFAULT_MUSIC_PREFERENCES,loaded:true,pending:false};
  const background={media:null};
  const view={panelTab:'cover'};
  const render=(current)=> {
    const host=document.createElement('div');
    host.innerHTML=musicPlayerPanel({current},view,preferences,background);
    return host.querySelector('[data-music-cover-title]');
  };

  const empty=render(null);
  assert.equal(empty.hasAttribute('data-music-cover-copy'),false);
  assert.equal(empty.hasAttribute('role'),false);
  assert.equal(empty.hasAttribute('tabindex'),false);
  assert.equal(empty.hasAttribute('title'),false);
  assert.equal(empty.hasAttribute('aria-label'),false);

  const active=render(track);
  assert.equal(active.getAttribute('data-music-cover-copy'),'');
  assert.equal(active.getAttribute('role'),'button');
  assert.equal(active.getAttribute('tabindex'),'0');
  assert.equal(active.getAttribute('aria-label'),'复制歌曲信息');
});

test('panel update adds and removes copy activation when the current track changes', {skip:!jsdomPath}, t => {
  const {dom,document}=domFixture();
  t.after(()=>dom.window.close());
  const preferences={preferences:DEFAULT_MUSIC_PREFERENCES,loaded:true,pending:false};
  const background={media:null};
  const root=document.createElement('div');
  root.innerHTML=musicPlayerPanel({current:null},{panelTab:'cover'},preferences,background);
  const title=root.querySelector('[data-music-cover-title]');

  updateMusicPlayerPanel(root,{state:{current:track},view:{panelTab:'cover'},preferences,background});
  assert.equal(title.getAttribute('role'),'button');
  assert.equal(title.getAttribute('tabindex'),'0');
  assert.equal(title.getAttribute('aria-label'),'复制歌曲信息');
  assert.equal(title.getAttribute('title'),'复制歌曲信息');
  assert.equal(title.getAttribute('data-music-cover-copy'),'');
  assert.equal(title.textContent,'Title');

  updateMusicPlayerPanel(root,{state:{current:{...track,id:'two',title:'Next'}},view:{panelTab:'cover'},preferences,background});
  assert.equal(root.querySelector('[data-music-cover-title]'),title);
  assert.equal(title.textContent,'Next');
  assert.equal(title.getAttribute('role'),'button');
  assert.equal(title.getAttribute('data-music-cover-copy'),'');

  updateMusicPlayerPanel(root,{state:{current:null},view:{panelTab:'cover'},preferences,background});
  assert.equal(title.hasAttribute('data-music-cover-copy'),false);
  assert.equal(title.hasAttribute('role'),false);
  assert.equal(title.hasAttribute('tabindex'),false);
  assert.equal(title.hasAttribute('aria-label'),false);
  assert.equal(title.hasAttribute('title'),false);
});
