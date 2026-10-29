import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile,mkdir,writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

test('music home controls preserve editors, select collections and expose full-row playback',
  {skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:180000},async()=>{
  const ui=path.resolve(fileURLToPath(new URL('..',import.meta.url)));
  const evidence=path.resolve(ui,'../../build-artifacts/native-rebuild/music-controls-20261003');
  const runtime=path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
  const {chromium}=createRequire(import.meta.url)(process.env.LUME_PLAYWRIGHT_DIR||path.join(runtime,'playwright'));
  const server=createServer(async(req,res)=>{
    const file=path.resolve(ui,'.'+new URL(req.url,'http://localhost').pathname);
    if(!file.startsWith(ui+path.sep))return res.writeHead(403).end();
    try {
      res.setHeader('Content-Type',({'.html':'text/html','.js':'text/javascript','.css':'text/css','.webp':'image/webp'})[path.extname(file)]||'application/octet-stream');
      res.end(await readFile(file));
    } catch {res.writeHead(404).end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser;
  const report=[];
  try {
    browser=await chromium.launch({headless:true,channel:'msedge'});
    await mkdir(evidence,{recursive:true});
    for(const [width,height,scale] of [[1440,900,1],[1152,720,1.25],[960,600,1.5],[720,450,2],[390,844,1]]) {
      const page=await browser.newPage({viewport:{width,height},deviceScaleFactor:scale});
      const errors=[];
      page.on('pageerror',error=>errors.push(error.message));
      await page.goto(`http://127.0.0.1:${server.address().port}/music.html`);
      await page.evaluate(async()=>{
        const {mountMusicView}=await import('./music-view.js');
        const {initialMusicState}=await import('./music.js');
        const {createMusicOnline,validMusicOnlineMessage}=await import('./music-online.js');
        const tracks=Array.from({length:30},(_,index)=>({id:`netease:${index}`,title:index===0?'长标题'.repeat(30):`Song ${index}`,
          artist:index===1?'Solo':'Ensemble',album:`Album ${index}`,source:`online:netease:${index}`,duration:180,artwork:''}));
        const collection=(id,title,owned)=>({id,title,owned,providerId:'netease',type:'playlist',artwork:'',description:''});
        const playlists=[collection('owned','Evening',true)],recommendations=[collection('daily','Daily',false)];
        window.state={...initialMusicState(),session:9,current:tracks[0],queue:tracks,pageVisible:true,
          capabilities:{canToggle:true,canPrevious:true,canNext:true,canSeek:true},timeline:{position:10,duration:180}};
        window.commands=[];window.onlineCommands=[];
        window.online=createMusicOnline({onChange:library=>window.controller?.updateLibrary(library),
          send:(action,fields)=>{
            onlineCommands.push({action,...fields});
            if(action==='cancel')return true;
            const selected=action==='search'?tracks.filter(track=>[track.title,track.artist,track.album]
              .some(value=>value.toLowerCase().includes(fields.query.toLowerCase()))):
              action==='playlist'&&fields.id==='owned'?tracks.slice(0,10):tracks;
            const message={version:1,type:'music-online-state',request:fields.request,provider:fields.provider,
              action,status:action==='like'?'error':'ready',error:action==='like'?'unsupported':'',
              data:{tracks:selected,playlists,recommendations,more:false}};
            if(!validMusicOnlineMessage(message))throw new Error('Invalid online fixture');
            online.receive(message);
            return true;
          }});
        online.accounts({accounts:[{provider:'netease',status:'signed-in',userId:'fixture'}]});
        window.controller=mountMusicView(document.querySelector('#music-preview'),{snapshot:state,
          library:online.snapshot(),online,
          send:(action,fields)=>{commands.push({action,...fields});return true;}});
        online.setVisible(true);
      });
      await page.locator('.MusicRecommendationCard').click();
      assert.equal(await page.locator('.MusicSectionHeader h1').textContent(),'Daily');
      const search=page.locator('[data-music-search]');
      await search.evaluate(element=>{window.editor=element;window.table=document.querySelector('.MusicTrackList');});
      await search.fill('Solo');
      assert.equal(await search.evaluate(element=>element===window.editor),true);
      assert.equal(await page.locator('.MusicTrackList').evaluate(element=>element===window.table),true);
      await search.press('Enter');
      assert.deepEqual(await page.evaluate(()=>({action:onlineCommands.at(-1).action,query:onlineCommands.at(-1).query})),
        {action:'search',query:'Solo'});
      assert.equal(await page.locator('[data-music-track-id]').count(),1);
      assert.equal(await page.locator('.MusicSongTab span').textContent(),'1');
      await page.locator('[data-music-action="clear-search"]').click();
      assert.deepEqual(await page.evaluate(()=>({action:onlineCommands.at(-1).action,id:onlineCommands.at(-1).id})),
        {action:'playlist',id:'daily'});
      assert.equal(await page.locator('.MusicSongTab span').textContent(),'30');
      assert.equal(await search.evaluate(element=>element===document.activeElement),true);
      await search.pressSequentially('Alpha');
      await page.evaluate(()=>{
        window.state={...state,current:state.queue[1],currentIndex:1,revision:2};
        controller.update(state);
      });
      await search.press('Control+z');
      assert.equal(await search.inputValue(),'','track changes preserve the native editor undo stack');
      await page.locator('[data-music-action="list-search"]').click();
      const filter=page.locator('[data-music-list-search]');
      assert.deepEqual(await filter.evaluate(element=>{
        const style=getComputedStyle(element.closest('label'));
        return {blur:style.backdropFilter,color:style.color};
      }),{blur:'blur(12px)',color:'rgba(255, 255, 255, 0.75)'});
      await filter.evaluate(element=>window.filterEditor=element);
      await filter.fill('Song 1');
      assert.equal(await filter.evaluate(element=>element===window.filterEditor),true);
      assert.equal(await page.locator('.MusicSongTab span').textContent(),'11');
      await filter.press('Escape');
      assert.equal(await page.locator('[data-music-action="list-search"]').evaluate(element=>element===document.activeElement),true);
      const first=page.locator('[data-music-track-id="netease:1"]');
      const beforePlay=await page.evaluate(()=>onlineCommands.length);
      await first.evaluate(element=>element.scrollIntoView({block:'center'}));
      const rowBounds=await first.boundingBox();
      assert.equal(await page.evaluate(({x,y})=>document.elementFromPoint(x,y)?.closest('[data-music-action]')?.dataset.musicAction,
        {x:rowBounds.x+rowBounds.width-16,y:rowBounds.y+rowBounds.height/2}),'online-play',
        `${width}x${height}: row edge is reachable at ${JSON.stringify(rowBounds)}`);
      await page.mouse.click(rowBounds.x+rowBounds.width-16,rowBounds.y+rowBounds.height/2);
      assert.deepEqual(await page.evaluate(start=>onlineCommands.slice(start)
        .filter(command=>command.action!=='cancel').map(({action,id})=>({action,id})),beforePlay),
        [{action:'play',id:'netease:1'}],
        `${width}x${height}: full-row playback sends one online play request`);
      assert.equal(await page.locator('[data-music-root]').getAttribute('data-screen'),'player');
      await page.locator('[data-music-action="back-home"]').click();
      const before=await page.evaluate(()=>commands.length);
      if(width>=768) {
        const like=await first.locator('[data-music-action="like"]').boundingBox();
        await page.mouse.click(like.x+like.width/2,like.y+like.height/2);
        assert.equal(await page.evaluate(()=>commands.length),before,'unavailable row actions do not click through to playback');
        assert.equal(await page.evaluate(()=>onlineCommands.at(-1).action),'like');
        await page.locator('[data-music-action="online-retry"]').click();
      }
      const geometry=await page.evaluate(()=>{
        const root=document.querySelector('[data-music-root]');
        const search=document.querySelector('.MusicSearchShell').getBoundingClientRect();
        const player=document.querySelector('[data-music-open-player]').getBoundingClientRect();
        return {horizontalOverflow:document.documentElement.scrollWidth>innerWidth||root.scrollWidth>root.clientWidth,
          player:{top:player.top,right:innerWidth-player.right},searchRight:search.right,playerLeft:player.left};
      });
      assert.equal(geometry.horizontalOverflow,false);
      assert.equal(geometry.player.top,66);
      assert.equal(geometry.player.right,24);
      assert.ok(geometry.searchRight<=geometry.playerLeft,'search does not overlap the fixed player entry');
      await page.screenshot({path:path.join(evidence,`list-${width}-${scale}.png`)});
      await page.locator('[data-music-action="open-player"]').click();
      await page.locator('[data-music-action="queue"]').click();
      await page.locator('aside [data-music-action="close-panel"]').press('Escape');
      assert.equal(await page.locator('[data-music-action="queue"]').evaluate(element=>element===document.activeElement),true);
      await page.locator('[data-music-action="back-home"]').click();
      assert.equal(await page.locator('.MusicSectionHeader h1').textContent(),'Daily');
      if(width>=640) {
        await page.locator('.MusicPlaylistItem').click();
        assert.equal(await page.locator('.MusicSectionHeader h1').textContent(),'Evening');
        assert.equal(await page.locator('.MusicPlaylistItem').getAttribute('aria-current'),'page');
      }
      assert.deepEqual(errors,[]);
      report.push({width,height,scale,geometry,errors});
      await page.evaluate(()=>{controller.dispose();online.dispose();});
      await page.close();
    }
    await writeFile(path.join(evidence,'report.json'),JSON.stringify({scope:'controlled browser UI; no live provider or audio host',viewports:report},null,2));
  } finally {await browser?.close();await new Promise(resolve=>server.close(resolve));}
});
