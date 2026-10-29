import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {createMusicBackground,createMusicBackgroundCatalogQuery,validMusicBackgroundCatalogMessage,
  validMusicBackgroundCommand,validMusicBackgroundMedia,validMusicBackgroundLoopbackUrl,
  validMusicBackgroundThumbnailUrl} from '../music-background.js';
import {createMusicWallpaperGallery} from '../music-wallpaper-gallery.js';

const lease=`http://127.0.0.1:54321/${'a'.repeat(64)}/media.png`;
const item=(id='local-1',patch={})=>({id,name:'Fixture wallpaper',type:'image',thumbnailUrl:lease,...patch});
const catalog=(request=1,patch={})=>({version:1,type:'music-background-catalog',request,status:'ready',
  error:'',total:1,items:[item()],selectedId:'',...patch});
const media={id:'music-owned',name:'Fixture.png',kind:'image',mimeType:'image/png',url:lease};
const state=(request=0,revision=0,patch={})=>({version:1,type:'music-background-state',
  request,revision,status:'ready',error:'',media:null,...patch});
function fixture(options={}) {
  const listeners=new Set(),sent=[];
  const bridge={subscribe(listener){listeners.add(listener);return ()=>listeners.delete(listener);},
    musicBackground(action,fields){sent.push({action,...fields});return true;}};
  return {listeners,sent,bridge,query:createMusicBackgroundCatalogQuery(bridge,{timeout:100,...options}),
    reply:value=>{for(const listener of [...listeners])listener(value);}};
}

test('background loopback media is exact, typed, bounded, and never an arbitrary endpoint',()=>{
  assert.equal(validMusicBackgroundMedia(media),true);
  for(const extension of ['mp4','webm','jpg','jpeg','png','webp','gif'])
    assert.equal(validMusicBackgroundLoopbackUrl(lease.replace('.png','.'+extension)),true);
  for(const url of [lease+'?secret=x',lease+'#x',lease.replace('54321','65536'),lease.replace('54321','0'),
    lease.replace('54321','054321'),lease.replace('127.0.0.1','localhost'),lease.replace('http:','https:'),
    lease.replace('a'.repeat(64),'A'.repeat(64)),lease.replace('a'.repeat(64),'a'.repeat(63)),
    lease.replace('/media.png','/../media.png'),lease.replace('.png','.svg'),lease.replace('.png','.bmp'),
    lease.replace('127.0.0.1','user@127.0.0.1'),'file:///C:/secret.png','data:image/png;base64,eA=='])
    assert.equal(validMusicBackgroundMedia({...media,url}),false,url);
  assert.equal(validMusicBackgroundMedia({...media,url:lease.replace('.png','.mp4')}),false);
  assert.equal(validMusicBackgroundMedia({...media,url:'https://lume-music.local/music-owned.png'}),true);
});

