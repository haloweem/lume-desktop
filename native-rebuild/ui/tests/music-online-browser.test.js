import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile,mkdir} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir,tmpdir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {installMusicPreferencesHarness} from './music-preferences-harness.js';

const ui=path.resolve(fileURLToPath(new URL('..',import.meta.url)));
const runtime=path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
const enabled=process.env.LUME_UI_BROWSER_TESTS==='1';

async function setup(t) {
  const {chromium}=createRequire(import.meta.url)(process.env.LUME_PLAYWRIGHT_DIR||path.join(runtime,'playwright'));
  const server=createServer(async(req,res)=>{
    const pathname=new URL(req.url,'http://localhost').pathname;
    if(pathname==='/tests/music-online-fixture.html') {
      res.setHeader('Content-Type','text/html; charset=utf-8');
      return res.end('<!doctype html><html lang="zh-CN"><meta charset="utf-8"><link rel="stylesheet" href="/music.css"><style>html,body,main{margin:0;width:100%;height:100%;background:#171717}*{box-sizing:border-box}</style><main id="fixture"></main></html>');
    }
    if(/^\/[a-f0-9]{64}\/media\.png$/.test(pathname)) {
      res.setHeader('Content-Type','image/png');
      return res.end(Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=','base64'));
    }
    const file=path.resolve(ui,'.'+pathname);
    if(!file.startsWith(ui+path.sep))return res.writeHead(403).end();
    try {
      res.setHeader('Content-Type',({'.html':'text/html; charset=utf-8','.js':'text/javascript','.css':'text/css','.webp':'image/webp'})[path.extname(file)]||'application/octet-stream');
      res.end(await readFile(file));
    } catch {res.writeHead(404).end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser;
  t.after(async()=>{await browser?.close();await new Promise(resolve=>server.close(resolve));});
  browser=await chromium.launch({headless:true,channel:'msedge'});
  const origin=`http://127.0.0.1:${server.address().port}`;
  const errors=[],external=[];
  const context=await browser.newContext();
  await context.route('**/*',route=>{
    const url=route.request().url();
    if(url.startsWith(origin+'/')||url.startsWith('data:'))return route.continue();
    external.push(url);return route.abort();
  });
  context.on('page',page=>page.on('pageerror',error=>errors.push(error.message)));
  return {context,origin,errors,external};
}

async function mount(page,origin,width) {
  await page.setViewportSize({width,height:900});
  await page.goto(origin+'/tests/music-online-fixture.html');
  await page.evaluate(async()=>{
    const [{mountMusicView},{initialMusicState},{createMusicOnline},{createMusicProviderAccount},{DEFAULT_MUSIC_PREFERENCES}]=await Promise.all([
      import('/music-view.js'),import('/music.js'),import('/music-online.js'),import('/music-provider-account.js'),import('/music-preference-values.js')]);
    const commands=[],localCommands=[],nativeCommands=[],imports=[];
    const data=()=>({tracks:[],playlists:[],recommendations:[],more:false});
    const song=(provider='netease')=>({id:provider+':track',source:'online:'+provider+':track',
      title:'Fixture song',artist:'Fixture artist',album:'Fixture album',artwork:'',duration:180});
    const collection=(id='old-list')=>({id,providerId:'netease',type:'playlist',title:id,
      artwork:'',description:'',owned:true});
    let view;
    const localState={tracks:[{id:'local-123',source:'',title:'Local fixture',
      artist:'Local artist',album:'Local album',artwork:'',duration:120}],total:5,offset:0,limit:2,loaded:true,error:'',pending:false};
    const localLibrary={get state(){return localState;},
      load(){localCommands.push(['load']);return true;},
      page(offset){localCommands.push(['page',offset]);localState.offset=offset;update();return true;},
      play(id){localCommands.push(['play',id]);return true;},
      enqueue(id,insertNext){localCommands.push(['enqueue',id,insertNext]);return true;},
      remove(id){localCommands.push(['remove',id]);localState.tracks=[];localState.total--;update();return true;}};
    const library=()=>({...online.snapshot(),localTracks:localState.tracks,localTotal:localState.total,
      localOffset:localState.offset,localMore:localState.offset+localState.limit<localState.total,
      localError:localState.error,localLoading:localState.pending});
    const update=()=>view?.updateLibrary(library());
    const online=createMusicOnline({now:()=>1,send:(action,fields)=>{commands.push({action,...fields});return true;},onChange:update});
    const providerAccount=createMusicProviderAccount({now:()=>1,send:()=>true});
    const snapshot={...initialMusicState(),session:4,pageOpen:true,pageVisible:true};
    const owner=()=>({update(){},dispose(){}});
    view=mountMusicView(document.querySelector('main'),{snapshot,library:library(),online,providerAccount,localLibrary,
      preferences:{loaded:true,pending:false,preferences:DEFAULT_MUSIC_PREFERENCES},
      createBackground:owner,createVisualizer:owner,createEffects:owner,createMaterial:owner,
      send:(action,fields)=>{nativeCommands.push({action,...fields});return true;},
      onImport:files=>{imports.push(files.map(file=>file.name));return true;}});
    online.setVisible(true);
    window.fixture={view,online,commands,localCommands,nativeCommands,imports,localState,update,song,collection,
      accounts(entries=[['netease','fixture-user']]) {
        const accounts=['netease','qq','kugou'].map(provider=>{
          const entry=entries.find(value=>value[0]===provider);
          return {provider,request:0,status:entry?'signed-in':'signed-out',phase:'',userId:entry?.[1]||'',displayName:entry?.[1]||'',qrImage:'',qrText:'',error:''};
        });
        online.accounts({accounts:accounts.map(({provider,userId,status})=>({provider,userId,status}))});
        providerAccount.receive({version:1,type:'music-provider-account-state',revision:Date.now(),accounts});
      },
      reply(patch={}) {
        const {request,provider,action}=commands.at(-1);
        return online.receive({version:1,type:'music-online-state',request,provider,action,status:'ready',error:'',
          data:{...data(),tracks:[song(provider)],playlists:[collection()],
            likedIds:Object.keys(online.snapshot().liked).filter(id=>id.startsWith(provider+':')&&online.snapshot().liked[id])},...patch});
      },
      visibility(visible){view.setVisible(visible);online.setVisible(visible);},
    };
  });
}

async function checkTopbar(page,width,evidence) {
  const geometry=await page.locator('.MusicTopbar').evaluate(node=>({
    children:[...node.children].map(child=>child.getBoundingClientRect().toJSON()),
  }));
  assert.ok(geometry.children.every(rect=>rect.x>=0&&rect.right<=width),JSON.stringify(geometry));
  for(let i=1;i<geometry.children.length;i++)assert.ok(geometry.children[i-1].right<=geometry.children[i].x);
  await page.screenshot({path:path.join(evidence,`music-online-${width}.png`)});
}

async function openPlatforms(page) {
  if(!await page.locator('[data-music-player-panel]').count())
    await page.locator('[data-music-settings-toggle]').click();
  await page.locator('[data-music-panel-tab="account"]').click();
}

test('真实浏览器 DOM：活动账户恢复、写锁、歌单刷新、弹窗失效与本地库接线',
  {skip:!enabled,timeout:120000},async t=>{
  const {context,origin,errors,external}=await setup(t);
  const evidence=path.join(tmpdir(),'lume-music-online-'+Date.now());
  await mkdir(evidence,{recursive:true});
  for(const width of [1440,390]) {
    const page=await context.newPage();
    await mount(page,origin,width);
    const active=page.locator('[data-music-provider-active]');
    assert.equal(await page.locator('[data-music-provider-select]').count(),0);
    await page.locator('[data-music-search]').fill('draft');
    await page.evaluate(()=>{window.searchNode=document.querySelector('[data-music-search]');fixture.accounts();fixture.reply();});
    assert.equal(await page.locator('.MusicTopbar select, .MusicTopbar [data-music-provider-open]').count(),0);
    await openPlatforms(page);
    assert.equal(await active.getAttribute('data-provider'),'netease');
    assert.equal(await active.isVisible(),true);
    assert.equal(await page.locator('[data-music-provider-select], .MusicProviderAccountConnect').count(),0);
    await page.keyboard.press('Escape');
    await page.locator('[data-music-search]').fill('retained draft');
    await page.evaluate(()=>fixture.accounts([['netease','fixture-user'],['qq','qq-fixture']]));
    await openPlatforms(page);
    assert.equal(await active.count(),1);
    assert.equal(await page.locator('[data-music-search]').inputValue(),'retained draft');
    assert.equal(await page.evaluate(()=>searchNode===document.querySelector('[data-music-search]')),true);
    await page.evaluate(()=>fixture.accounts([['qq','qq-fixture']]));
    assert.equal(await active.getAttribute('data-provider'),'qq');
    assert.equal(await active.locator('[data-music-provider-active-name]').innerText(),'qq-fixture');
    await page.evaluate(()=>fixture.reply());
    assert.equal(await page.locator('[data-music-track-id="qq:track"]').count(),1);
    await page.evaluate(()=>fixture.accounts());
    assert.equal(await active.getAttribute('data-provider'),'netease','restoration selects the remaining signed-in account');
    assert.equal(await page.locator('[data-music-provider-select]').count(),0);
    await page.keyboard.press('Escape');
    await page.evaluate(()=>fixture.reply());
    await page.evaluate(()=>{
      const {source,...track}=fixture.song();
      fixture.view.updateLibrary({...fixture.online.snapshot(),tracks:[track]});
    });
    assert.equal(await page.locator('.MusicTrackRow [data-music-action="online-play"]').isDisabled(),false);
    if(width<640) {
      await checkTopbar(page,width,evidence);
      await page.evaluate(()=>{fixture.view.dispose();fixture.online.dispose();});
      await page.close();
      continue;
    }
    await page.locator('[data-music-search]').fill('Fixture');
    await page.locator('[data-music-search]').press('Enter');
    await page.evaluate(()=>fixture.reply());
    await page.locator('[data-music-action="create-playlist"]').click();
    await page.locator('.MusicCollectionDialog input').fill('new-list');
    await page.locator('.MusicCollectionDialog button[type="submit"]').click();
    assert.equal(await page.locator('.MusicCollectionDialog').count(),0);
    assert.equal(await page.locator('[data-music-action="create-playlist"]').isDisabled(),true);
    const count=await page.evaluate(()=>fixture.commands.length);
    await page.locator('[data-music-nav="favorites"]').click();
    await page.locator('[data-music-action="clear-search"]').click();
    assert.equal(await page.evaluate(()=>fixture.commands.length),count);
    assert.equal(await page.locator('[data-music-search]').inputValue(),'Fixture');
    assert.equal(await page.locator('.MusicSectionHeader h1').innerText(),'搜索结果');
    await page.evaluate(()=>fixture.reply());
    assert.equal(await page.evaluate(()=>fixture.commands.at(-1).action),'home');
    await page.evaluate(()=>fixture.reply({data:{tracks:[],recommendations:[],playlists:[fixture.collection('new-list')],more:false}}));
    assert.equal(await page.evaluate(()=>fixture.commands.at(-1).action),'search');
    await page.evaluate(()=>fixture.reply());
    assert.equal(await page.locator('.MusicPlaylistItem').innerText(),'new-list');
    await page.locator('.MusicTrackActions [data-music-action="like"]').click();
    assert.equal(await page.evaluate(()=>fixture.commands.at(-1).value),true);
    await page.evaluate(()=>{fixture.reply({data:{tracks:[],playlists:[],recommendations:[],more:false,liked:true}});fixture.reply();});
    assert.equal(await page.locator('.MusicTrackActions [data-music-action="like"]').getAttribute('aria-pressed'),'true');
    await page.locator('.MusicTrackActions [data-music-action="like"]').click();
    assert.equal(await page.evaluate(()=>fixture.commands.at(-1).value),false);
    await page.evaluate(()=>{fixture.reply({status:'error',error:'network'});});
    assert.match(await page.locator('.MusicContent [role="status"]').innerText(),/网络/);
    await page.locator('[data-music-action="online-retry"]').click();
    assert.equal(await page.evaluate(()=>fixture.commands.at(-1).action),'search','重试不重放写请求');
    await page.evaluate(()=>fixture.reply());
    await page.locator('.MusicTrackActions [data-music-action="add-to-playlist"]').click();
    const menu=page.getByRole('menu',{name:'添加到歌单'});
    assert.equal(await page.locator('.MusicCollectionDialog').count(),0,'add no longer opens the create/select dialog');
    assert.equal(await menu.locator('select').count(),0);
    assert.equal(await menu.locator('[data-playlist-song]').innerText(),'Fixture song');
    const menuGeometry=await menu.evaluate(node=>{
      const rect=node.getBoundingClientRect(),css=getComputedStyle(node);
      const anchor=document.querySelector('.MusicTrackActions [data-music-action="add-to-playlist"]').getBoundingClientRect();
      return {width:rect.width,left:rect.left,anchorRight:anchor.right,background:css.backgroundColor,
        blur:css.backdropFilter,fits:rect.left>=8&&rect.right<=innerWidth-8&&rect.top>=8&&rect.bottom<=innerHeight-8};
    });
    assert.equal(menuGeometry.width,336);
    assert.equal(menuGeometry.left,Math.min(Math.max(8,menuGeometry.anchorRight+16-336),width-344));
    assert.equal(menuGeometry.background,'rgba(0, 0, 0, 0.2)');
    assert.equal(menuGeometry.blur,'blur(24px)');
    assert.equal(menuGeometry.fits,true);
    await page.screenshot({path:path.join(evidence,`add-menu-${width}.png`)});
    await menu.locator('[data-playlist-id="new-list"]').click();
    await menu.waitFor({state:'detached'});
    assert.deepEqual(await page.evaluate(()=>Object.fromEntries(Object.entries(fixture.commands.at(-1))
      .filter(([key])=>['action','id','trackId'].includes(key)))),
      {action:'addToPlaylist',id:'new-list',trackId:'netease:track'});
    await page.evaluate(()=>{fixture.reply({data:{tracks:[],playlists:[],recommendations:[],more:false}});fixture.reply();});
    await page.locator('.MusicTrackActions [data-music-action="add-to-playlist"]').click();
    await menu.locator('[data-playlist-create]').click();
    assert.equal(await menu.count(),0);
    assert.equal(await page.locator('.MusicCollectionDialog input').isVisible(),true,'new playlist remains its original modal');
    await page.locator('.MusicCollectionDialog button').filter({hasText:'取消'}).click();
    await page.locator('.MusicTrackActions [data-music-action="add-to-playlist"]').click();
    await page.evaluate(()=>{
      window.staleSelection=document.querySelector('[data-playlist-id="new-list"]');
      window.writeCount=fixture.commands.filter(command=>command.action==='addToPlaylist').length;
      fixture.accounts([['netease','replacement-user']]);
      staleSelection.click();
    });
    assert.equal(await menu.count(),0);
    assert.equal(await page.evaluate(()=>fixture.commands.filter(command=>command.action==='addToPlaylist').length===writeCount),true);
    assert.equal(await page.evaluate(()=>fixture.commands.at(-1).action),'home');
    assert.equal(await page.locator('[data-music-search]').inputValue(),'');
    await page.evaluate(()=>fixture.reply());
    await page.locator('[data-music-action="create-playlist"]').click();
    await page.evaluate(()=>{fixture.visibility(false);fixture.visibility(false);fixture.visibility(false);});
    assert.equal(await page.locator('.MusicCollectionDialog').count(),0);
    assert.equal(await page.evaluate(()=>fixture.commands.at(-2).action),'home');
    await page.evaluate(()=>{fixture.visibility(true);fixture.reply();});
    await page.locator('[data-music-nav="local"]').click();
    assert.deepEqual(await page.evaluate(()=>fixture.localCommands),[['load']]);
    const onlineCount=await page.evaluate(()=>fixture.commands.length);
    await page.locator('.MusicTrackRow [data-music-action="local-enqueue"]').click();
    await page.locator('.MusicTrackRow [data-music-action="local-next"]').click();
    await page.locator('.MusicTrackRow [data-music-action="local-play"]').click();
    assert.equal(await page.locator('[data-music-root]').getAttribute('data-screen'),'player');
    await page.locator('[data-music-action="back-home"]').click();
    assert.deepEqual(await page.evaluate(()=>fixture.localCommands.slice(-3)),[
      ['enqueue','local-123',false],['enqueue','local-123',true],['play','local-123']]);
    assert.equal(await page.evaluate(()=>fixture.nativeCommands.length),0);
    await page.locator('[data-music-action="local-more"]').click();
    assert.equal(await page.locator('.MusicPagination').innerText(),'第 2 页 / 共 5 首');
    await page.locator('[data-music-action="local-previous"]').click();
    assert.equal(await page.locator('[data-music-action="local-previous"]').isDisabled(),true);
    await page.evaluate(()=>{fixture.localState.tracks[0].available=false;fixture.update();});
    assert.equal(await page.locator('.MusicTrackRow [data-music-action="local-play"]').isDisabled(),true);
    assert.equal(await page.locator('.MusicTrackRow [data-music-action="local-enqueue"]').isDisabled(),true);
    assert.equal(await page.locator('.MusicTrackRow [data-music-action="local-next"]').isDisabled(),true);
    assert.equal(await page.locator('.MusicTrackRow [data-music-action="local-remove"]').isDisabled(),false);
    await page.locator('.MusicTrackRow [data-music-action="local-remove"]').click();
    assert.equal(await page.locator('.MusicTrackRow').count(),0);
    await page.evaluate(()=>{fixture.localState.error='storage';fixture.update();});
    await page.locator('[data-music-action="local-retry"]').click();
    assert.deepEqual(await page.evaluate(()=>fixture.localCommands.at(-1)),['load']);
    await page.locator('[data-music-file-input]').setInputFiles({name:'fixture.mp3',mimeType:'audio/mpeg',buffer:Buffer.from('fixture')});
    assert.deepEqual(await page.evaluate(()=>fixture.imports),[['fixture.mp3']]);
    assert.equal(await page.evaluate(()=>fixture.commands.length),onlineCount);
    await page.evaluate(()=>{fixture.localState.error='';fixture.update();});
    await checkTopbar(page,width,evidence);
    await page.evaluate(()=>{fixture.view.dispose();fixture.online.dispose();});
    await page.close();
  }
  assert.deepEqual(errors,[]);
  assert.deepEqual(external,[]);
  console.log(`Online fixture screenshots: ${evidence}`);
});

test('A4 生产菜单：可写列表过滤、拒绝重试、滚动翻转、隐藏与 dispose 阻断旧按钮',
  {skip:!enabled,timeout:120000},async t=>{
  const {context,origin,errors,external}=await setup(t);
  for(const width of [1440,900]) {
    const page=await context.newPage();
    await mount(page,origin,width);
    await page.evaluate(()=>{
      fixture.accounts();
      fixture.reply({data:{tracks:[fixture.song()],recommendations:[],more:false,playlists:[
        {...fixture.collection('owned'),title:'<script>Fixture</script>',description:'Fixture description'},
        {...fixture.collection('subscribed'),owned:false},
        {...fixture.collection('other-provider'),providerId:'qq'},
      ]}});
    });
    const add=page.locator('.MusicTrackActions [data-music-action="add-to-playlist"]');
    const menu=page.getByRole('menu',{name:'添加到歌单'});
    await add.click();
    assert.deepEqual(await menu.locator('[data-playlist-id]').allTextContents(),['<script>Fixture</script>Fixture description']);
    assert.equal(await menu.locator('script').count(),0);
    await menu.locator('[data-playlist-create]').focus();await page.keyboard.press('End');
    assert.equal(await page.evaluate(()=>document.activeElement.dataset.playlistId),'owned');
    await page.keyboard.press('Escape');
    assert.equal(await menu.count(),0);
    assert.equal(await add.evaluate(node=>node===document.activeElement),true);
    await add.click();
    await page.evaluate(()=>{
      fixture.originalRequest=fixture.online.request;
      fixture.online.request=()=>false;
    });
    await menu.locator('[data-playlist-id="owned"]').click();
    await menu.getByRole('alert').waitFor({state:'visible'});
    assert.equal(await menu.getAttribute('aria-busy'),'false','refused request leaves no UI pending');
    assert.equal(await menu.locator('[data-playlist-id="owned"]').isEnabled(),true);
    await page.evaluate(()=>fixture.online.request=fixture.originalRequest);
    await menu.locator('[data-playlist-id="owned"]').click();
    await menu.waitFor({state:'detached'});
    assert.equal(await page.evaluate(()=>fixture.commands.at(-1).action),'addToPlaylist');
    await page.evaluate(()=>{
      fixture.reply({data:{tracks:[],playlists:[],recommendations:[],more:false}});
      fixture.reply({data:{tracks:[fixture.song()],playlists:[fixture.collection('owned')],
        recommendations:[],more:false}});
    });
    await add.click();
    const afterWrite=await page.evaluate(()=>({
      ids:[...document.querySelectorAll('[data-playlist-id]')].map(node=>node.dataset.playlistId),
      library:fixture.online.snapshot(),
      commands:fixture.commands.slice(-3),
    }));
    assert.ok(afterWrite.ids.includes('owned'),JSON.stringify(afterWrite));
    await page.evaluate(()=>{
      window.hiddenSelection=document.querySelector('[data-playlist-id="owned"]');
      window.beforeHide=fixture.commands.filter(command=>command.action==='addToPlaylist').length;
      fixture.visibility(false);hiddenSelection.click();
    });
    assert.equal(await menu.count(),0);
    assert.equal(await page.evaluate(()=>fixture.commands.filter(command=>command.action==='addToPlaylist').length),await page.evaluate(()=>beforeHide));
    await page.evaluate(()=>{fixture.visibility(true);fixture.reply();});
    await add.click();
    await page.locator('[data-music-nav="local"]').click();
    assert.equal(await menu.count(),0,'navigation dismisses the portal');
    await page.locator('[data-music-nav="recommendations"]').click();
    await page.evaluate(()=>fixture.reply({data:{tracks:[fixture.song()],recommendations:[],more:false,
      playlists:Array.from({length:20},(_,index)=>({...fixture.collection('list-'+index),description:'Fixture'}))}}));
    await page.setViewportSize({width,height:440});
    await add.click();
    const bounds=await menu.evaluate(node=>{
      const rect=node.getBoundingClientRect();
      const anchor=document.querySelector('.MusicTrackActions [data-music-action="add-to-playlist"]').getBoundingClientRect();
      const list=node.querySelector('.MusicPlaylistSelectionList');
      return {fits:rect.left>=8&&rect.right<=innerWidth-8&&rect.top>=8&&rect.bottom<=innerHeight-8,
        above:rect.bottom<=anchor.top-8,scrolls:list.scrollHeight>list.clientHeight};
    });
    assert.equal(bounds.fits,true);
    assert.equal(bounds.scrolls,true);
    await page.evaluate(()=>{
      window.disposedSelection=document.querySelector('[data-playlist-id]');
      window.beforeDispose=fixture.commands.length;
      fixture.view.dispose();disposedSelection.click();
    });
    assert.equal(await menu.count(),0);
    assert.equal(await page.evaluate(()=>fixture.commands.length),await page.evaluate(()=>beforeDispose));
    await page.evaluate(()=>fixture.online.dispose());
    await page.close();
  }
  assert.deepEqual(errors,[]);
  assert.deepEqual(external,[]);
});

test('本地封面租约：无 source 原生快照按 ID 投影，曲库更新及隐藏撤销不重置时钟',
  {skip:!enabled,timeout:120000},async t=>{
  const {context,origin,errors,external}=await setup(t),page=await context.newPage();
  await page.setViewportSize({width:1440,height:900});
  await page.goto(origin+'/tests/music-online-fixture.html');
  await page.evaluate(async()=>{
    const {mountMusicView}=await import('/music-view.js');
    const {initialMusicState}=await import('/music.js');
    const song=id=>({id,title:id,artist:'Fixture',album:'',duration:180,artwork:'music-artwork:'+id});
    const current=song('local-123'),queue=[current,song('local-124'),song('qq:123')];
    const snapshot={...initialMusicState(),session:9,pageOpen:true,pageVisible:true,playing:true,current,queue,
      timeline:{position:37,duration:180},capabilities:{canSeek:true,canToggle:true}};
    const calls=[];
    const owner=()=>({update(){},dispose(){}});
    const view=mountMusicView(document.querySelector('main'),{snapshot,library:{localTracks:[]},
      viewState:{screen:'player',panel:'queue'},createBackground:owner,createEffects:owner,createMaterial:owner,
      createVisualizer:()=>({update(...args){calls.push(args);},dispose(){}})});
    const url=letter=>location.origin+'/'+letter.repeat(64)+'/media.png';
    window.artworkFixture={view,calls,url,project(value){
      view.updateLibrary({localTracks:value===null?[]:queue.map(track=>({...track,artwork:value}))});
    },remember(){
      this.nodes=[...document.querySelectorAll('[data-music-stage], [data-music-progress], .MusicTrackRow, .MusicPanelCover')];
      this.count=calls.length;this.snapshot=JSON.stringify(view.snapshot());
    },unchanged(){
      return this.nodes.every(node=>node.isConnected)&&calls.length===this.count&&JSON.stringify(view.snapshot())===this.snapshot;
    }};
    artworkFixture.remember();
  });
  const player=page.locator('.MusicPlayerArtwork img');
  const localQueue=page.locator('[data-music-panel="queue"] [data-music-track-id^="local-"] img');
  assert.equal(await player.count(),0);
  await page.evaluate(()=>artworkFixture.project(artworkFixture.url('a')));
  await page.waitForFunction(()=>[...document.querySelectorAll('[data-music-image]')].length===3&&
    [...document.querySelectorAll('[data-music-image]')].every(image=>image.complete&&image.naturalWidth===1));
  assert.equal(await localQueue.count(),2);
  assert.equal(await page.locator('[data-music-track-id="qq:123"] img').count(),0,'曲库不能覆盖在线歌曲');
  assert.equal(await page.evaluate(()=>artworkFixture.unchanged()),true);
  const image=await player.elementHandle();
  await page.evaluate(()=>artworkFixture.project(artworkFixture.url('a')));
  assert.equal(await image.evaluate(node=>node===document.querySelector('.MusicPlayerArtwork img')),true);
  await page.evaluate(()=>artworkFixture.project(artworkFixture.url('b')));
  assert.equal(await player.getAttribute('src'),origin+'/'+'b'.repeat(64)+'/media.png');
  assert.equal(await page.evaluate(()=>artworkFixture.unchanged()),true);
  await page.evaluate(()=>{artworkFixture.view.setVisible(false);artworkFixture.remember();artworkFixture.project('');});
  assert.equal(await page.locator('[data-music-image]').count(),0);
  assert.equal(await page.evaluate(()=>artworkFixture.unchanged()),true);
  await page.evaluate(()=>{artworkFixture.view.setVisible(true);artworkFixture.remember();artworkFixture.project(artworkFixture.url('c'));});
  await page.waitForFunction(()=>document.querySelector('.MusicPlayerArtwork img')?.naturalWidth===1);
  assert.equal(await page.evaluate(()=>artworkFixture.unchanged()),true);
  await page.evaluate(()=>artworkFixture.project(null));
  assert.equal(await page.locator('[data-music-image]').count(),0,'移出当前曲库投影也撤销旧租约');
  await page.evaluate(()=>artworkFixture.project('http://127.0.0.1:1234/not-a-lease.png'));
  assert.equal(await page.locator('[data-music-image]').count(),0,'仍通过现有 safeImage 边界');
  await page.locator('[data-music-panel="queue"] [data-music-action="close-panel"]').click();
  await page.locator('[data-music-action="lyrics"]').click();
  await page.evaluate(()=>{artworkFixture.remember();artworkFixture.project(artworkFixture.url('d'));});
  await page.waitForFunction(()=>document.querySelector('.MusicPanelCover img')?.naturalWidth===1);
  assert.equal(await page.evaluate(()=>artworkFixture.unchanged()),true);
  await page.locator('[data-music-panel-tab="cover"]').click();
  assert.equal(await page.locator('.MusicPanelCover img').getAttribute('src'),origin+'/'+'d'.repeat(64)+'/media.png');
  await page.evaluate(()=>{artworkFixture.remember();artworkFixture.project('');});
  assert.equal(await page.locator('.MusicPanelCover img, .MusicPlayerArtwork img').count(),0);
  assert.equal(await page.evaluate(()=>artworkFixture.unchanged()),true);
  await page.evaluate(()=>artworkFixture.view.dispose());
  assert.deepEqual(errors,[]);
  assert.deepEqual(external,[]);
});

test('桌面 DPI 与原版缩放下底栏不重叠，390 仅作窄窗口保险检查',
  {skip:!enabled,timeout:120000},async t=>{
  const {context,origin,errors,external}=await setup(t);
  const page=await context.newPage();
  const session=await context.newCDPSession(page);
  await mount(page,origin,390);
  await page.evaluate(()=>{
    const track=fixture.localState.tracks[0];
    fixture.view.update({...fixture.view.snapshot(),current:track,queue:[track],
      capabilities:{canPrevious:true,canNext:true,canToggle:true,canSeek:true},
      timeline:{position:15,duration:120}});
  });
  await page.locator('[data-music-action="open-player"]').click();
  const evidence=path.join(tmpdir(),'lume-music-player-controls-'+Date.now());
  await mkdir(evidence,{recursive:true});
  // Mirrors webview_page_window.cpp Layout / CalculateOriginalZoom, without mobile emulation.
  const sizes=[1.25,1.5,2].flatMap(dpi=>[[900,700],[1440,900],[1920,1080]].map(([pixels,heightPixels])=>{
    const zoom=Math.max(0.5,Math.min(2,Math.round(pixels/dpi)/1536,Math.round(heightPixels/dpi)/864));
    return {width:Math.round(pixels/dpi/zoom),height:Math.round(heightPixels/dpi/zoom),deviceScaleFactor:dpi*zoom};
  })).concat([390,900,901,1440].map(width=>({width,height:844,deviceScaleFactor:2})));
  for(const {width,height,deviceScaleFactor} of sizes) {
    await session.send('Emulation.setDeviceMetricsOverride',{width,height,deviceScaleFactor,mobile:false});
    const geometry=await page.locator('.MusicPlayerBar').evaluate(bar=>{
      const rect=node=>node.getBoundingClientRect().toJSON();
      const nodes=[...bar.querySelectorAll('.MusicPlayerArtwork, button, [data-music-volume], [data-music-quality]')]
        .filter(node=>node.getClientRects().length&&getComputedStyle(node).visibility!=='hidden');
      return {dpr:devicePixelRatio,bar:rect(bar),items:nodes.map(node=>({id:node.dataset.musicAction||
        (node.matches('input')?'volume':'artwork'),...rect(node)}))};
    });
    assert.ok(Math.abs(geometry.dpr-deviceScaleFactor)<0.00001);
    for(const item of geometry.items) {
      assert.ok(item.x>=geometry.bar.x&&item.right<=geometry.bar.right,`${width}: ${item.id} escapes bar`);
      for(const other of geometry.items.filter(value=>value!==item))
        assert.ok(item.right<=other.x||other.right<=item.x||item.bottom<=other.y||other.bottom<=item.y,
          `${width}: ${item.id} overlaps ${other.id}`);
    }
    assert.equal(await page.locator('[data-music-volume]').isVisible(),false);
    assert.equal(await page.locator('[data-music-volume-trigger]').isVisible(),true);
    if(width===390)await page.screenshot({path:path.join(evidence,'390-2x.png')});
  }
  await page.locator('[data-music-volume-trigger]').click();
  assert.equal(await page.locator('[data-music-volume]').isVisible(),true);
  await page.locator('[data-music-volume]').fill('0.4');
  assert.deepEqual(await page.evaluate(()=>fixture.nativeCommands.at(-1)),{action:'setVolume',value:0.4,session:4});
  await page.locator('[data-music-volume-trigger]').click();
  assert.equal(await page.locator('[data-music-volume]').isVisible(),false);
  assert.deepEqual(await page.evaluate(()=>fixture.nativeCommands.at(-1)),{action:'setVolume',value:0.4,session:4});
  await page.evaluate(()=>{fixture.view.dispose();fixture.online.dispose();});
  assert.deepEqual(errors,[]);
  assert.deepEqual(external,[]);
  console.log(`Player controls screenshot: ${evidence}`);
});

test('原版桌面控件：独立设置、账户平台、沉浸/点击音量/歌单与全屏 Esc 优先级',
  {skip:!enabled,timeout:120000},async t=>{
  const {context,origin,errors,external}=await setup(t),page=await context.newPage();
  await page.setViewportSize({width:1536,height:864});
  await page.goto(origin+'/tests/music-online-fixture.html');
  await page.evaluate(async()=>{
    const [{mountMusicView},{initialMusicState},{createMusicOnline},{DEFAULT_MUSIC_PREFERENCES}]=await Promise.all([
      import('/music-view.js'),import('/music.js'),import('/music-online.js'),import('/music-preference-values.js')]);
    const commands=[],native=[],updates=[];
    const song=(id='netease:123')=>({id,title:id,artist:'Fixture',album:'Fixture album',
      source:'online:'+id,artwork:'',duration:180});
    const {source,...current}=song(),local={...current,id:'local-123',title:'Local fixture'};
    let view,fullscreen=false,windowToggles=0;
    const online=createMusicOnline({now:()=>1,send:(action,fields)=>{commands.push({action,...fields});return true;},
      onChange:value=>view?.updateLibrary(value)});
    const owner=()=>({update(){},dispose(){}});
    view=mountMusicView(document.querySelector('main'),{
      snapshot:{...initialMusicState(),session:12,current,queue:[current,local],pageOpen:true,pageVisible:true,
        timeline:{position:18,duration:180},capabilities:{canPrevious:true,canNext:true,canToggle:true,canSeek:true}},
      preferences:{loaded:true,pending:false,preferences:DEFAULT_MUSIC_PREFERENCES},online,library:online.snapshot(),
      createVisualizer:()=>({update(...args){updates.push(args);},dispose(){}}),createEffects:owner,createMaterial:owner,
      onToggleFullscreen:()=>{windowToggles++;},
      onExitFullscreen:()=>{if(!fullscreen)return false;fullscreen=false;return true;},
      send:(action,fields)=>{native.push({action,...fields});return true;}});
    online.setVisible(true);
    online.accounts({accounts:[{provider:'netease',userId:'fixture',status:'signed-in'}]});
    window.chromeFixture={view,online,commands,native,updates,song,
      get fullscreen(){return fullscreen;},get windowToggles(){return windowToggles;},
      setFullscreen(value){fullscreen=value;},reply(){
      const {action,request,provider}=commands.at(-1);
      return online.receive({version:1,type:'music-online-state',action,request,provider,status:'ready',error:'',
        data:{tracks:[song()],playlists:[{id:'list-123',providerId:'netease',type:'playlist',title:'Fixture playlist',
          artwork:'',description:'',owned:true}],recommendations:[],more:false,
          likedIds:Object.keys(online.snapshot().liked).filter(id=>id.startsWith(provider+':')&&online.snapshot().liked[id])}});
    }};
    chromeFixture.reply();
  });
  assert.equal(await page.locator('.MusicTopbar select, .MusicTopbar [data-music-provider-open]').count(),0);
  const settings=page.locator('[data-music-settings-toggle]');
  assert.deepEqual(await settings.evaluate(node=>{
    const box=node.getBoundingClientRect();return {width:box.width,height:box.height,right:innerWidth-box.right,bottom:innerHeight-box.bottom};
  }),{width:48,height:48,right:32,bottom:32});
  await openPlatforms(page);
  assert.equal(await page.locator('[data-music-panel-content="account"] [data-music-provider-active]').count(),1);
  assert.equal(await page.locator('[data-music-provider-select], .MusicProviderAccountConnect').count(),0);
  await page.keyboard.press('Escape');
  await page.locator('[data-music-action="open-player"]').click();
  await page.waitForFunction(()=>document.querySelector('[data-music-background]')?.dataset.musicBackgroundState==='ready');
  await page.evaluate(()=>{
    window.chromeNodes=[document.querySelector('[data-music-stage]'),document.querySelector('[data-music-progress]')];
    window.clockCount=chromeFixture.updates.length;
  });
  assert.equal(await page.locator('.MusicPlayerBar [data-music-settings-toggle]').count(),0);
  assert.deepEqual(await page.locator('.MusicPlayerActions [data-music-action]').evaluateAll(nodes=>
    nodes.map(node=>node.dataset.musicAction)),['queue','immersive','volume']);
  assert.equal(await page.locator('[data-music-quality-trigger]').innerText(),'音质');
  assert.equal(await page.locator('[data-music-quality-trigger]').isEnabled(),true);
  assert.equal(await page.locator('[data-music-quality-option]').count(),7,'NetEase exposes its seven provider-specific qualities');
  const progress=page.locator('[data-music-progress]');
  const progressPixels=async()=>{
    await page.waitForTimeout(180);
    const image=(await page.locator('.MusicProgressLine').screenshot({animations:'disabled'})).toString('base64');
    return page.evaluate(async source=>{
      const image=new Image();image.src='data:image/png;base64,'+source;await image.decode();
      const canvas=document.createElement('canvas');canvas.width=image.width;canvas.height=image.height;
      const context=canvas.getContext('2d');context.drawImage(image,0,0);
      const {data}=context.getImageData(0,0,canvas.width,canvas.height);
      let white=0;for(let i=0;i<data.length;i+=4)if(data[i]>245&&data[i+1]>245&&data[i+2]>245)white++;
      return white;
    },image);
  };
  await page.mouse.move(900,250);
  const idleProgress=await progressPixels();
  await progress.hover();
  const hoverProgress=await progressPixels();
  assert.ok(hoverProgress>idleProgress+20,`悬停显示原版 12px 白色滑块: ${idleProgress} -> ${hoverProgress}`);
  await page.mouse.move(900,250);
  assert.equal(await progressPixels(),idleProgress,'离开进度区域后空闲滑块隐藏');
  await page.locator('[data-music-volume-trigger]').click();
  const slider=page.locator('[data-music-volume]');
  assert.equal(await slider.isVisible(),true);
  assert.deepEqual(await slider.evaluate(node=>({mode:getComputedStyle(node).writingMode,
    width:node.getBoundingClientRect().width,height:node.getBoundingClientRect().height})),
    {mode:'vertical-lr',width:16,height:112});
  await slider.fill('0.35');
  assert.deepEqual(await page.evaluate(()=>chromeFixture.native.at(-1)),{action:'setVolume',value:0.35,session:12});
  await page.locator('[data-music-action="immersive"]').click();
  assert.equal(await page.locator('.MusicPlayerBar').isVisible(),false);
  assert.equal(await settings.isVisible(),false);
  assert.equal(await page.locator('[data-music-return]').isVisible(),false);
  assert.equal(await page.evaluate(()=>chromeFixture.windowToggles),0,'Focus 不改变原生窗口全屏');
  await page.mouse.move(900,250);
  await page.mouse.move(768,840);
  await page.locator('.MusicPlayerBar').waitFor({state:'visible'});
  await page.locator('[data-music-action="immersive"]').click();
  assert.equal(await settings.isVisible(),true);
  assert.equal(await page.locator('[data-music-return]').isVisible(),true);
  await page.evaluate(()=>chromeFixture.setFullscreen(true));
  await settings.click();
  await page.locator('[data-music-panel-tab="controls"]').click();
  await page.evaluate(()=>document.activeElement.dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',repeat:true,bubbles:true})));
  assert.equal(await page.evaluate(()=>chromeFixture.fullscreen),true);
  await page.keyboard.press('Escape');
  assert.equal(await page.evaluate(()=>chromeFixture.fullscreen),false);
  assert.equal(await page.locator('[data-music-player-panel]').count(),1,'全屏先退出，普通面板保持');
  await page.keyboard.press('Escape');
  assert.equal(await page.locator('[data-music-player-panel]').count(),0);
  await page.mouse.move(5,180);
  await page.locator('[data-music-rail]').waitFor();
  assert.equal(await page.locator('[data-music-rail] .MusicTrackRow').count(),2);
  await page.mouse.move(900,250);
  assert.equal(await page.locator('[data-music-rail]').count(),0,'未固定且无输入焦点时离开关闭');
  await page.mouse.move(5,180);
  await page.locator('[data-music-action="rail-pin"]').click();
  await page.mouse.move(900,250);
  assert.equal(await page.locator('[data-music-rail]').count(),1);
  const railLike=page.locator('[data-music-rail] [data-music-action="like"]');
  await railLike.click();
  assert.equal(await railLike.isDisabled(),true,'队列喜欢写请求期间原位锁定');
  await page.evaluate(()=>{
    const {action,request,provider}=chromeFixture.commands.at(-1);
    chromeFixture.online.receive({version:1,type:'music-online-state',action,request,provider,status:'ready',error:'',
      data:{liked:true}});
    chromeFixture.reply();
  });
  assert.equal(await railLike.isDisabled(),false);
  assert.equal(await railLike.getAttribute('aria-pressed'),'true');
  assert.equal(await page.locator('.MusicPlayerControls [data-music-action="like"]').getAttribute('aria-pressed'),'true');
  const search=page.locator('[data-music-rail-search]');
  await search.fill('Local');
  assert.equal(await page.locator('[data-music-rail] .MusicTrackRow').count(),1);
  await page.locator('[data-music-rail] [data-music-action="play-now"]').click();
  assert.deepEqual(await page.evaluate(()=>chromeFixture.native.at(-1)),
    {action:'playNow',id:'local-123',index:1,session:12});
  await page.locator('[data-music-action="rail-clear-search"]').click();
  await page.locator('[data-music-rail-tab="playlists"]').click();
  await page.locator('[data-music-rail-playlist]').click();
  assert.equal(await page.evaluate(()=>chromeFixture.commands.at(-1).action),'playlist');
  await page.evaluate(()=>chromeFixture.reply());
  assert.equal(await page.locator('.MusicRailCollection [data-music-action="online-play"]').count(),1);
  await search.fill('Search fixture');
  const beforeComposition=await page.evaluate(()=>chromeFixture.commands.length);
  await search.dispatchEvent('compositionstart');
  await search.press('Enter');
  assert.equal(await page.evaluate(()=>chromeFixture.commands.length),beforeComposition,'输入法确认不提交在线搜索');
  await search.dispatchEvent('compositionend');
  await search.press('Enter');
  assert.equal(await page.evaluate(()=>chromeFixture.commands.at(-1).action),'search');
  await page.evaluate(()=>chromeFixture.reply());
  assert.equal(await search.inputValue(),'Search fixture');
  assert.equal(await search.evaluate(node=>document.activeElement===node),true);
  assert.equal(await page.evaluate(()=>chromeNodes.every(node=>node.isConnected)),true);
  assert.equal(await page.evaluate(()=>chromeFixture.updates.slice(clockCount).some(([, , options])=>options?.syncClock)),false,
    '控件与 rail 操作只允许非校时更新');
  assert.equal(await page.evaluate(()=>chromeFixture.view.snapshot().timeline.position),18);
  const evidence=path.join(tmpdir(),'lume-music-original-controls-'+Date.now());
  await mkdir(evidence,{recursive:true});
  await page.screenshot({path:path.join(evidence,'1536-rail.png')});
  await page.locator('[data-music-rail] [data-music-action="close-panel"]').click();
  await page.mouse.move(900,250);
  await page.screenshot({path:path.join(evidence,'1536-player.png')});
  await page.locator('[data-music-action="immersive"]').focus();
  await page.keyboard.press('Escape');
  assert.equal(await page.locator('[data-music-root]').getAttribute('data-screen'),'home');
  await page.evaluate(()=>{chromeFixture.view.dispose();chromeFixture.online.dispose();});
  assert.deepEqual(errors,[]);
  assert.deepEqual(external,[]);
  console.log(`Original desktop chrome screenshots: ${evidence}`);
});

test('生产 app/bridge 接线：合成账号登录更新账户面板，隐藏幂等且旧回包不可见',
  {skip:!enabled,timeout:120000},async t=>{
  const {context,origin,errors}=await setup(t),page=await context.newPage();
  await page.addInitScript(()=>{
    const listeners=new Set();
    window.harness={commands:[],receive:message=>listeners.forEach(listener=>listener({data:message}))};
    window.chrome={webview:{addEventListener:(_,listener)=>listeners.add(listener),
      removeEventListener:(_,listener)=>listeners.delete(listener),postMessage:message=>harness.commands.push(message)}};
    window.onlineCommands=()=>harness.commands.filter(value=>value?.scope==='music-online');
    window.accountRevision=0;
    window.accountsReply=(entries)=>harness.receive({version:1,type:'music-provider-account-state',revision:++accountRevision,
      accounts:['netease','qq','kugou'].map(provider=>({provider,request:0,status:entries.includes(provider)?'signed-in':'signed-out',
        phase:'',userId:entries.includes(provider)?provider+'-fixture':'',displayName:'Synthetic fixture',qrImage:'',qrText:'',error:''}))});
    window.onlineReply=command=>harness.receive({version:1,type:'music-online-state',...command,status:'ready',error:'',
      data:{tracks:[],playlists:[],recommendations:[],more:false}});
  });
  await page.addInitScript(installMusicPreferencesHarness);
  await page.goto(origin+'/index.html');
  await page.waitForFunction(()=>harness.commands.includes('v1|ready'));
  await page.locator('[data-titlebar-music]').click();
  await page.evaluate(()=>accountsReply(['netease']));
  assert.equal(await page.locator('.MusicTopbar select, .MusicTopbar [data-music-provider-open]').count(),0);
  await openPlatforms(page);
  assert.equal(await page.locator('[data-music-provider-active]').getAttribute('data-provider'),'netease');
  assert.equal(await page.locator('[data-music-provider-select], .MusicProviderAccountConnect').count(),0);
  await page.evaluate(()=>accountsReply(['qq']));
  assert.equal(await page.locator('[data-music-provider-active]').getAttribute('data-provider'),'qq');
  assert.equal(await page.evaluate(()=>onlineCommands().at(-1).provider),'qq');
  await page.evaluate(()=>{
    const {request,action,provider}=onlineCommands().at(-1);
    window.oldReply={request,action,provider};
    harness.receive({version:1,type:'visibility',visible:false});
    harness.receive({version:1,type:'visibility',visible:false});
  });
  const count=await page.evaluate(()=>onlineCommands().length);
  await page.evaluate(()=>onlineReply(oldReply));
  assert.equal(await page.evaluate(()=>onlineCommands().length),count);
  assert.equal(await page.evaluate(()=>onlineCommands().at(-2).action),'home');
  await page.evaluate(()=>harness.receive({version:1,type:'visibility',visible:true}));
  assert.equal(await page.evaluate(()=>onlineCommands().at(-1).action),'home');
  await page.evaluate(()=>accountsReply([]));
  assert.equal(await page.locator('[data-music-online-provider]').count(),0);
  assert.equal(await page.locator('.MusicGuest').count(),1);
  await page.locator('[data-music-panel-tab="account"]').focus();
  await page.keyboard.press('Escape');
  await page.evaluate(async()=>{
    const {initialMusicState}=await import('/music.js');
    const {validHostMessage}=await import('/bridge.js');
    const {query,nav,lyrics,...initial}=initialMusicState();
    const online={id:'qq:source-free',title:'Public online title',artist:'Fixture',album:'',artwork:'',duration:180};
    const local={...online,id:'local-123',title:'Public local title'};
    const message={...initial,version:1,type:'music-state',session:8,revision:1,pageOpen:true,pageVisible:true,
      current:online,queue:[online,local],timeline:{position:15,duration:180},status:'paused',
      capabilities:{canPrevious:false,canNext:true,canToggle:true,canSeek:true}};
    if(!validHostMessage(message))throw new Error('真实无 source 原生快照被 bridge 拒绝');
    harness.receive(message);
  });
  await page.locator('[data-music-action="open-player"]').click();
  assert.equal(await page.locator('.MusicNowPlayingMeta strong').innerText(),'Public online title');
  await page.locator('[data-music-action="queue"]').click();
  assert.equal(await page.locator('[data-music-panel="queue"] .MusicTrackRow').count(),2);
  assert.equal(await page.locator('[data-music-panel="queue"] [data-music-action="online-play"]').count(),0);
  await page.locator('[data-music-panel="queue"] [data-music-track-id="local-123"] [data-music-action="play-now"]').click();
  assert.deepEqual(await page.evaluate(()=>{
    const command=harness.commands.filter(value=>value?.scope==='music'&&value.action==='playNow').at(-1);
    return {id:command.id,index:command.index,session:command.session};
  }),{id:'local-123',index:1,session:8});
  await page.locator('[data-music-panel="queue"] [data-music-action="close-panel"]').click();
  await page.locator('[data-music-action="back-home"]').click();
  await page.evaluate(async()=>{
    const {validHostMessage}=await import('/bridge.js');
    const {validMusicLibraryCommand}=await import('/music-library.js');
    window.localReply=revision=>{
      const message={version:1,type:'music-library-state',revision,loaded:true,error:'',
        total:1,offset:0,limit:100,tracks:[{id:'local-123',title:'Public local title',artist:'Fixture',
          album:'',artwork:'',duration:180,source:'',available:true,error:''}]};
      if(!validHostMessage(message))throw new Error('Invalid native local-library fixture');
      if(validHostMessage({...message,tracks:[{...message.tracks[0],id:'local-source-free'}]})||
        validMusicLibraryCommand('play',{id:'local-source-free'}))
        throw new Error('Non-native local IDs must remain rejected');
      harness.receive(message);
    };
    localReply(1);
  });
  await page.locator('[data-music-nav="local"]').click();
  await page.evaluate(()=>localReply(2));
  await page.locator('.MusicTrackRow [data-music-action="local-play"]').click();
  assert.deepEqual(await page.evaluate(()=>harness.commands.filter(value=>value?.scope==='music-library'&&value.action==='play').at(-1)),
    {version:1,scope:'music-library',action:'play',id:'local-123'});
  assert.deepEqual(errors,[]);
});

test('桌面音乐组件沿用组件设置页，开关回执原位更新且不触碰播放或在线账号',
  {skip:!enabled,timeout:120000},async t=>{
  const {context,origin,errors,external}=await setup(t),page=await context.newPage();
  await page.goto(origin+'/tests/music-online-fixture.html');
  await page.evaluate(async()=>{
    const {mountMusicView}=await import('/music-view.js');
    const {createMusicDesktop}=await import('/music-desktop.js');
    const commands=[],navigation={back:0,bubbled:0};
    let view;
    const desktop=createMusicDesktop({send:(action,fields)=>{commands.push({action,...fields});return true;},
      onChange:()=>view?.updateDesktopControls()});
    view=mountMusicView(document.querySelector('main'),{desktopControls:desktop,viewState:{screen:'desktop'},
      onDesktopBack:()=>{navigation.back++;view.dispose();}});
    document.addEventListener('click',event=>{
      if(event.target.closest('[data-action="back"]'))navigation.bubbled++;
    });
    window.desktopFixture={view,desktop,commands,navigation,reply(enabled,error='',running=enabled){
      desktop.receive({version:1,type:'music-desktop-state',enabled,running,stopped:!running,locked:false,
        material:'liquid-glass',width:320,height:152,loadStatus:'loaded',error,
        smtc:{registered:false,status:'verification'},result:'applied',saved:true});
    }};
  });
  assert.equal(await page.locator('[data-settings-scope="music-desktop"]').count(),1);
  assert.equal(await page.locator('[data-music-stage], [data-music-online-provider]').count(),0);
  const toggle=page.locator('[data-music-action="desktop-enabled"]');
  await toggle.focus();
  await page.evaluate(()=>window.toggleNode=document.activeElement);
  await page.keyboard.press('Space');
  assert.deepEqual(await page.evaluate(()=>desktopFixture.commands),[{action:'setEnabled',value:true}]);
  assert.equal(await toggle.isDisabled(),true);
  await page.evaluate(()=>desktopFixture.reply(true));
  assert.equal(await toggle.getAttribute('aria-checked'),'true');
  assert.equal(await page.evaluate(()=>toggleNode===document.querySelector('[data-music-action="desktop-enabled"]')),true);
  await page.locator('[data-tab="details"]').click();
  assert.equal(await toggle.count(),0);
  await page.evaluate(()=>desktopFixture.reply(false,'runtime'));
  assert.match(await page.locator('[role="alert"]').innerText(),/失败/);
  await page.locator('[data-tab="settings"]').click();
  assert.equal(await toggle.getAttribute('aria-checked'),'false');
  await page.evaluate(()=>desktopFixture.reply(true,'',false));
  assert.match(await page.locator('.status').innerText(),/未运行/);
  await toggle.click();
  assert.deepEqual(await page.evaluate(()=>desktopFixture.commands.at(-1)),{action:'setEnabled',value:false});
  await page.locator('[data-action="back"] svg').click();
  assert.deepEqual(await page.evaluate(()=>desktopFixture.navigation),{back:1,bubbled:0});
  await page.evaluate(()=>{desktopFixture.view.dispose();desktopFixture.desktop.dispose();});
  const count=await page.evaluate(()=>desktopFixture.commands.length);
  await toggle.evaluate(node=>node.click());
  await page.locator('[data-action="back"]').evaluate(node=>node.click());
  assert.equal(await page.evaluate(()=>desktopFixture.navigation.back),1,'销毁后不重复调用返回回调');
  assert.equal(await page.evaluate(()=>desktopFixture.commands.length),count);
  assert.deepEqual(errors,[]);
  assert.deepEqual(external,[]);
});

test('冷启动跨平台队列等待喜欢快照后显示红心，首次点击正确取消喜欢',
  {skip:!enabled,timeout:120000},async t=>{
  const {context,origin,errors}=await setup(t),page=await context.newPage();
  await mount(page,origin,1440);
  await page.evaluate(async()=>{
    fixture.accounts([['netease','n'],['qq','q'],['kugou','k']]);
    fixture.reply({data:{tracks:[fixture.song()],playlists:[],recommendations:[],more:false,likedIds:[]}});
    const {initialMusicState}=await import('/music.js'),track=fixture.song('qq');
    fixture.view.update({...initialMusicState(),session:9,current:track,queue:[track],
      pageOpen:true,pageVisible:true,timeline:{position:10,duration:180},
      capabilities:{canToggle:true,canSeek:true,canPrevious:false,canNext:false}});
  });
  await page.locator('[data-music-action="open-player"]').click();
  const heart=page.locator('.MusicPlayerControls [data-music-action="like"]');
  assert.equal(await heart.isDisabled(),true);
  assert.equal(await page.evaluate(()=>fixture.commands.at(-1).action),'likes');
  await page.evaluate(()=>fixture.reply({data:{tracks:[],playlists:[],recommendations:[],more:false,likedIds:['qq:track']}}));
  assert.equal(await heart.isDisabled(),false);
  assert.equal(await heart.getAttribute('aria-pressed'),'true');
  assert.equal(await page.evaluate(()=>fixture.commands.at(-1).provider),'kugou');
  await page.evaluate(()=>fixture.reply({data:{tracks:[],playlists:[],recommendations:[],more:false,likedIds:[]}}));
  assert.deepEqual(await page.evaluate(()=>({
    provider:fixture.online.snapshot().providerId,
    tracks:fixture.online.snapshot().tracks.map(track=>track.id),
  })),{provider:'netease',tracks:['netease:track']});
  await heart.click();
  assert.deepEqual(await page.evaluate(()=>{
    const {action,provider,id,value}=fixture.commands.at(-1);return {action,provider,id,value};
  }),{action:'like',provider:'qq',id:'qq:track',value:false});
  await page.evaluate(()=>fixture.reply({data:{tracks:[],playlists:[],recommendations:[],more:false,liked:false,likedIds:[]}}));
  assert.equal(await heart.getAttribute('aria-pressed'),'false');
  assert.deepEqual(errors,[]);
});

for(const width of [1440,390])test(`生产 app：音乐卡片启停、失败回滚、pending 和后台跨页同步 (${width}px)`,
  {skip:!enabled,timeout:120000},async t=>{
  const {context,origin,errors}=await setup(t),page=await context.newPage();
  await page.setViewportSize({width,height:900});
  await page.addInitScript(()=>{
    const listeners=new Set();
    window.harness={commands:[],failNext:false,receive:message=>listeners.forEach(listener=>listener({data:message}))};
    window.chrome={webview:{addEventListener:(_,listener)=>listeners.add(listener),
      removeEventListener:(_,listener)=>listeners.delete(listener),postMessage:message=>{
        harness.commands.push(message);
        if(message?.scope==='music-desktop'&&harness.failNext) {
          harness.failNext=false;throw new Error('Disconnected');
        }
      }}};
    window.desktopReply=(enabled,error='',result='applied')=>harness.receive({
      version:1,type:'music-desktop-state',enabled,running:enabled,stopped:!enabled,locked:false,
      material:'liquid-glass',width:320,height:152,loadStatus:'loaded',error,
      smtc:{registered:false,status:'verification'},result,saved:result==='applied'&&!error,
    });
  });
  await page.addInitScript(installMusicPreferencesHarness);
  await page.goto(origin+'/index.html');
  await page.waitForFunction(()=>harness.commands.includes('v1|ready'));
  await page.evaluate(()=>desktopReply(true,'','snapshot'));
  await page.locator('[data-view="desktopBeauty"]').click();
  await page.locator('[data-open="widgets"]').click();
  const card=page.locator('.Card [data-action="music-toggle"]');
  const settings=page.locator('[data-music-action="desktop-enabled"]');
  const commands=()=>page.evaluate(()=>harness.commands.filter(command=>command?.scope==='music-desktop'));
  const checkCard=async(enabled,pending)=>{
    assert.equal(await card.count(),1);
    assert.equal(await card.getAttribute('aria-checked'),String(enabled));
    assert.equal(await card.evaluate(node=>node.classList.contains('checked')),enabled);
    assert.equal(await card.isDisabled(),pending);
  };
  await checkCard(true,false);
  await card.evaluate(node=>window.musicCardNode=node);
  await card.click();
  assert.deepEqual((await commands()).at(-1),{version:1,scope:'music-desktop',action:'setEnabled',value:false});
  await checkCard(true,true);
  const sent=await commands();
  await card.evaluate(node=>{
    node.click();
    node.dispatchEvent(new MouseEvent('click',{bubbles:true}));
  });
  assert.deepEqual(await commands(),sent,'忙碌时不重复发送');
  await page.evaluate(()=>desktopReply(true,'save'));
  await checkCard(true,false);
  assert.equal(await card.evaluate(node=>node===window.musicCardNode),true,'回执不重建卡片');
  assert.equal(await page.locator('#notice').isVisible(),true);
  assert.match(await page.locator('#notice').innerText(),/失败/);
  await card.click();
  await page.evaluate(()=>desktopReply(false));
  await checkCard(false,false);
  assert.equal(await page.evaluate(()=>harness.commands.some(command=>command?.scope==='music')),false,
    '卡片启停无需打开播放器');

  await card.click();
  await checkCard(false,true);
  const beforeOpen=await commands();
  await page.locator('[data-open="music"]').click();
  assert.deepEqual(await commands(),beforeOpen,'跨页不得用 snapshot 清除尚未完成的 pending');
  assert.equal(await settings.isDisabled(),true);
  await settings.evaluate(node=>node.click());
  assert.deepEqual(await commands(),beforeOpen);
  await page.evaluate(()=>desktopReply(false,'save'));
  assert.equal(await settings.getAttribute('aria-checked'),'false');
  assert.equal(await settings.isDisabled(),false);
  assert.match(await page.locator('[data-settings-scope="music-desktop"] [role="alert"]').innerText(),/失败/);
  await settings.click();
  await page.evaluate(()=>desktopReply(true));
  assert.equal(await settings.getAttribute('aria-checked'),'true');
  await settings.click();
  await page.locator('[data-music-root] [data-action="back"]').click();
  await checkCard(true,true);
  await page.evaluate(()=>desktopReply(false));
  await checkCard(false,false);

  await card.click();
  await page.locator('.widget-catalog [data-action="back"]').click();
  await page.locator('[data-view="wallpaperOfficial"]').click();
  const wallpaper=await page.locator('.wallpaper-library').elementHandle();
  await page.evaluate(()=>{
    harness.receive({version:1,type:'visibility',visible:false,generation:1});
    desktopReply(true);
    desktopReply(false,'','snapshot');
    harness.receive({version:1,type:'visibility',visible:true,generation:2});
  });
  assert.equal(await wallpaper.evaluate(node=>node===document.querySelector('.wallpaper-library')),true,
    '后台状态更新不重建当前页面');
  await page.locator('[data-view="desktopBeauty"]').click();
  await page.locator('[data-open="widgets"]').click();
  await checkCard(false,false);
  await page.evaluate(()=>harness.failNext=true);
  await card.click();
  await checkCard(false,false);
  assert.equal(await page.locator('#notice').isVisible(),true);
  assert.match(await page.locator('#notice').innerText(),/失败/);
  await card.click();
  await checkCard(false,true);
  await page.evaluate(()=>desktopReply(true));
  await checkCard(true,false);

  if(width===1440) {
    await page.locator('.widget-catalog [data-action="back"]').click();
    await page.locator('.SearchInput').fill('音乐');
    await page.locator('.SearchInput').press('Enter');
    await checkCard(true,false);
    await card.click();
    await checkCard(true,true);
    await page.evaluate(()=>desktopReply(false));
    await checkCard(false,false);
  }
  assert.equal(await page.evaluate(()=>harness.commands.some(command=>command?.scope==='music'&&
    ['play','pause','toggle','playNow','clearQueue'].includes(command.action))),false);
  assert.deepEqual(errors,[]);
});

test('生产 app：组件卡片和原生导航打开桌面设置，返回及标题栏重入不会滞留 desktop',
  {skip:!enabled,timeout:120000},async t=>{
  const {context,origin,errors}=await setup(t),page=await context.newPage();
  await page.setViewportSize({width:1440,height:900});
  await page.addInitScript(()=>{
    const listeners=new Set();
    window.harness={commands:[],receive:message=>listeners.forEach(listener=>listener({data:message}))};
    window.chrome={webview:{addEventListener:(_,listener)=>listeners.add(listener),
      removeEventListener:(_,listener)=>listeners.delete(listener),postMessage:message=>harness.commands.push(message)}};
    window.desktopReply=enabled=>harness.receive({version:1,type:'music-desktop-state',enabled,running:enabled,
      stopped:!enabled,locked:false,material:'liquid-glass',width:320,height:152,loadStatus:'loaded',error:'',
      smtc:{registered:false,status:'verification'},result:'applied',saved:true});
    window.musicCounts=()=>Object.fromEntries(['open','close'].map(action=>[action,
      harness.commands.filter(command=>command?.scope==='music'&&command.action===action).length]));
  });
  await page.addInitScript(installMusicPreferencesHarness);
  await page.goto(origin+'/index.html');
  await page.waitForFunction(()=>harness.commands.includes('v1|ready'));
  await page.locator('[data-view="desktopBeauty"]').click();
  await page.locator('[data-open="widgets"]').click();
  const before=await page.evaluate(()=>musicCounts());
  await page.locator('[data-open="music"]').click();
  assert.equal(await page.locator('[data-music-root]').getAttribute('data-screen'),'desktop');
  assert.equal(await page.locator('[data-settings-scope="music-desktop"]').count(),1);
  assert.equal(await page.locator('[data-music-search], [data-music-stage]').count(),0);
  await page.evaluate(()=>desktopReply(false));
  await page.locator('[data-music-action="desktop-enabled"]').click();
  assert.deepEqual(await page.evaluate(()=>harness.commands.filter(command=>command?.scope==='music-desktop').at(-1)),
    {version:1,scope:'music-desktop',action:'setEnabled',value:true});
  await page.evaluate(()=>desktopReply(true));
  assert.equal(await page.locator('[data-music-action="desktop-enabled"]').getAttribute('aria-checked'),'true');
  await page.locator('[data-tab="details"]').click();
  await page.locator('[data-music-root] [data-action="back"] svg').click();
  await page.locator('.widget-catalog [data-open="music"]').waitFor();
  assert.equal(await page.locator('[data-music-root]').count(),0);
  assert.deepEqual(await page.evaluate(()=>musicCounts()),{open:before.open+1,close:before.close+1});
  await page.locator('.widget-catalog [data-action="back"]').click();
  await page.locator('[data-view="wallpaperOfficial"]').click();
  const wallpaper='[data-view="wallpaperOfficial"][aria-current="page"]';
  await page.locator(wallpaper).waitFor();
  await page.locator('[data-titlebar-music]').click();
  assert.equal(await page.locator('[data-music-root]').getAttribute('data-screen'),'home');
  assert.equal(await page.locator('[data-music-search]').count(),1);
  await page.locator('[data-titlebar-music]').click();
  await page.locator(wallpaper).waitFor();
  const nativeBefore=await page.evaluate(()=>musicCounts());
  await page.evaluate(async()=>{
    const {validHostMessage}=await import('/bridge.js');
    const message={version:1,type:'navigate',page:'music',section:'desktop'};
    if(!validHostMessage(message))throw new Error('Native desktop navigation fixture rejected');
    harness.receive(message);
  });
  assert.equal(await page.locator('[data-music-root]').getAttribute('data-screen'),'desktop');
  await page.locator('[data-music-root] [data-action="back"]').focus();
  await page.keyboard.press('Enter');
  await page.locator(wallpaper).waitFor();
  assert.equal(await page.locator('[data-music-root]').count(),0);
  assert.deepEqual(await page.evaluate(()=>musicCounts()),{open:nativeBefore.open+1,close:nativeBefore.close+1});
  await page.locator('[data-titlebar-music]').click();
  assert.equal(await page.locator('[data-music-root]').getAttribute('data-screen'),'home');
  await page.locator('[data-titlebar-music]').click();
  await page.locator(wallpaper).waitFor();
  assert.equal(await page.evaluate(()=>harness.commands.some(command=>command?.scope==='music'&&
    ['play','pause','toggle','playNow','clearQueue'].includes(command.action))),false);
  assert.deepEqual(errors,[]);
});
