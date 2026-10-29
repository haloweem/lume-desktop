import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile, mkdir, writeFile} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const enabled = process.env.LUME_UI_BROWSER_TESTS === '1';
test('music reference geometry, materials and overflow in home, list and guest states', {skip:!enabled}, async () => {
  const ui = fileURLToPath(new URL('..', import.meta.url));
  const evidence = path.resolve(ui, `../../build-artifacts/native-rebuild/music-visual-${new Date().toISOString().replace(/[:.]/g,'-')}`);
  await mkdir(evidence, {recursive:true});
  const server = createServer(async (req, res) => {
    const relative = new URL(req.url, 'http://localhost').pathname;
    const file = path.resolve(ui, '.' + (relative === '/' ? '/index.html' : relative));
    if (!file.startsWith(ui)) return res.writeHead(403).end();
    try {
      res.setHeader('Content-Type', ({'.html':'text/html','.js':'text/javascript','.css':'text/css','.webp':'image/webp'})[path.extname(file)] || 'application/octet-stream');
      res.end(await readFile(file));
    } catch { res.writeHead(404).end(); }
  });
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const {chromium} = createRequire(import.meta.url)(process.env.LUME_PLAYWRIGHT_DIR || path.join(homedir(), '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright'));
  let browser;
  const measurements = [];
  try {
    browser = await chromium.launch({headless:true, channel:'msedge'});
    for (const [width,height] of [[1536,864],[2560,1440],[680,760],[390,844]]) {
      const page = await browser.newPage({viewport:{width,height}});
      const errors = [];
      page.on('pageerror', error => errors.push(error.message));
      await page.route('https://static.lumedesktop.com/music/fixture.webp', route =>
        route.fulfill({path:path.join(ui,'assets/music/forest.webp'),contentType:'image/webp'}));
      await page.goto(`http://127.0.0.1:${server.address().port}/music.html`);
      await page.evaluate(async () => {
        const {musicPage, initialMusicState} = await import('./music.js');
        const {mountMusicBackground} = await import('./music-background-view.js');
        const tracks = Array.from({length:20}, (_, i) => ({id:String(i), title:i === 0 ? '很长的歌名'.repeat(25) : `歌曲 ${i+1}`, artist:'歌手', album:'专辑名称', artwork:'', duration:180+i, source:`C:/Music/${i}.mp3`}));
        window.fixture = {...initialMusicState(), current:tracks[0], queue:tracks, capabilities:{canToggle:true,canSeek:true,canPrevious:true,canNext:true}, session:9, revision:2, timeline:{position:60,duration:180}};
        window.library = {signedIn:true, recommendations:Array.from({length:17}, (_, i) => ({id:`list-${i}`, title:`参考歌单 ${i+1}`, description:'推荐', artwork:'https://static.lumedesktop.com/music/fixture.webp', tracks})), playlists:[]};
        let background;
        window.draw = async (view = {}, library = window.library) => {
          background?.dispose();
          document.querySelector('#music-preview').innerHTML = musicPage(window.fixture, {view, library});
          const canvas=document.querySelector('[data-music-background]');
          canvas.dataset.musicBackgroundState='loading';
          background=mountMusicBackground(canvas,{visible:true,onReady:()=>{canvas.dataset.musicBackgroundState='ready';}});
          if(!await background.ready)throw new Error('Geometry fixture background failed to load');
        };
        await window.draw();
      });
      await page.waitForFunction(()=>document.querySelector('.MusicBackground')?.dataset.musicBackgroundState==='ready');
      const cover = await page.locator('.MusicRecommendationImage').first().evaluate(async element => {
        const image=element.querySelector('img'); await image.decode();
        const parent=element.getBoundingClientRect(), rect=image.getBoundingClientRect();
        return {parent:{x:parent.x,y:parent.y,width:parent.width,height:parent.height},
          image:{x:rect.x,y:rect.y,width:rect.width,height:rect.height}};
      });
      assert.deepEqual(cover.image,cover.parent,'loaded artwork fills the cover without a second grid row');
      const home = await page.evaluate(() => {
        const rect = selector => {const r=document.querySelector(selector)?.getBoundingClientRect(); return r ? {x:r.x,y:r.y,width:r.width,height:r.height}:null;};
        return {sidebar:rect('.MusicSidebar'),search:rect('.MusicSearchShell'),main:rect('.MusicMain'),player:rect('.MusicPlayerBar'),
          searchRadius:getComputedStyle(document.querySelector('.MusicSearchShell')).borderRadius,
          cards:[...document.querySelectorAll('.MusicRecommendationGrid--supply .MusicRecommendationArtwork')].map(e=>({width:e.clientWidth,height:e.clientHeight})),
          pageOverflow:document.documentElement.scrollWidth > innerWidth};
      });
      measurements.push({width,height,home});
      assert.equal(home.player, null, 'home has no fixed player bar');
      assert.equal(home.search.height, 42, 'historical search height');
      assert.equal(home.searchRadius, '50px');
      assert.equal(home.pageOverflow, false);
      if (width >= 640) assert.equal(home.sidebar.width, 208, 'reference w-52 sidebar');
      if (width >= 768) assert.equal(home.search.x, 248, '208px sidebar + 40px content inset');
      for (const card of home.cards) assert.ok(Math.abs(card.width-card.height) <= 1, 'supply artwork remains square');
      await page.screenshot({path:path.join(evidence, `home-${width}.png`)});
      await page.evaluate(() => window.draw({nav:'local'}));
      const table = page.locator('[data-music-home-track-list]');
      const material = await table.evaluate(e => ({background:getComputedStyle(e).backgroundColor,blur:getComputedStyle(e).backdropFilter,
        rowHeight:e.querySelector('.MusicTrackRow').getBoundingClientRect().height, width:e.clientWidth,scroll:e.scrollWidth}));
      assert.equal(material.background,'rgba(0, 0, 0, 0.2)');
      assert.equal(material.blur,'blur(24px)');
      assert.ok(material.rowHeight >= 60 && material.rowHeight <= 65);
      assert.equal(material.width,material.scroll, 'long titles must not expand the table');
      await page.screenshot({path:path.join(evidence, `list-${width}.png`)});
      await page.evaluate(() => window.draw({screen:'player'}));
      const player = await page.locator('.MusicPlayerBar').evaluate(element => {
        const rect=element.getBoundingClientRect();
        const controls=[...element.querySelectorAll('button')].filter(button=>button.getBoundingClientRect().width)
          .map(button=>{const r=button.getBoundingClientRect(); return {left:r.left,right:r.right,top:r.top,bottom:r.bottom};});
        return {left:rect.left,right:rect.right,top:rect.top,bottom:rect.bottom,height:rect.height,controls};
      });
      assert.ok(player.left>=0 && player.right<=width && player.bottom<=height);
      assert.ok(player.height>=104 && player.height<=112);
      for (const control of player.controls) {
        assert.ok(control.left>=player.left && control.right<=player.right && control.top>=player.top && control.bottom<=player.bottom,
          'player controls stay within the floating surface');
      }
      await page.screenshot({path:path.join(evidence, `player-${width}.png`)});
      await page.evaluate(() => window.draw({screen:'player',panel:'queue'}));
      await page.screenshot({path:path.join(evidence, `queue-${width}.png`)});
      await page.evaluate(() => { window.fixture={...window.fixture,queue:[],current:null}; return window.draw({}, {signedIn:false}); });
      assert.equal(await page.locator('.MusicPlayerBar').count(),0);
      assert.equal(await page.locator('.MusicGuest').count(),1);
      await page.screenshot({path:path.join(evidence, `guest-${width}.png`)});
      assert.deepEqual(errors, []);
      await page.close();
    }
    await writeFile(path.join(evidence,'geometry.json'), JSON.stringify(measurements,null,2));
  } finally {
    await browser?.close();
    await new Promise(resolve=>server.close(resolve));
  }
});