test('catalog validates exact native fields, IDs, status, page bounds and thumbnail allowlist',()=>{
  assert.equal(validMusicBackgroundCatalogMessage(catalog()),true);
  assert.equal(validMusicBackgroundThumbnailUrl('https://static.lumedesktop.com/lume/covers/one.png'),true);
  assert.equal(validMusicBackgroundThumbnailUrl('https://lume-12345-1444884137.cos.ap-nanjing.myqcloud.com/lume/one.jpg'),true);
  for(const url of ['https://evil.invalid/lume/x.png','https://static.lumedesktop.com/other/x.png',
    'https://static.lumedesktop.com.evil.invalid/lume/x.png','https://static.lumedesktop.com/lume/../x.png',
    'https://static.lumedesktop.com/lume/%2e%2e/x.png','https://static.lumedesktop.com/lume/%2fsecret.png',
    'https://static.lumedesktop.com/lume/%252e%252e/x.png','https://static.lumedesktop.com/lume/x.png#x',
    'https://user@static.lumedesktop.com/lume/x.png','https://static.lumedesktop.com:123/lume/x.png'])
    assert.equal(validMusicBackgroundThumbnailUrl(url),false,url);
  for(const patch of [{revision:1},{request:0},{status:'loading'},{status:'busy',error:'load'},{error:'raw exception'},
    {status:'error',error:''},{total:-1},{total:20001},{selectedId:'../x'},{items:[item(),item()]},
    {total:121,items:Array.from({length:121},(_,i)=>item(`local-${i}`))},{items:[item('con.txt')]},
    {items:[item('a'.repeat(97))]},{items:[item('x',{path:'C:/private.png'})]},
    {items:[item('x',{name:'bad\nname'})]},{items:[item('x',{thumbnailUrl:'https://evil.invalid/a.png'})]}])
    assert.equal(validMusicBackgroundCatalogMessage(catalog(1,patch)),false,JSON.stringify(patch));
  assert.equal(validMusicBackgroundCommand('select',{request:1,revision:0,id:'Local.01'}),true);
  for(const id of ['.','..','x.','COM1','aux.jpg','con','lpt9.txt','x/y','a'.repeat(97)])
    assert.equal(validMusicBackgroundCommand('select',{request:1,revision:0,id}),false,id);
  assert.equal(validMusicBackgroundCommand('catalog',{request:1,offset:20000,limit:120}),true);
  for(const patch of [{request:0},{offset:20001},{limit:0},{limit:121},{revision:0}])
    assert.equal(validMusicBackgroundCommand('catalog',{request:1,offset:0,limit:120,...patch}),false);
});

test('catalog requests are lazy, correlated, bounded and independent of background mutations',async()=>{
  const f=fixture(),background=createMusicBackground({send:f.bridge.musicBackground});
  assert.equal(f.sent.length,0);
  background.receive(state());
  const list=f.query(),selected=background.select('local-1');
  assert.deepEqual(f.sent,[{action:'catalog',request:1,offset:0,limit:120},
    {action:'select',request:1,revision:0,id:'local-1'}]);
  assert.equal(background.receive(catalog()),false);
  f.reply(state(1,1,{media}));assert.equal(f.listeners.size,1);
  f.reply(catalog(1,{status:'busy',items:[],total:0}));assert.equal(f.listeners.size,1);
  f.reply(catalog(99));assert.equal(f.listeners.size,1);
  f.reply(catalog());assert.deepEqual(await list,catalog());assert.equal(f.listeners.size,0);
  assert.equal(background.snapshot().pending,true);
  background.receive(state(1,0,{status:'busy'}));assert.equal(background.snapshot().media,null);
  assert.equal(await background.select('local-2'),false);
  background.receive(state(1,1,{media}));assert.equal(await selected,true);
  assert.equal(background.snapshot().media.id,'music-owned');
  assert.equal(background.receive(state(1,2)),false);
  const failed=background.select('local-2');
  background.receive(state(2,1,{media,status:'error',error:'unsupported'}));
  assert.equal(await failed,false);assert.equal(background.snapshot().media.id,'music-owned');
});

test('catalog abort, replace, errors, send failure, short pages and timeout clean subscriptions',async()=>{
  const f=fixture({timeout:25}),abort=new AbortController();
  const cancelled=assert.rejects(f.query({signal:abort.signal}),/cancelled/);
  abort.abort();await cancelled;assert.equal(f.listeners.size,0);
  const replaced=assert.rejects(f.query(),/cancelled/),next=f.query();
  await replaced;f.reply(catalog(2));assert.equal(f.listeners.size,1);f.reply(catalog(3));await next;
  const failure=assert.rejects(f.query(),/load/);
  f.reply(catalog(4,{status:'error',error:'load',items:[],total:0}));await failure;
  const page=f.query({offset:1,limit:1});
  assert.deepEqual(f.sent.at(-1),{action:'catalog',request:5,offset:1,limit:1});
  f.reply(catalog(5,{items:[],total:1}));assert.equal((await page).items.length,0);
  await assert.rejects(f.query(),/timeout/);assert.equal(f.listeners.size,0);
  await assert.rejects(createMusicBackgroundCatalogQuery({...f.bridge,musicBackground(){throw Error('offline');}})(),/unavailable/);
  assert.equal(f.listeners.size,0);
  const invalid=assert.rejects(f.query({limit:1}),/invalid/);
  f.reply(catalog(7,{total:2,items:[item('one'),item('two')]}));await invalid;
});

