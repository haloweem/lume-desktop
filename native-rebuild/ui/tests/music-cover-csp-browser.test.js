import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile,mkdir} from 'node:fs/promises';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

test('production CSP loads QQ and Kugou covers in desktop and mobile layouts',
  {skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:60000},async t=>{
  const ui=path.resolve(fileURLToPath(new URL('..',import.meta.url)));
  const csp=(await readFile(path.join(ui,'index.html'),'utf8'))
    .match(/<meta\s+http-equiv="Content-Security-Policy"[\s\S]*?>/)[0];
  const server=createServer(async(req,res)=>{
    const pathname=new URL(req.url,'http://localhost').pathname;
    if(pathname==='/fixture.html') {
      res.setHeader('Content-Type','text/html; charset=utf-8');
      return res.end(`<!doctype html><html><head>${csp}<link rel="stylesheet" href="/music.css"><style>html,body{margin:0;background:#252525}*{box-sizing:border-box}main{height:900px}</style></head><body><main></main></body></html>`);
    }
    const file=path.resolve(ui,'.'+pathname);
    if(!file.startsWith(ui+path.sep))return res.writeHead(403).end();
    try {
      res.setHeader('Content-Type',({'.js':'text/javascript','.css':'text/css'})[path.extname(file)]||'application/octet-stream');
      res.end(await readFile(file));
    } catch {res.writeHead(404).end();}
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  let browser;
  t.after(async()=>{await browser?.close();await new Promise(resolve=>server.close(resolve));});
  const runtime=path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
  const require=createRequire(import.meta.url);
  const {chromium}=require(process.env.LUME_PLAYWRIGHT_DIR||path.join(runtime,'playwright'));
  const {PNG}=require(path.join(runtime,'pngjs'));
  const png=new PNG({width:32,height:32});
  for(let i=0;i<png.data.length;i+=4)png.data.set([40,180,120,255],i);
  const image=PNG.sync.write(png);
  const evidence=path.resolve(ui,'../../build-artifacts/native-rebuild/music-cover-csp');
  await mkdir(evidence,{recursive:true});
  browser=await chromium.launch({headless:true,channel:'msedge'});
  const origin=`http://127.0.0.1:${server.address().port}`;
  const covers=['https://qpic.y.qq.com/music_cover/fixture.jpg','https://img.kgimg.com/stdmusic/fixture.jpg'];
  for(const width of [1440,390]) {
    const page=await browser.newPage({viewport:{width,height:900}});
    const errors=[],violations=[];
    page.on('pageerror',error=>errors.push(error.message));
    await page.route('**/*',route=>{
      if(covers.includes(route.request().url()))
        return route.fulfill({status:200,contentType:'image/png',body:image});
      if(route.request().url().startsWith(origin+'/'))return route.continue();
      return route.abort();
    });
    await page.goto(origin+'/fixture.html');
    await page.evaluate(async covers=>{
      window.coverViolations=[];
      document.addEventListener('securitypolicyviolation',event=>coverViolations.push(event.blockedURI));
      const {musicPage,initialMusicState}=await import('/music.js');
      document.querySelector('main').innerHTML=musicPage(initialMusicState(),{library:{
        signedIn:true,provider:'qq',tracks:[],playlists:[],
        recommendations:covers.map((artwork,index)=>({id:String(index),providerId:'qq',title:'Cover fixture '+index,artwork})),
      }});
    },covers);
    const images=page.locator('.MusicRecommendationImage img');
    assert.equal(await images.count(),2);
    await images.evaluateAll(nodes=>nodes.forEach(node=>{node.loading='eager';}));
    await images.evaluateAll(nodes=>Promise.all(nodes.map(node=>node.decode())));
    assert.ok((await images.evaluateAll(nodes=>nodes.map(node=>node.naturalWidth))).every(width=>width===32));
    violations.push(...await page.evaluate(()=>coverViolations));
    assert.deepEqual(violations,[]);
    assert.deepEqual(errors,[]);
    await page.screenshot({path:path.join(evidence,`covers-${width}.png`)});
    await page.close();
  }
});