test('superseded state and disposed selection do not claim a successful wallpaper apply',async()=>{
  const background=createMusicBackground({send:()=>true});
  background.receive(state());
  const first=background.select('local-1');background.receive(state(0,1,{media}));
  assert.equal(await first,false);
  const second=background.select('local-2');background.dispose();assert.equal(await second,false);
  assert.equal(background.receive(state(2,2,{media})),false);
  const cancelled=createMusicBackground({send:()=>true});cancelled.receive(state(0,1,{media}));
  const third=cancelled.select('local-3');cancelled.receive(state(1,1,{media}));
  assert.equal(await third,false,'unchanged media after native cancellation is not a successful select');
});

const jsdomPath=process.env.LUME_JSDOM_PATH;
const {JSDOM}=jsdomPath?createRequire(import.meta.url)(jsdomPath):{};
const flush=()=>new Promise(resolve=>setImmediate(resolve));
function domFixture(t) {
  const dom=new JSDOM('<button id="opener">Background</button><main id="stage"></main>',
    {url:'https://lume.local/',pretendToBeVisual:true});
  const {document}=dom.window,queries=[],selections=[];
  const view=createMusicWallpaperGallery({document,
    query:fields=>new Promise((resolve,reject)=>queries.push({fields,resolve,reject})),
    onSelect:id=>new Promise((resolve,reject)=>selections.push({id,resolve,reject}))});
  const opener=document.querySelector('#opener');opener.focus();
  const key=(key,shiftKey=false)=>document.activeElement.dispatchEvent(
    new dom.window.KeyboardEvent('keydown',{key,shiftKey,bubbles:true,cancelable:true}));
  t.after(()=>{view.dispose();dom.window.close();});
  return {dom,document,view,queries,selections,opener,key,find:selector=>document.querySelector(selector)};
}

test('gallery traps focus, paginates, tags thumbnails and only closes after confirmed selection',
  {skip:!jsdomPath},async t=>{
  const f=domFixture(t),stage=f.find('#stage');
  assert.equal(f.view.open(),true);assert.equal(f.view.open(),false);
  assert.match(f.find('[role=status]').textContent,/正在读取/);
  assert.equal(f.queries[0].fields.limit,120);
  f.queries[0].resolve(catalog(1,{total:2,items:[item('one',{name:'<script>not markup</script>'})],selectedId:'one'}));
  await flush();
  assert.equal(f.find('script'),null);assert.equal(f.find('[aria-pressed=true]').dataset.wallpaperId,'one');
  const selected=f.find('[data-wallpaper-id=one]'),preview=selected.firstElementChild;
  assert.deepEqual([...selected.children].map(node=>node.className),
    ['MusicWallpaperGalleryPreview','MusicWallpaperGalleryName']);
  for(const selector of ['img','.MusicWallpaperGalleryFallback','.MusicWallpaperGalleryTag','.MusicWallpaperGallerySelected'])
    assert.equal(selected.querySelector(selector).parentElement,preview,selector+' remains inside the preview');
  assert.equal(selected.querySelector('.MusicWallpaperGalleryTag').lastElementChild.textContent,'\u9759\u6001');
  f.find('[data-gallery-more]').focus();f.key('Tab');
  assert.equal(f.document.activeElement,f.find('[data-gallery-refresh]'));
  f.key('Tab',true);assert.equal(f.document.activeElement,f.find('[data-gallery-more]'));
  f.find('[data-gallery-more]').click();assert.equal(f.queries[1].fields.offset,1);
  f.queries[1].resolve(catalog(2,{total:2,items:[item('two',{type:'video'})]}));await flush();
  assert.equal(f.document.querySelectorAll('[data-wallpaper-id]').length,2);
  assert.match(f.find('[data-wallpaper-id=two]').textContent,/动态/);
  f.find('img').dispatchEvent(new f.dom.window.Event('error'));assert.equal(f.find('img').hidden,true);
  f.find('[data-wallpaper-id=two]').click();f.find('[data-wallpaper-id=one]').click();
  assert.equal(f.selections.length,1);assert.equal(f.selections[0].id,'two');
  assert.equal(f.find('[data-gallery-refresh]').disabled,true);assert.ok(f.find('dialog'));
  const pending=f.find('[data-wallpaper-id=two]');
  assert.deepEqual([...pending.children].map(node=>node.className),
    ['MusicWallpaperGalleryPreview','MusicWallpaperGalleryName']);
  assert.equal(pending.querySelector('.MusicWallpaperGalleryPending').parentElement,pending.firstElementChild);
  assert.equal(pending.querySelector('.MusicWallpaperGalleryTag').parentElement,pending.firstElementChild);
  f.selections[0].resolve(true);await flush();
  assert.equal(f.find('dialog'),null);assert.equal(f.document.activeElement,f.opener);
  assert.equal(f.find('#stage'),stage);
});

test('gallery empty, load error, refetch and failed selection keep usable controls',{skip:!jsdomPath},async t=>{
  const f=domFixture(t);f.view.open();
  f.queries[0].reject(Error('private path must not render'));await flush();
  assert.match(f.find('[role=alert]').textContent,/暂时无法读取/);
  assert.equal(f.document.body.textContent.includes('private path'),false);
  f.find('[data-gallery-refresh]').click();f.queries[1].resolve(catalog(2,{items:[],total:0}));await flush();
  assert.match(f.find('[role=status]').textContent,/还没有本地/);
  f.find('[data-gallery-refresh]').click();f.queries[2].resolve(catalog(3));await flush();
  f.find('[data-wallpaper-id]').click();f.selections[0].resolve(false);await flush();
  assert.match(f.find('[role=alert]').textContent,/暂时无法使用/);
  assert.equal(f.find('[data-wallpaper-id]').disabled,false);
  assert.equal(f.find('[data-gallery-refresh]').disabled,false);
});

for(const end of ['escape','close','hide','visibility','pagehide','dispose'])
  test(`gallery ${end} aborts pending catalog and discards a late reply`,{skip:!jsdomPath},async t=>{
    const f=domFixture(t);f.view.open();
    if(end==='escape')f.key('Escape');
    if(end==='close')f.find('[data-gallery-close]').click();
    if(end==='hide')f.view.setVisible(false);
    if(end==='visibility') {
      Object.defineProperty(f.document,'hidden',{configurable:true,value:true});
      f.document.dispatchEvent(new f.dom.window.Event('visibilitychange'));
    }
    if(end==='pagehide')f.dom.window.dispatchEvent(new f.dom.window.Event('pagehide'));
    if(end==='dispose')f.view.dispose();
    assert.equal(f.queries[0].fields.signal.aborted,true);
    f.queries[0].resolve(catalog());await flush();
    assert.equal(f.find('dialog'),null);assert.equal(f.document.activeElement,f.opener);
  });

test('closing and reopening cannot let an old selection close the new gallery',{skip:!jsdomPath},async t=>{
  const f=domFixture(t);f.view.open();f.queries[0].resolve(catalog());await flush();
  f.find('[data-wallpaper-id]').click();f.key('Escape');f.view.open();
  f.selections[0].resolve(true);await flush();assert.ok(f.find('dialog'));
  f.queries[1].resolve(catalog(2));await flush();assert.ok(f.find('[data-wallpaper-id]'));
});
