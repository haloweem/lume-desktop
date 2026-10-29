import test from "node:test";
import assert from "node:assert/strict";
import {createServer} from "node:http";
import {readFile,mkdir} from "node:fs/promises";
import {homedir} from "node:os";
import path from "node:path";
import {fileURLToPath,pathToFileURL} from "node:url";
import {createRequire} from "node:module";

const enabled=process.env.LUME_UI_BROWSER_TESTS==="1";
const mediaFixture=process.env.LUME_UI_TEST_VIDEO;
// Test-only responses exercise decoding and error recovery without shipping
// local wallpaper art or depending on the availability of a public CDN.
const gallery=[
  {title:"Forest",file:"forest.jpg"},
  {title:"Floating In Space By VISUALDON",file:"floating-in-space.jpg"},
  {title:"The Dock",file:"the-dock.jpg"},
];
test(`wallpaper browser commands, snapshot updates and preview cleanup${mediaFixture?" with real MP4 decoding":""}`,{skip:!enabled},async()=>{
  const root=fileURLToPath(new URL("../",import.meta.url));
  const playwrightDirectory=process.env.LUME_PLAYWRIGHT_DIR ||
    path.join(homedir(),".cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright");
  const {chromium}=await import(pathToFileURL(path.join(playwrightDirectory,"index.mjs")).href);
  const server=createServer(async(request,response)=>{
    try {
      const pathname=decodeURIComponent(new URL(request.url,"http://localhost").pathname);
      const target=path.resolve(root,pathname==="/"?"index.html":pathname.slice(1));
      if (!target.startsWith(root)) {response.writeHead(403).end();return;}
      const data=await readFile(target);
      const type={".html":"text/html",".js":"text/javascript",".css":"text/css",".jpg":"image/jpeg"}[path.extname(target)];
      response.writeHead(200,{"Content-Type":type || "application/octet-stream"}).end(data);
    } catch {response.writeHead(404).end();}
  });
  await new Promise(resolve=>server.listen(0,"127.0.0.1",resolve));
  let browser;
  try {
    browser=await chromium.launch({headless:true,
      ...(process.env.LUME_BROWSER_EXECUTABLE?{executablePath:process.env.LUME_BROWSER_EXECUTABLE}:{})});
    const page=await browser.newPage({viewport:{width:1536,height:864}});
    if(process.env.LUME_UI_SCREENSHOT_DIR)await mkdir(process.env.LUME_UI_SCREENSHOT_DIR,{recursive:true});
    const errors=[];
    page.on("pageerror",error=>errors.push(error.message));
    const cover=await readFile(path.join(root,"assets/black-texture.webp"));
    const video=mediaFixture ? await readFile(mediaFixture) : null;
    const imageRequests=[];
    const assetRequests=[];
    let failPublicImages=false;
    await page.route(/^https:\/\/(?:static\.lumedesktop\.com|lume-wallpaper\.local|lume-12345-1444884137\.cos\.ap-nanjing\.myqcloud\.com)\//,route=>{
      const url=new URL(route.request().url());
      assetRequests.push(url.href);
      const filename=path.basename(new URL(route.request().url()).pathname);
      const isVideo=video && filename.endsWith(".mp4");
      if(!filename.endsWith(".mp4")) {
        imageRequests.push(url.href);
        if(failPublicImages && url.hostname==="static.lumedesktop.com")return route.abort("failed");
      }
      return route.fulfill({status:200,contentType:isVideo?"video/mp4":"image/webp",
        body:isVideo ? video : cover});
    });
    await page.addInitScript(({gallery,realMedia})=>{
      const listeners=new Set();
      const item={id:"official-1",title:"Forest",kind:"video",thumbnail:"https://static.lumedesktop.com/forest.jpg",
        url:"https://static.lumedesktop.com/forest.mp4",width:1920,height:1080,bytes:1024,libraryId:""};
      window.wallpaperFixture={version:1,type:"wallpaper-state",revision:0,status:"idle",error:"",loadStatus:"fresh",
        playerAvailable:true,catalogStatus:"idle",catalogError:"",catalog:[],
        library:[{id:"local-1",title:"My video",kind:"video",thumbnail:"https://lume-wallpaper.local/forest.jpg",
          url:"https://lume-wallpaper.local/forest.mp4",bytes:1024,missing:false}],
        displays:[{id:"1",name:"主显示器",width:1920,height:1080,primary:true,wallpaperId:"",
          state:"stopped",autoPaused:false,fit:"cover",volume:0,error:""},
          {id:"18446744073709551615",name:"第二显示器",width:1920,height:1080,primary:false,wallpaperId:"",
            state:"stopped",autoPaused:false,fit:"cover",volume:0,error:""}],
        download:{id:"",status:"idle",received:0,total:0,error:""},pauseFullscreen:false,likedIds:[]};
      window.wallpaperCommands=[];
      window.emitHost=value=>listeners.forEach(listener=>listener({data:value}));
      window.engagementFixture={version:1,type:"wallpaper-engagement-state",revision:0,id:"",
        status:"idle",likes:null,downloads:null,liked:false,canLike:false,busy:false,error:""};
      window.emitEngagement=fields=>{
        window.engagementFixture={...window.engagementFixture,...fields,revision:window.engagementFixture.revision+1};
        window.emitHost(window.engagementFixture);
      };
      window.emitWallpaper=fields=>{
        window.wallpaperFixture={...window.wallpaperFixture,...fields,revision:window.wallpaperFixture.revision+1};
        const {catalog,library,likedIds,...partial}=window.wallpaperFixture;
        window.emitHost({...partial,...(Object.hasOwn(fields,"catalog")?{catalog}:{}),
          ...(Object.hasOwn(fields,"library")?{library}:{}),...(Object.hasOwn(fields,"likedIds")?{likedIds}:{})});
      };
      window.finishWallpaperDownload=()=>{
        const command=window.pendingNativeApply;
        const official=window.wallpaperFixture.catalog.find(item=>item.id===command.id);
        const cached={id:"downloaded-1",title:official.title,kind:official.kind,bytes:official.bytes,
          thumbnail:official.thumbnail,url:"https://lume-wallpaper.local/forest.mp4",missing:false};
        window.emitWallpaper({library:[...window.wallpaperFixture.library,cached],
          catalog:window.wallpaperFixture.catalog.map(item=>item.id===official.id?{...item,libraryId:cached.id}:item),
          displays:window.wallpaperFixture.displays.map(display=>command.action==="applyAll" || display.id===command.display ?
            {...display,wallpaperId:cached.id,state:"playing",error:""} : display),
          download:{id:official.id,status:"complete",received:1000,total:1000,error:""},error:""});
        window.pendingNativeApply=null;
      };
      window.chrome={webview:{
        addEventListener(name,callback){if(name==="message")listeners.add(callback);},
        removeEventListener(name,callback){listeners.delete(callback);},
        postMessageWithAdditionalObjects(command,files){
          (window.wallpaperDroppedFiles ||= []).push({command,names:files.map(file=>file.name)});
          queueMicrotask(()=>window.emitWallpaper({error:"",status:"idle"}));
        },
        postMessage(command){
          window.wallpaperCommands.push(command);
          if(command==="v1|ready") queueMicrotask(()=>{
            window.emitHost({version:1,type:"account-state",revision:0,status:"signed-in",busy:false,error:"",
              profile:{id:"fixture-account",displayName:"Fixture",avatarUrl:"",vip:false,vipExpiresAt:""}});
            window.emitHost(window.wallpaperFixture);
          });
          if(command?.scope==="wallpaper") queueMicrotask(()=>{
            if(command.action==="refresh")window.emitWallpaper({catalogStatus:"ready",
              catalog:Array.from({length:8043},(_,n)=>({...item,id:`official-${n+1}`,
                title:gallery[n%gallery.length].title,thumbnail:`https://static.lumedesktop.com/${gallery[n%gallery.length].file}`}))});
            else if(command.action==="download")window.emitWallpaper({error:"",download:{id:command.id,status:"running",received:500,total:1000,error:""}});
            else if(command.action==="cancel"){
              window.pendingNativeApply=null;
              window.emitWallpaper({error:"",download:{...window.wallpaperFixture.download,status:"cancelled"}});
            }
            else if(command.action==="stats")window.emitEngagement({id:command.id,status:"ready",
              likes:0,downloads:0,liked:false,canLike:true,busy:false,error:""});
            else if(command.action==="endStats")window.emitEngagement({id:"",status:"idle",
              likes:null,downloads:null,liked:false,canLike:false,busy:false,error:""});
            else if(command.action==="like"){
              if(window.failNextLike){window.failNextLike=false;window.emitEngagement({status:"failed",error:"WALLPAPER_LIKE_FAILED",busy:false});}
              else window.emitEngagement({status:"ready",error:"",busy:false,liked:command.value,likes:command.value?1:0});
            }
            else if(command.action==="apply" || command.action==="applyAll"){
              if(window.wallpaperFixture.library.some(item=>item.id===command.id))
                window.emitWallpaper({error:"",displays:window.wallpaperFixture.displays.map(display=>
                  command.action==="applyAll" || display.id===command.display ?
                    {...display,wallpaperId:command.id,state:"playing",error:""} : display)});
              else {
                window.pendingNativeApply=command;
                window.emitWallpaper({error:"",download:{id:command.id,status:"running",received:500,total:1000,error:""}});
              }
            }
            else if(command.action==="remove"){
              if(window.wallpaperFixture.displays.some(display=>display.wallpaperId===command.id && display.state!=="stopped"))
                window.emitWallpaper({error:"WALLPAPER_MEDIA_IN_USE"});
              else window.emitWallpaper({error:"",
                library:window.wallpaperFixture.library.filter(item=>item.id!==command.id),
                catalog:window.wallpaperFixture.catalog.map(item=>item.libraryId===command.id?{...item,libraryId:""}:item)});
            }
            else if(command.action==="stopAll")window.emitWallpaper({error:"",displays:window.wallpaperFixture.displays.map(display=>
              ({...display,state:"stopped",wallpaperId:"",error:""}))});
            else if(["fit","volume","pause","resume","stop"].includes(command.action))
              window.emitWallpaper({error:"",displays:window.wallpaperFixture.displays.map(display=>
                display.id!==command.display ? display : command.action==="fit" || command.action==="volume" ?
                  {...display,[command.action]:command.value} : {...display,state:{pause:"paused",resume:"playing",stop:"stopped"}[command.action]})});
            else if(command.action==="pauseFullscreen")window.emitWallpaper({error:"",pauseFullscreen:command.value});
            else window.emitWallpaper({error:""});
          });
        },
      }};
      const play=HTMLMediaElement.prototype.play;
      HTMLMediaElement.prototype.play=function(){
        window.previewPlayCount=(window.previewPlayCount || 0)+1;
        return realMedia ? play.call(this) : Promise.resolve();
      };
      if (!realMedia) {
        HTMLMediaElement.prototype.pause=function(){};
        HTMLMediaElement.prototype.load=function(){};
      }
    },{gallery,realMedia:!!video});
    await page.goto(`http://127.0.0.1:${server.address().port}/`);
    const dropFiles=async(selector,count=2)=>page.locator(selector).evaluate((target,count)=>{
      const transfer=new DataTransfer();
      for(let i=0;i<count;i++)transfer.items.add(new File(["fixture"],`wallpaper-${i}.png`,{type:"image/png"}));
      const drag=new DragEvent("dragover",{bubbles:true,cancelable:true,dataTransfer:transfer});
      target.dispatchEvent(drag);
      const highlighted=!!document.querySelector(".wallpaper-drop-active");
      const drop=new DragEvent("drop",{bubbles:true,cancelable:true,dataTransfer:transfer});
      target.dispatchEvent(drop);
      return {prevented:drop.defaultPrevented,highlighted,
        cleared:!document.querySelector(".wallpaper-drop-active"),
        batches:window.wallpaperDroppedFiles?.length || 0};
    },count);
    await page.waitForFunction(()=>window.wallpaperCommands.includes("v1|ready"));
    await page.waitForSelector('.HomeHero [data-wallpaper-id="official-1"]');
    const dragCount=()=>page.evaluate(()=>window.wallpaperCommands.filter(command=>command==="v1|window|drag").length);
    const dragsBeforeBlank=await dragCount();
    const toolbarBounds=await page.locator(".ModuleToolbar").boundingBox();
    await page.mouse.click(toolbarBounds.x+10,toolbarBounds.y+24);
    assert.equal(await dragCount(),dragsBeforeBlank+1,"the actual toolbar gap sends a window drag");
    await page.locator(".SearchInput").click();
    assert.equal(await dragCount(),dragsBeforeBlank+1,"the search input remains an input, not a drag surface");
    assert.doesNotMatch(await page.locator(".HomeHero").textContent(),/内置预览/);
    failPublicImages=true;
    await page.evaluate(()=>window.emitWallpaper({catalog:window.wallpaperFixture.catalog.map((item,index)=>index ? item :
      {...item,thumbnail:"https://static.lumedesktop.com/lume/wallpapers/official/thumbnails/cdn-fallback.jpg"})}));
    await page.waitForFunction(()=>{
      const image=document.querySelector(".preview.current img");
      return image && !image.hidden && image.naturalWidth>0 &&
        image.src.startsWith("https://lume-12345-1444884137.cos.ap-nanjing.myqcloud.com/");
    });
    assert.ok(imageRequests.some(url=>url.includes("cos.ap-nanjing.myqcloud.com") && url.includes("cdn-fallback")),
      "home covers decode the approved origin response after a public CDN failure");
    assert.equal(assetRequests.filter(url=>url.includes("cdn-fallback")).length,1,
      "the preferred cover is requested once");
    await page.getByRole("button",{name:"下一张",exact:true}).click();
    await page.waitForFunction(()=>document.querySelector(".HomeHero [data-wallpaper-id]")?.dataset.wallpaperId==="official-2");
    await page.getByRole("button",{name:"上一张",exact:true}).click();
    await page.waitForFunction(()=>document.querySelector(".HomeHero [data-wallpaper-id]")?.dataset.wallpaperId==="official-1");
    assert.equal(assetRequests.filter(url=>url.includes("cdn-fallback")).length,1,
      "returning to a carousel cover reuses the session image cache");
    await page.locator('.HomeHero [data-wallpaper-id="official-1"]').click();
    await page.waitForSelector("[data-wallpaper-detail]");
    assert.deepEqual(await dropFiles(".wallpaper-detail-media"),
      {prevented:true,highlighted:false,cleared:true,batches:0},"detail never imports dropped files or navigates to them");
    await page.waitForFunction(()=>{
      const image=document.querySelector(".wallpaper-detail-poster");
      return image && !image.hidden && image.naturalWidth>0;
    });
    assert.equal(await page.getByRole("button",{name:"前往官方库",exact:true}).count(),0);
    await page.getByRole("button",{name:"下载壁纸",exact:true}).click();
    await page.waitForFunction(()=>window.wallpaperCommands.some(command=>command.action==="download"));
    assert.deepEqual(await page.evaluate(()=>window.wallpaperCommands.find(command=>command.action==="download")),
      {version:1,scope:"wallpaper",action:"download",id:"official-1"});
    await page.evaluate(()=>window.emitWallpaper({download:{id:"",status:"idle",received:0,total:0,error:""}}));
    await page.getByRole("button",{name:"返回壁纸列表",exact:true}).click();
    await page.waitForSelector(".HomeHero");
    failPublicImages=false;
    await page.evaluate(()=>{
      window.catalogForAsyncList=window.wallpaperFixture.catalog;
      window.emitWallpaper({catalog:[],catalogStatus:"loading"});
    });
    await page.getByRole("button",{name:"官方壁纸",exact:true}).click();
    await page.waitForSelector(".wallpaper-card-loading");
    await page.evaluate(()=>window.emitWallpaper({catalog:window.catalogForAsyncList,catalogStatus:"ready"}));
    await page.waitForSelector('[data-wallpaper-id="official-1"]');
    assert.equal(await page.locator(".wallpaper-card").count(),36);
    const grid=await page.locator(".wallpaper-card").evaluateAll(elements=>elements.slice(0,5).map(node=>({
      x:node.getBoundingClientRect().x,y:node.getBoundingClientRect().y,
    })));
    assert.equal(grid[0].y,grid[3].y);
    assert.ok(grid[4].y>grid[0].y);
    assert.equal(await page.locator("video").count(),0);
    assert.deepEqual(await dropFiles(".wallpaper-grid"),
      {prevented:true,highlighted:false,cleared:true,batches:0},"official wallpaper library is not a local import target");
    await page.evaluate(()=>{
      window.firstCardBeforeSnapshot=document.querySelector(".wallpaper-card");
      document.querySelector(".wallpaper-grid").scrollTop=180;
      window.emitWallpaper({});
    });
    assert.ok(await page.evaluate(()=>window.firstCardBeforeSnapshot===document.querySelector(".wallpaper-card")),
      "display snapshots preserve visible catalog cards");
    assert.equal(await page.locator(".wallpaper-grid").evaluate(grid=>grid.scrollTop),180);
    await page.locator('[data-wallpaper-page="3"]').click();
    await page.waitForSelector('[data-wallpaper-id="official-73"]');
    const beforeDetailScroll=await page.locator(".wallpaper-grid").evaluate(grid=>{
      grid.scrollTop=grid.scrollHeight;
      return grid.scrollTop;
    });
    assert.ok(beforeDetailScroll>500);
    await page.locator('[data-wallpaper-id="official-108"]').click();
    await page.waitForSelector("[data-wallpaper-detail]");
    await page.getByRole("button",{name:"返回壁纸列表",exact:true}).click();
    assert.equal(await page.locator(".wallpaper-grid").evaluate(grid=>grid.scrollTop),beforeDetailScroll,
      "returning from a bottom-row wallpaper restores the original page-three scroll position");
    if(process.env.LUME_UI_SCREENSHOT_DIR)
      await page.screenshot({path:path.join(process.env.LUME_UI_SCREENSHOT_DIR,"wallpaper-return-page-three.png")});
    assert.equal(await page.locator('[data-wallpaper-page][aria-current="page"]').textContent(),"3");
    assert.equal(await page.locator("#wallpaper-kind").inputValue(),"video");
    await page.getByRole("button",{name:"下一页",exact:true}).click();
    assert.equal(await page.locator(".wallpaper-grid").evaluate(grid=>grid.scrollTop),0,
      "deliberate pagination starts the next page at the top");
    await page.locator('[data-wallpaper-page="1"]').click();
    await page.getByRole("button",{name:"下一页",exact:true}).click();
    await page.waitForSelector('[data-wallpaper-id="official-37"]');
    await page.getByRole("button",{name:"上一页",exact:true}).click();
    await page.locator('[data-wallpaper-id="official-1"]').click();
    await page.waitForFunction(()=>document.querySelector("video")?.hasAttribute("src"),null,{timeout:2000});
    assert.ok(await page.locator("video").evaluate(video=>video.muted && video.loop));
    if (video) await page.waitForFunction(()=>{
      const preview=document.querySelector("video");
      return preview.readyState>=2 && !preview.paused && preview.currentTime>0;
    });
    assert.deepEqual(await page.locator(".wallpaper-detail-bar").evaluate(bar=>
      [...bar.querySelectorAll(":scope > button, :scope > .wallpaper-detail-engagement button, :scope > .wallpaper-detail-actions .wallpaper-apply")]
        .map(button=>button.dataset.action || button.dataset.wallpaperAction || button.dataset.wallpaperPanel)),
    ["back","download","remove","settings","like","targets"]);
    assert.equal(await page.locator(".wallpaper-detail-bar .wallpaper-preview-button").count(),0);
    await page.getByRole("button",{name:"喜欢",exact:true}).click();
    await page.waitForFunction(()=>document.querySelector('[data-wallpaper-action="like"]').getAttribute("aria-pressed")==="true");
    assert.equal(await page.locator('[data-wallpaper-action="like"]').evaluate(button=>getComputedStyle(button).color),"rgb(255, 59, 48)");
    if(process.env.LUME_UI_SCREENSHOT_DIR)await page.screenshot({path:path.join(process.env.LUME_UI_SCREENSHOT_DIR,"wallpaper-liked.png")});
    await page.evaluate(()=>window.emitWallpaper({}));
    assert.equal(await page.locator('[data-wallpaper-action="like"]').getAttribute("aria-pressed"),"true");
    await page.getByRole("button",{name:"取消喜欢",exact:true}).click();
    await page.waitForFunction(()=>document.querySelector('[data-wallpaper-action="like"]').getAttribute("aria-pressed")==="false");
    await page.evaluate(()=>{window.failNextLike=true;});
    await page.getByRole("button",{name:"喜欢",exact:true}).click();
    await page.waitForFunction(()=>document.querySelector(".wallpaper-detail-notice").textContent.includes("喜欢操作失败"));
    assert.equal(await page.locator('[data-wallpaper-action="like"]').getAttribute("aria-pressed"),"false");
    await page.evaluate(()=>window.emitEngagement({status:"ready",error:"",liked:false}));
    await page.locator('[data-wallpaper-panel="settings"]').click();
    assert.ok(await page.locator(".wallpaper-info-sidebar").isVisible());
    assert.equal(await page.evaluate(()=>document.activeElement.id),"wallpaper-info-sidebar");
    await page.keyboard.press("Escape");
    assert.ok(await page.locator(".wallpaper-info-sidebar").isHidden());
    assert.equal(await page.evaluate(()=>document.activeElement.dataset.wallpaperPanel),"settings");
    await page.locator('[data-wallpaper-panel="settings"]').click();
    await page.locator(".wallpaper-apply").click();
    assert.ok(await page.locator(".wallpaper-info-sidebar").isHidden(),"application menu closes the settings sidebar");
    assert.ok(await page.locator("#wallpaper-target-menu").isVisible());
    if(process.env.LUME_UI_SCREENSHOT_DIR)await page.screenshot({path:path.join(process.env.LUME_UI_SCREENSHOT_DIR,"wallpaper-targets.png")});
    await page.getByRole("menuitem",{name:"屏幕2",exact:true}).click();
    await page.waitForFunction(()=>window.wallpaperCommands.some(command=>command.action==="apply"));
    assert.deepEqual(await page.evaluate(()=>window.wallpaperCommands.find(command=>command.action==="apply")),
      {version:1,scope:"wallpaper",action:"apply",id:"official-1",display:"18446744073709551615"});
    assert.equal(await page.locator(".wallpaper-apply").textContent(),"50%");
    assert.ok(await page.getByRole("button",{name:"下载壁纸",exact:true}).isDisabled());
    await page.evaluate(()=>{
      window.videoBeforeProgress=document.querySelector("video");
      window.controlsBeforeProgress=document.querySelector(".wallpaper-detail-bar");
      window.emitHost({version:1,type:"wallpaper-progress",download:{id:"official-1",status:"running",received:750,total:1000,error:""}});
    });
    assert.equal(await page.locator(".wallpaper-apply").textContent(),"75%");
    assert.ok(await page.evaluate(()=>window.videoBeforeProgress===document.querySelector("video") &&
      window.controlsBeforeProgress===document.querySelector(".wallpaper-detail-bar")));
    await page.evaluate(()=>window.emitWallpaper({download:{id:"official-1",status:"cancelled",received:750,total:1000,error:""}}));
    await page.waitForFunction(()=>document.querySelector(".wallpaper-apply").textContent==="下载并应用");
    await page.locator(".wallpaper-apply").click();
    assert.equal(await page.getByRole("menuitem").count(),2);
    await page.getByRole("menuitem",{name:"屏幕1",exact:true}).click();
    await page.waitForFunction(()=>window.pendingNativeApply?.display==="1");
    await page.evaluate(()=>window.finishWallpaperDownload());
    await page.waitForFunction(()=>document.querySelector(".wallpaper-apply").textContent==="取消应用");
    await page.locator(".wallpaper-apply").click();
    assert.deepEqual(await page.locator("#wallpaper-target-menu button").allTextContents(),
      ["取消屏幕1","屏幕2"]);
    assert.equal(await page.getByRole("menuitem",{name:"取消屏幕1",exact:true}).getAttribute("data-wallpaper-action"),"stop");
    assert.equal(await page.getByRole("menuitem",{name:"屏幕2",exact:true}).getAttribute("data-wallpaper-action"),"apply");
    if(process.env.LUME_UI_SCREENSHOT_DIR)await page.screenshot({path:path.join(process.env.LUME_UI_SCREENSHOT_DIR,"wallpaper-cancel-mixed.png")});
    await page.getByRole("menuitem",{name:"屏幕2",exact:true}).click();
    await page.waitForFunction(()=>window.wallpaperFixture.displays.every(d=>d.wallpaperId==="downloaded-1"));
    await page.locator(".wallpaper-apply").click();
    assert.equal(await page.locator("#wallpaper-target-menu .is-cancel").count(),2);
    assert.deepEqual(await page.locator("#wallpaper-target-menu .is-cancel").allTextContents(),
      ["取消屏幕1","取消屏幕2"]);
    assert.equal(await page.locator(".wallpaper-apply").evaluate(button=>getComputedStyle(button).backdropFilter),
      "blur(18px) saturate(1.25)");
    if(process.env.LUME_UI_SCREENSHOT_DIR)await page.screenshot({path:path.join(process.env.LUME_UI_SCREENSHOT_DIR,"wallpaper-cancel-dual.png")});
    await page.getByRole("menuitem",{name:"取消屏幕1",exact:true}).click();
    assert.deepEqual(await page.evaluate(()=>window.wallpaperCommands.filter(c=>c.action==="stop").at(-1)),
      {version:1,scope:"wallpaper",action:"stop",display:"1"});
    await page.waitForFunction(()=>window.wallpaperFixture.displays[0].state==="stopped");
    assert.equal(await page.evaluate(()=>window.wallpaperFixture.displays[1].state),"playing");
    await page.locator(".wallpaper-apply").click();
    await page.getByRole("menuitem",{name:"屏幕1",exact:true}).click();
    await page.evaluate(()=>window.emitWallpaper({displays:window.wallpaperFixture.displays.map((display,n)=>n ?
      {...display,state:"failed",error:"WALLPAPER_PLAYER_EXITED"} : display)}));
    assert.equal(await page.locator(".wallpaper-apply").textContent(),"取消应用","one failed display must not hide cancellation on another screen");
    await page.evaluate(()=>{
      window.dualDisplays=window.wallpaperFixture.displays;
      window.emitWallpaper({displays:[window.dualDisplays[0]]});
    });
    assert.equal(await page.locator(".wallpaper-apply").getAttribute("data-wallpaper-action"),"stop");
    assert.equal(await page.locator("#wallpaper-target-menu").count(),0);
    if(process.env.LUME_UI_SCREENSHOT_DIR)await page.screenshot({path:path.join(process.env.LUME_UI_SCREENSHOT_DIR,"wallpaper-cancel-single.png")});
    await page.setViewportSize({width:390,height:844});
    const cancelBounds=await page.locator(".wallpaper-apply").boundingBox();
    const cancelBarBounds=await page.locator(".wallpaper-detail-bar").boundingBox();
    assert.ok(cancelBounds.x>=0 && cancelBounds.x+cancelBounds.width<=390);
    assert.ok(cancelBounds.y>=cancelBarBounds.y &&
      cancelBounds.y+cancelBounds.height<=cancelBarBounds.y+cancelBarBounds.height);
    if(process.env.LUME_UI_SCREENSHOT_DIR)await page.screenshot({path:path.join(process.env.LUME_UI_SCREENSHOT_DIR,"wallpaper-cancel-mobile.png")});
    await page.locator(".wallpaper-apply").click();
    await page.waitForFunction(()=>window.wallpaperFixture.displays[0].state==="stopped");
    assert.equal(await page.locator(".wallpaper-apply").textContent(),"应用");
    await page.setViewportSize({width:1536,height:864});
    await page.evaluate(()=>window.emitWallpaper({displays:window.dualDisplays}));
    await page.locator('[data-wallpaper-action="remove"]').click();
    await page.waitForFunction(()=>document.querySelector(".wallpaper-detail-notice")?.textContent.includes("正在使用"));
    assert.ok(await page.locator("[data-wallpaper-detail]").isVisible(),"an in-use removal failure stays on the detail page");
    assert.equal(await page.locator("video").getAttribute("src"),null,"removal releases preview before asking native to delete");
    assert.ok(await page.getByRole("button",{name:"从我的壁纸移除",exact:true}).isEnabled());
    await page.locator('[data-wallpaper-panel="settings"]').click();
    assert.equal(await page.locator(".wallpaper-playback-settings").count(),0,
      "the original settings sidebar contains metadata only");
    assert.equal(await page.locator('[data-wallpaper-action="stopAll"]').count(),0,
      "stop-all remains a native capability without an extra sidebar control");
    await page.evaluate(()=>window.emitWallpaper({displays:window.wallpaperFixture.displays.map(display=>
      ({...display,state:"stopped",wallpaperId:"",error:""}))}));
    await page.waitForFunction(()=>window.wallpaperFixture.displays.every(display=>display.state==="stopped"));
    await page.keyboard.press("Escape");
    await page.locator('[data-wallpaper-action="remove"]').click();
    await page.waitForSelector("[data-wallpaper-library]");
    assert.match(await page.locator("#notice").textContent(),/壁纸已从我的壁纸移除/);
    if(process.env.LUME_UI_SCREENSHOT_DIR)await page.screenshot({path:path.join(process.env.LUME_UI_SCREENSHOT_DIR,"wallpaper-delete-return.png")});
    await page.getByRole("button",{name:"我的壁纸",exact:true}).click();
    const plusCenter=await page.locator(".wallpaper-import-plus").evaluate(plus=>{
      const card=plus.closest("button").getBoundingClientRect(),bounds=plus.getBoundingClientRect();
      return {dx:Math.abs(bounds.x+bounds.width/2-card.x-card.width/2),
        dy:Math.abs(bounds.y+bounds.height/2-card.y-card.height/2)};
    });
    assert.ok(plusCenter.dx<1 && plusCenter.dy<1,JSON.stringify(plusCenter));
    for (const width of [1536,960,1920]) {
      await page.setViewportSize({width,height:width*9/16});
      for (const font of ["Segoe UI","Arial","Times New Roman"]) {
        await page.locator(".wallpaper-import-card").evaluate((card,font)=>{card.style.fontFamily=font;},font);
        const image=await page.locator(".wallpaper-import-plus").screenshot({
          ...(process.env.LUME_UI_SCREENSHOT_DIR ? {path:path.join(process.env.LUME_UI_SCREENSHOT_DIR,
            `wallpaper-import-glyph-${width}-${font.replaceAll(" ","-")}.png`)} : {}),
        });
        const ink=await page.evaluate(async png=>{
          const image=new Image();
          image.src=`data:image/png;base64,${png}`;
          await image.decode();
          const canvas=document.createElement("canvas");
          canvas.width=image.width;canvas.height=image.height;
          const context=canvas.getContext("2d");
          context.drawImage(image,0,0);
          const {data}=context.getImageData(0,0,canvas.width,canvas.height);
          let left=canvas.width,top=canvas.height,right=-1,bottom=-1,count=0;
          // Only the light glyph is counted; the dark card and blue ring are excluded.
          for (let y=0;y<canvas.height;y++) for (let x=0;x<canvas.width;x++) {
            const index=(y*canvas.width+x)*4;
            if (data[index]>128 && data[index+1]>128 && data[index+2]>128) {
              left=Math.min(left,x);right=Math.max(right,x);
              top=Math.min(top,y);bottom=Math.max(bottom,y);count++;
            }
          }
          return {count,dx:Math.abs((left+right+1-canvas.width)/2),
            dy:Math.abs((top+bottom+1-canvas.height)/2)};
        },image.toString("base64"));
        assert.ok(ink.count>20 && ink.dx<=1 && ink.dy<=1,
          `visible plus strokes must be centered at ${width}px with ${font}: ${JSON.stringify(ink)}`);
      }
    }
    await page.locator(".wallpaper-import-card").evaluate(card=>card.style.removeProperty("font-family"));
    await page.setViewportSize({width:1536,height:864});
    await page.evaluate(()=>window.emitWallpaper({status:"busy",error:"WALLPAPER_BUSY"}));
    assert.deepEqual(await dropFiles(".wallpaper-grid"),
      {prevented:true,highlighted:false,cleared:true,batches:0},
      "an error cannot reopen the drop zone while the native worker is busy");
    await page.evaluate(()=>window.emitWallpaper({status:"idle",error:""}));
    assert.deepEqual(await dropFiles(".wallpaper-grid"),
      {prevented:true,highlighted:true,cleared:true,batches:1});
    assert.deepEqual(await page.evaluate(()=>window.wallpaperDroppedFiles[0]),
      {command:"v1|wallpaper|dropFiles",names:["wallpaper-0.png","wallpaper-1.png"]});
    assert.equal((await dropFiles(".wallpaper-grid",33)).batches,1);
    assert.match(await page.locator("#notice").textContent(),/32/);
    if(process.env.LUME_UI_SCREENSHOT_DIR)
      await page.screenshot({path:path.join(process.env.LUME_UI_SCREENSHOT_DIR,"wallpaper-import-centered.png")});
    await page.evaluate(()=>{
      window.localLibraryBeforeScrollTest=window.wallpaperFixture.library;
      const item=window.wallpaperFixture.library[0];
      window.emitWallpaper({library:Array.from({length:80},(_,n)=>({...item,id:`local-${n+1}`}))});
    });
    await page.getByRole("button",{name:"下一页",exact:true}).click();
    const localScroll=await page.locator(".wallpaper-grid").evaluate(grid=>{
      grid.scrollTop=grid.scrollHeight;
      return grid.scrollTop;
    });
    assert.ok(localScroll>500);
    await page.locator('[data-wallpaper-id="local-72"]').click();
    await page.getByRole("button",{name:"返回壁纸列表",exact:true}).click();
    assert.equal(await page.locator(".wallpaper-grid").evaluate(grid=>grid.scrollTop),localScroll,
      "my wallpaper restores its own page-two position");
    assert.equal(await page.locator('[data-wallpaper-page][aria-current="page"]').textContent(),"2");
    await page.locator(".SearchInput").fill("My");
    await page.locator(".SearchInput").press("Enter");
    assert.equal(await page.locator(".wallpaper-grid").evaluate(grid=>grid.scrollTop),0,
      "submitting a local search resets the list position");
    await page.locator(".wallpaper-grid").evaluate(grid=>{grid.scrollTop=500;});
    await page.locator('[data-action="clear"]').click();
    assert.equal(await page.locator(".wallpaper-grid").evaluate(grid=>grid.scrollTop),0,
      "clearing the local search resets the list position");
    await page.locator(".wallpaper-grid").evaluate(grid=>{grid.scrollTop=500;});
    await page.locator("#wallpaper-page-jump").fill("2");
    await page.locator("#wallpaper-page-jump").press("Tab");
    assert.equal(await page.locator('[data-wallpaper-page][aria-current="page"]').textContent(),"2");
    assert.equal(await page.locator(".wallpaper-grid").evaluate(grid=>grid.scrollTop),0,
      "entering a page number uses the same reset path as pagination buttons");
    await page.selectOption("#wallpaper-kind","video");
    assert.equal(await page.locator(".wallpaper-grid").evaluate(grid=>grid.scrollTop),0,
      "changing a filter starts at the top");
    await page.selectOption("#wallpaper-kind","all");
    await page.evaluate(()=>window.emitWallpaper({library:window.localLibraryBeforeScrollTest}));
    await page.locator('[data-wallpaper-id="local-1"]').click();
    assert.equal(await page.locator('[data-wallpaper-action="like"]').count(),0);
    await page.locator(".wallpaper-apply").click();
    await page.getByRole("menuitem",{name:"屏幕2",exact:true}).click();
    await page.waitForFunction(()=>window.wallpaperCommands.some(command=>command.action==="apply" && command.id==="local-1"));
    assert.deepEqual(await page.evaluate(()=>window.wallpaperCommands.find(command=>command.action==="apply" && command.id==="local-1")),
      {version:1,scope:"wallpaper",action:"apply",id:"local-1",display:"18446744073709551615"});
    await page.locator('[data-wallpaper-panel="settings"]').click();
    assert.equal(await page.locator(".wallpaper-playback-settings").count(),0);
    assert.equal(await page.locator("#wallpaper-display").count(),0);
    assert.equal(await page.locator("#wallpaper-fit").count(),0);
    assert.equal(await page.locator("#wallpaper-volume").count(),0);
    assert.equal(await page.locator('[data-wallpaper-action="pauseFullscreen"]').count(),0);
    await page.keyboard.press("Escape");
    await page.waitForFunction(()=>window.wallpaperCommands.some(command=>command.action==="preview"));
    assert.equal(await page.locator("video").getAttribute("src"),"https://lume-wallpaper.local/forest.mp4");
    await page.evaluate(()=>{
      window.videoBeforeSnapshot=document.querySelector("video");
      window.controlsBeforeSnapshot=document.querySelector(".wallpaper-detail-bar");
      window.playsBeforeSnapshot=window.previewPlayCount;
      const {catalog,library,...partial}=window.wallpaperFixture;
      window.wallpaperFixture.revision++;
      window.emitHost({...partial,revision:window.wallpaperFixture.revision});
    });
    assert.ok(await page.evaluate(()=>window.videoBeforeSnapshot===document.querySelector("video") &&
      window.controlsBeforeSnapshot===document.querySelector(".wallpaper-detail-bar")),
      "unchanged partial snapshots keep both the preview and controls");
    assert.equal(await page.evaluate(()=>window.previewPlayCount),await page.evaluate(()=>window.playsBeforeSnapshot));
    await page.evaluate(()=>{
      const {catalog,library,...partial}=window.wallpaperFixture;
      window.wallpaperFixture.revision++;
      window.emitHost({...partial,revision:window.wallpaperFixture.revision,
        displays:partial.displays.map(display=>({...display,volume:31}))});
    });
    assert.ok(await page.evaluate(()=>window.videoBeforeSnapshot===document.querySelector("video")));
    assert.equal(await page.evaluate(()=>window.previewPlayCount),await page.evaluate(()=>window.playsBeforeSnapshot));
    await page.evaluate(()=>window.emitHost({version:1,type:"visibility",visible:false}));
    assert.equal(await page.locator("video").getAttribute("src"),null);
    assert.ok(await page.evaluate(()=>window.wallpaperCommands.some(command=>command.action==="endPreview")));
    await page.evaluate(()=>window.emitHost({version:1,type:"visibility",visible:true}));
    await page.waitForFunction(()=>document.querySelector("video").hasAttribute("src"));
    await page.evaluate(()=>{
      Object.defineProperty(document,"visibilityState",{configurable:true,value:"hidden"});
      document.dispatchEvent(new Event("visibilitychange"));
    });
    assert.equal(await page.locator("video").getAttribute("src"),null,"document hiding also releases the media source");
    await page.evaluate(()=>{
      delete document.visibilityState;
      document.dispatchEvent(new Event("visibilitychange"));
    });
    await page.waitForFunction(()=>document.querySelector("video").hasAttribute("src"));
    await page.getByRole("button",{name:"暂停预览",exact:true}).click();
    assert.equal(await page.locator("video").getAttribute("src"),null);
    await page.evaluate(()=>{
      window.playsBeforeManualPause=window.previewPlayCount;
      window.emitHost({version:1,type:"visibility",visible:false});
      window.emitHost({version:1,type:"visibility",visible:true});
    });
    assert.equal(await page.locator("video").getAttribute("src"),null,"manual pause survives hiding and restoring");
    assert.equal(await page.evaluate(()=>window.previewPlayCount),await page.evaluate(()=>window.playsBeforeManualPause));
    await page.getByRole("button",{name:"播放预览",exact:true}).click();
    await page.waitForFunction(()=>document.querySelector("video").hasAttribute("src"));
    await page.evaluate(()=>window.emitWallpaper({library:window.wallpaperFixture.library.map(item=>({...item,missing:true,url:""}))}));
    assert.equal(await page.locator("video").getAttribute("src"),null,"missing media must release its decoder");
    assert.ok(await page.getByRole("button",{name:"播放预览",exact:true}).isDisabled());
    await page.evaluate(()=>window.emitWallpaper({library:window.wallpaperFixture.library.map(item=>({...item,missing:false,url:"https://lume-wallpaper.local/forest.mp4"}))}));
    await page.waitForFunction(()=>document.querySelector("video").hasAttribute("src"));
    await page.evaluate(()=>window.emitWallpaper({library:[]}));
    assert.equal(await page.locator("video").getAttribute("src"),null,"removed media must release its decoder");
    assert.deepEqual(errors,[]);
    const output=process.env.LUME_UI_SCREENSHOT_DIR;
    await page.getByRole("button",{name:"返回壁纸列表",exact:true}).click();
    await page.getByRole("button",{name:"官方壁纸",exact:true}).click();
    await page.evaluate(()=>window.emitWallpaper({download:{id:"",status:"idle",received:0,total:0,error:""}}));
    await page.evaluate(()=>window.emitWallpaper({catalogStatus:"ready",catalogError:"WALLPAPER_CATALOG_CACHED"}));
    assert.equal(await page.locator(".wallpaper-inline-notice").count(),0,
      "using a valid catalog cache must not add a persistent footer notice");
    assert.ok(await page.locator(".wallpaper-pagination").isVisible());
    for (const [catalogError,text] of [
      ["WALLPAPER_CATALOG_OFFLINE:WALLPAPER_HTTP_SEND","暂时无法更新官方目录"],
      ["WALLPAPER_CATALOG_CACHE_SAVE:WALLPAPER_LIBRARY_WRITE","本地缓存保存失败"],
      ["WALLPAPER_HTTP_503","官方壁纸暂时无法访问"],
    ]) {
      await page.evaluate(catalogError=>window.emitWallpaper({catalogError}),catalogError);
      assert.ok((await page.locator(".wallpaper-inline-notice").textContent()).includes(text));
      assert.ok(await page.locator(".wallpaper-pagination").isVisible());
    }
    await page.evaluate(()=>window.emitWallpaper({catalogError:"WALLPAPER_CATALOG_CACHED"}));
    assert.equal(await page.locator(".wallpaper-inline-notice").count(),0);
    if (output) {
      await mkdir(output,{recursive:true});
      await page.screenshot({path:path.join(output,"wallpaper-official.png")});
    }
    await page.locator('.wallpaper-card[data-wallpaper-id="official-1"]').click();
    if (output) {
      await page.screenshot({path:path.join(output,"wallpaper-detail.png")});
    }
    const detailLayout=await page.locator(".wallpaper-detail-bar").evaluate(bar=>{
      const bounds=bar.getBoundingClientRect(),apply=bar.querySelector(".wallpaper-apply").getBoundingClientRect();
      const textWidth=selector=>{
        const range=document.createRange();
        range.selectNodeContents(bar.querySelector(selector));
        return range.getBoundingClientRect().width;
      };
      return {barTop:bounds.top,barBottom:bounds.bottom,applyTop:apply.top,applyBottom:apply.bottom,
        width:bounds.width,height:bounds.height,expectedWidth:Math.min(760,Math.max(520,
          420+Math.ceil(Math.max(textWidth(".wallpaper-detail-title"),textWidth(".wallpaper-detail-meta"))))),
        background:getComputedStyle(bar,"::before").backgroundColor,blur:getComputedStyle(bar,"::before").backdropFilter,
        children:[...bar.children].map(element=>({name:element.className,display:getComputedStyle(element).display,
          x:element.getBoundingClientRect().x,y:element.getBoundingClientRect().y}))};
    });
    assert.equal(detailLayout.width,detailLayout.expectedWidth,"detail capsule follows the original content width");
    assert.equal(detailLayout.height,60);
    assert.equal(detailLayout.background,"rgba(20, 24, 30, 0.44)");
    assert.equal(detailLayout.blur,"blur(18px) saturate(1.25)");
    assert.ok(detailLayout.applyTop>=detailLayout.barTop && detailLayout.applyBottom<=detailLayout.barBottom,
      JSON.stringify(detailLayout));
    await page.locator('[data-wallpaper-panel="settings"]').click();
    const settingsLayout=await page.locator(".wallpaper-info-sidebar").evaluate(sidebar=>{
      const bounds=sidebar.getBoundingClientRect(),style=getComputedStyle(sidebar);
      return {x:bounds.x,y:bounds.y,width:bounds.width,bottom:bounds.bottom,
        background:style.backgroundColor,blur:style.backdropFilter,imageFit:getComputedStyle(sidebar.querySelector("img")).objectFit};
    });
    assert.deepEqual(settingsLayout,{x:1196,y:44,width:340,bottom:864,background:"rgba(0, 0, 0, 0.2)",blur:"blur(24px)",imageFit:"cover"});
    if (process.env.LUME_UI_SCREENSHOT_DIR)
      await page.screenshot({path:path.join(process.env.LUME_UI_SCREENSHOT_DIR,"wallpaper-settings.png")});
    await page.locator(".wallpaper-detail-media").click({position:{x:300,y:300}});
    assert.ok(await page.locator(".wallpaper-info-sidebar").isHidden());
    assert.equal(await page.evaluate(()=>document.activeElement.dataset.wallpaperPanel),"settings");
    await page.evaluate(()=>window.emitWallpaper({catalog:window.wallpaperFixture.catalog.map(item=>item.id==="official-1" ?
      {...item,title:"Ender Miles Morales Snow Time WUHD (4K) — 山间薄雾与晨光中的森林"} : item)}));
    const longLayout=await page.locator(".wallpaper-detail-bar").evaluate(bar=>{
      const title=bar.querySelector(".wallpaper-detail-title");
      return {width:bar.getBoundingClientRect().width,titleWidth:title.clientWidth,scrollWidth:title.scrollWidth,
        textOverflow:getComputedStyle(title).textOverflow,
        overflowing:[...bar.children].some(child=>child.getBoundingClientRect().right>bar.getBoundingClientRect().right)};
    });
    assert.equal(longLayout.width,760);
    assert.equal(longLayout.textOverflow,"ellipsis");
    assert.ok(longLayout.scrollWidth>longLayout.titleWidth);
    assert.equal(longLayout.overflowing,false);
    if(process.env.LUME_UI_SCREENSHOT_DIR)await page.screenshot({path:path.join(process.env.LUME_UI_SCREENSHOT_DIR,"wallpaper-detail-long-title.png")});
    assert.equal(await page.evaluate(()=>window.wallpaperCommands.filter(command=>command.action==="refresh").length),1);
    await page.waitForFunction(()=>document.querySelector("video").hasAttribute("src"));
    await page.evaluate(()=>{
      window.commandsBeforeClose=window.wallpaperCommands.length;
      window.dispatchEvent(new Event("beforeunload"));
    });
    assert.equal(await page.locator("video").getAttribute("src"),null);
    assert.ok(await page.evaluate(()=>window.wallpaperCommands.slice(window.commandsBeforeClose)
      .some(command=>command.action==="endPreview")));
    await page.reload();
    await page.getByRole("button",{name:"官方壁纸",exact:true}).click();
    await page.locator('.wallpaper-card[data-wallpaper-id="official-1"]').click();
    await page.waitForFunction(()=>document.querySelector("video").hasAttribute("src"));
    assert.equal(await page.evaluate(()=>window.previewPlayCount),1);
    await page.evaluate(()=>{
      document.querySelector("video").dispatchEvent(new Event("error"));
      window.playsAfterFailure=window.previewPlayCount;
      window.emitWallpaper({});
    });
    await page.waitForFunction(()=>document.querySelector("video").getAttribute("src")==="https://static.lumedesktop.com/forest.mp4");
    assert.equal(await page.evaluate(()=>window.previewPlayCount),2,
      "a failed preferred preview source retries once through the approved fallback");
    await new Promise(resolve=>setTimeout(resolve,100));
    assert.equal(await page.evaluate(()=>window.previewPlayCount),2,
      "a successful fallback does not start a preview retry loop");
    await page.evaluate(()=>{
      const staticItem={id:"official-static",title:"Static Full Image",kind:"image",
        thumbnail:"https://static.lumedesktop.com/static-thumb.jpg",
        url:"https://static.lumedesktop.com/static-full.jpg",width:3840,height:2160,bytes:2048,libraryId:""};
      window.wallpaperFixture.catalog=[staticItem,...window.wallpaperFixture.catalog];
      const {catalog,library,...partial}=window.wallpaperFixture;
      window.wallpaperFixture.revision++;
      window.emitHost({...partial,revision:window.wallpaperFixture.revision,catalog});
    });
    await page.getByRole("button",{name:"返回壁纸列表",exact:true}).click();
    await page.getByRole("button",{name:"官方壁纸",exact:true}).click();
    await page.selectOption("#wallpaper-kind","image");
    await page.locator('[data-wallpaper-id="official-static"]').click();
    await page.waitForFunction(()=>window.wallpaperCommands.some(command=>command.action==="preview" && command.id==="official-static"));
    await page.waitForFunction(()=>document.querySelector(".wallpaper-detail-poster")?.getAttribute("src")==="https://lume-12345-1444884137.cos.ap-nanjing.myqcloud.com/static-full.jpg");
    assert.equal(await page.locator("video").count(),0,"static detail does not create a video decoder");
    assert.equal(await page.locator(".wallpaper-detail-poster").getAttribute("src"),
      "https://lume-12345-1444884137.cos.ap-nanjing.myqcloud.com/static-full.jpg");
    assert.notEqual(await page.locator(".wallpaper-detail-poster").getAttribute("src"),
      "https://static.lumedesktop.com/static-thumb.jpg");
    assert.equal(await page.locator(".wallpaper-preview-button").count(),0);
    await page.evaluate(()=>{
      window.staticImageBeforeSnapshot=document.querySelector(".wallpaper-detail-poster");
      window.emitWallpaper({});
    });
    assert.ok(await page.evaluate(()=>window.staticImageBeforeSnapshot===document.querySelector(".wallpaper-detail-poster")));
    await page.evaluate(()=>window.emitHost({version:1,type:"visibility",visible:false}));
    assert.equal(await page.locator(".wallpaper-detail-poster").getAttribute("src"),
      "https://lume-12345-1444884137.cos.ap-nanjing.myqcloud.com/static-thumb.jpg","hidden detail releases full image lease");
    assert.ok(await page.evaluate(()=>window.wallpaperCommands.some(command=>command.action==="endPreview")));
    await page.evaluate(()=>window.emitHost({version:1,type:"visibility",visible:true}));
    await page.waitForFunction(()=>document.querySelector(".wallpaper-detail-poster")?.getAttribute("src")==="https://lume-12345-1444884137.cos.ap-nanjing.myqcloud.com/static-full.jpg");
    if (process.env.LUME_UI_SCREENSHOT_DIR)
      await page.screenshot({path:path.join(process.env.LUME_UI_SCREENSHOT_DIR,"wallpaper-static-full.png")});
    await page.evaluate(()=>window.emitWallpaper({playerAvailable:false,
      library:[...window.wallpaperFixture.library,{id:"native-image",title:"Native image",kind:"image",
        thumbnail:"",url:"https://lume-wallpaper.local/native.jpg",bytes:1024,missing:false}],
      catalog:window.wallpaperFixture.catalog.map(item=>item.id==="official-static"?
        {...item,libraryId:"native-image"}:item),
      displays:window.wallpaperFixture.displays.map(display=>({...display,state:"stopped",wallpaperId:"",error:""}))}));
    for (const [width,height] of [[1536,864],[390,844]]) {
      await page.setViewportSize({width,height});
      assert.ok(await page.locator(".wallpaper-apply").isEnabled(),"static apply does not require mpv");
      assert.doesNotMatch(await page.locator(".wallpaper-detail-notice").textContent(),/播放器不可用/);
      await page.locator(".wallpaper-apply").click();
      await page.getByRole("menuitem",{name:"屏幕2",exact:true}).click();
      await page.waitForFunction(()=>window.wallpaperFixture.displays[1].wallpaperId==="native-image");
      assert.deepEqual(await page.evaluate(()=>window.wallpaperCommands.filter(command=>command.action==="apply").at(-1)),
        {version:1,scope:"wallpaper",action:"apply",id:"native-image",display:"18446744073709551615"});
      await page.locator(".wallpaper-apply").click();
      await page.getByRole("menuitem",{name:"取消屏幕2",exact:true}).click();
      await page.waitForFunction(()=>window.wallpaperFixture.displays[1].state==="stopped");
      assert.ok(await page.locator(".wallpaper-apply").isEnabled(),"native image remains applicable after unapply");
    }
    await page.setViewportSize({width:1536,height:864});
    await page.evaluate(()=>window.emitWallpaper({playerAvailable:true}));
    await page.getByRole("button",{name:"返回壁纸列表",exact:true}).click();
    await page.getByRole("button",{name:"首页",exact:true}).click();
    await page.locator(".SearchInput").fill("Forest");
    await page.locator(".SearchInput").press("Enter");
    await page.waitForSelector(".wallpaper-search-results");
    assert.ok(await page.locator(".wallpaper-search-results").evaluate(root=>{
      const grid=root.querySelector(".wallpaper-grid").getBoundingClientRect();
      const pagination=root.querySelector(".wallpaper-pagination").getBoundingClientRect();
      const notice=root.querySelector(".availability").getBoundingClientRect();
      return grid.bottom<=pagination.top && pagination.bottom<=notice.top;
    }),"search results, pagination and footer must occupy separate rows");
    await page.getByRole("button",{name:"下一页",exact:true}).click();
    const searchScroll=await page.locator(".wallpaper-grid").evaluate(grid=>{
      grid.scrollTop=grid.scrollHeight;
      return grid.scrollTop;
    });
    assert.ok(searchScroll>500);
    await page.locator(".wallpaper-card").last().click();
    await page.getByRole("button",{name:"返回壁纸列表",exact:true}).click();
    assert.ok(await page.locator(".wallpaper-search-results").isVisible());
    assert.equal(await page.locator(".SearchInput").inputValue(),"Forest");
    assert.equal(await page.locator('[data-wallpaper-page][aria-current="page"]').textContent(),"2");
    assert.equal(await page.locator(".wallpaper-grid").evaluate(grid=>grid.scrollTop),searchScroll,
      "search results restore the query, page and bottom-row position after detail");
    assert.deepEqual(errors,[]);
  } finally {
    await browser?.close();
    await new Promise(resolve=>server.close(resolve));
  }
});

for (const width of [1536,390]) {
  test(`wallpaper engagement sidebar and lifecycle at ${width}px`,{skip:!enabled},async()=>{
    const root=fileURLToPath(new URL("../",import.meta.url));
    const playwrightDirectory=process.env.LUME_PLAYWRIGHT_DIR ||
      path.join(homedir(),".cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright");
    const {chromium}=await import(pathToFileURL(path.join(playwrightDirectory,"index.mjs")).href);
    const {PNG}=createRequire(path.join(playwrightDirectory,"package.json"))("pngjs");
    const image=new PNG({width:3840,height:1620});
    const colors=[[229,55,74],[40,184,170],[100,140,230]];
    for (let y=0;y<image.height;y++) for (let x=0;x<image.width;x++) {
      const offset=(y*image.width+x)*4,color=colors[Math.floor(x/1280)];
      image.data[offset]=color[0];image.data[offset+1]=color[1];image.data[offset+2]=color[2];image.data[offset+3]=255;
    }
    const cover=PNG.sync.write(image);
    const server=createServer(async(request,response)=>{
      try {
        const pathname=decodeURIComponent(new URL(request.url,"http://localhost").pathname);
        const target=path.resolve(root,pathname==="/"?"index.html":pathname.slice(1));
        if(!target.startsWith(`${path.resolve(root)}${path.sep}`)){response.writeHead(403).end();return;}
        const type={".html":"text/html",".js":"text/javascript",".css":"text/css"}[path.extname(target)];
        response.writeHead(200,{"Content-Type":type || "application/octet-stream"}).end(await readFile(target));
      } catch {response.writeHead(404).end();}
    });
    await new Promise(resolve=>server.listen(0,"127.0.0.1",resolve));
    let browser;
    try {
      browser=await chromium.launch({headless:true,
        ...(process.env.LUME_BROWSER_EXECUTABLE?{executablePath:process.env.LUME_BROWSER_EXECUTABLE}:{})});
      const page=await browser.newPage({viewport:{width,height:width===390?844:864}});
      const errors=[],remoteRequests=[];
      page.on("pageerror",error=>errors.push(error.message));
      await page.route(/^https:\/\//,route=>{
        remoteRequests.push(route.request().url());
        return route.fulfill({status:200,contentType:"image/png",body:cover});
      });
      await page.addInitScript(()=>{
        const listeners=new Set(),url="https://static.lumedesktop.com/wide.png";
        const official={id:"official-a",title:"Wide Wallpaper With A Very Long Title Across Mountains And Lakes And A Quiet Morning",
          kind:"image",thumbnail:url,url,width:3840,height:1620,bytes:2048,libraryId:"downloaded-a"};
        window.wallpaperFixture={version:1,type:"wallpaper-state",revision:0,status:"idle",error:"",loadStatus:"fresh",
          playerAvailable:true,catalogStatus:"ready",catalogError:"",
          catalog:[official,{...official,id:"official-b",title:"Second wallpaper",libraryId:""}],
          library:[{id:"downloaded-a",title:official.title,kind:"image",thumbnail:url,
            url:"https://lume-wallpaper.local/wide.png",bytes:2048,missing:false,official:true,width:3840,height:1620},
            {id:"imported-a",title:"Imported image",kind:"image",thumbnail:url,
              url:"https://lume-wallpaper.local/imported.png",bytes:2048,missing:false,official:false}],
          displays:[{id:"1",name:"Screen",width:1920,height:1080,primary:true,wallpaperId:"",
            state:"stopped",autoPaused:false,fit:"cover",volume:0,error:""}],
          download:{id:"",status:"idle",received:0,total:0,error:""},pauseFullscreen:false,likedIds:["official-a"]};
        window.engagementFixture={version:1,type:"wallpaper-engagement-state",revision:0,id:"",
          status:"idle",likes:null,downloads:null,liked:false,canLike:false,busy:false,error:""};
        window.accountFixture={version:1,type:"account-state",revision:0,status:"signed-out",profile:null,busy:false,error:""};
        window.wallpaperCommands=[];
        window.emitHost=value=>listeners.forEach(listener=>listener({data:value}));
        window.emitEngagement=fields=>{
          window.engagementFixture={...window.engagementFixture,...fields,revision:window.engagementFixture.revision+1};
          window.emitHost(window.engagementFixture);
        };
        window.emitWallpaper=fields=>{
          window.wallpaperFixture={...window.wallpaperFixture,...fields,revision:window.wallpaperFixture.revision+1};
          window.emitHost(window.wallpaperFixture);
        };
        window.emitAccount=id=>{
          window.accountFixture={...window.accountFixture,revision:window.accountFixture.revision+1,
            status:id?"signed-in":"signed-out",profile:id?{id,displayName:"Fixture",avatarUrl:"",vip:false,vipExpiresAt:""}:null};
          window.emitHost(window.accountFixture);
        };
        window.finishStats=fields=>window.emitEngagement({status:"ready",likes:0,downloads:0,
          liked:false,canLike:window.accountFixture.status==="signed-in",busy:false,error:"",...fields});
        window.finishLike=()=>window.finishStats({liked:window.pendingLike.value,likes:window.pendingLike.value?1:0});
        window.chrome={webview:{
          addEventListener(name,listener){if(name==="message")listeners.add(listener);},
          removeEventListener(name,listener){listeners.delete(listener);},
          postMessage(command){
            window.wallpaperCommands.push(command);
            if(command==="v1|ready")queueMicrotask(()=>{
              window.emitHost(window.accountFixture);window.emitHost(window.wallpaperFixture);
            });
            if(command?.scope!=="wallpaper")return;
            queueMicrotask(()=>{
              if(command.action==="stats")window.emitEngagement({id:command.id,status:"loading",
                likes:null,downloads:null,liked:false,canLike:false,busy:false,error:""});
              else if(command.action==="endStats")window.emitEngagement({id:"",status:"idle",
                likes:null,downloads:null,liked:false,canLike:false,busy:false,error:""});
              else if(command.action==="like"){
                window.pendingLike=command;window.emitEngagement({busy:true});
              } else window.emitWallpaper({});
            });
          },
        }};
      });
      await page.goto(`http://127.0.0.1:${server.address().port}/`);
      await page.locator('.HomeHero [data-wallpaper-id="official-a"]').click();
      await page.waitForFunction(()=>window.wallpaperCommands.some(c=>c.action==="stats"),null,{timeout:1500});
      assert.deepEqual(await page.evaluate(()=>window.wallpaperCommands.filter(c=>c.action==="stats")),
        [{version:1,scope:"wallpaper",action:"stats",id:"official-a"}]);
      const heart=page.locator('[data-wallpaper-action="like"]');
      await page.locator('[data-wallpaper-panel="settings"]').click();
      const totals=()=>page.locator(".wallpaper-info-meta [data-wallpaper-count] strong").allTextContents();
      const screenshot=async name=>{
        if(!process.env.LUME_UI_SCREENSHOT_DIR)return;
        await mkdir(process.env.LUME_UI_SCREENSHOT_DIR,{recursive:true});
        await page.screenshot({path:path.join(process.env.LUME_UI_SCREENSHOT_DIR,`wallpaper-engagement-${width}-${name}.png`)});
      };
      assert.deepEqual(await totals(),["--","--"]);
      assert.ok(await heart.isDisabled());
      await screenshot("unknown");
      await page.evaluate(()=>window.finishStats());
      assert.deepEqual(await totals(),["0","0"]);
      assert.ok(await heart.isDisabled(),"guests may read totals but cannot like");
      assert.match(await heart.getAttribute("title"),/登录/);
      assert.equal(await heart.getAttribute("aria-pressed"),"false","legacy likedIds are not a fallback");
      assert.equal(await heart.textContent(),"","the heart is a command without a count");
      assert.equal(await page.locator("[data-wallpaper-count]").count(),2);
      assert.equal(await page.locator(".wallpaper-detail-bar [data-wallpaper-count]").count(),0);
      await screenshot("zero");
      await page.evaluate(()=>window.finishStats({likes:Number.MAX_SAFE_INTEGER,downloads:Number.MAX_SAFE_INTEGER}));
      assert.deepEqual(await totals(),["9007199254740991","9007199254740991"]);
      const layout=await page.locator(".wallpaper-info-thumbnail").evaluate(async area=>{
        const image=area.querySelector("img");await image.decode();
        const box=element=>{const r=element.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height,right:r.right,bottom:r.bottom};};
        const meta=area.querySelector(".wallpaper-info-meta"),title=area.querySelector("h3");
        return {area:box(area),image:box(image),meta:box(meta),title:box(title),overlay:box(title.parentElement),
          natural:[image.naturalWidth,image.naturalHeight],fit:getComputedStyle(image).objectFit,
          position:getComputedStyle(image).position,center:getComputedStyle(image).objectPosition,
          overflow:meta.scrollWidth>meta.clientWidth,wrap:getComputedStyle(meta).flexWrap,
          fonts:[...meta.querySelectorAll("strong,span")].map(e=>getComputedStyle(e).fontSize),
          rows:new Set([...meta.children].map(e=>e.getBoundingClientRect().y)).size,
          documentOverflow:document.documentElement.scrollWidth>innerWidth};
      });
      assert.deepEqual(layout.natural,[3840,1620]);
      assert.equal(layout.fit,"cover");assert.equal(layout.center,"50% 50%");assert.equal(layout.position,"absolute");
      assert.deepEqual(layout.image,layout.area,"the decoded wide image occupies the full fixed area");
      assert.ok(Math.abs(layout.area.height-layout.area.width*9/16)<1,"intrinsic image dimensions cannot grow the grid");
      assert.equal(layout.overflow,false);assert.equal(layout.wrap,"wrap");
      assert.ok(layout.fonts.every(font=>font==="11px"));assert.ok(layout.rows>1,"large totals wrap");
      assert.ok(layout.overlay.y>=layout.area.y && layout.overlay.bottom<=layout.area.bottom+1,
        `long title and wrapped totals stay inside the cover: ${JSON.stringify(layout)}`);
      assert.ok(layout.title.right<=layout.area.right && layout.meta.right<=layout.area.right);
      assert.equal(layout.documentOverflow,false);
      const pixels=PNG.sync.read(await page.locator(".wallpaper-info-thumbnail").screenshot());
      for (const [x,color] of [[3,colors[0]],[Math.floor(pixels.width/2),colors[1]],[pixels.width-4,colors[2]]]) {
        const offset=(2*pixels.width+x)*4;
        const actual=[...pixels.data.subarray(offset,offset+3)];
        assert.ok(actual.every((value,index)=>Math.abs(value-color[index])<=3),
          `cover has no top letterbox and remains centered at x=${x}: ${actual}`);
      }
      await screenshot("counts-cover-long-title");
      await page.evaluate(()=>{
        window.videoOrImageBeforeProgress=document.querySelector(".wallpaper-detail-poster");
        window.emitWallpaper({download:{id:"official-a",status:"running",received:10,total:100,error:""}});
        window.emitHost({version:1,type:"wallpaper-progress",
          download:{id:"official-a",status:"running",received:50,total:100,error:""}});
      });
      assert.deepEqual(await totals(),["9007199254740991","9007199254740991"]);
      assert.ok(await page.evaluate(()=>window.videoOrImageBeforeProgress===document.querySelector(".wallpaper-detail-poster")));
      await page.evaluate(()=>window.emitWallpaper({download:{id:"",status:"idle",received:0,total:0,error:""}}));
      assert.equal(await page.evaluate(()=>window.wallpaperCommands.filter(c=>c.action==="stats").length),1,
        "host snapshots and progress do not refetch or form a cycle");
      await page.evaluate(()=>window.emitAccount("account-1"));
      assert.ok(await heart.isDisabled(),"account changes disable immediately before native refetch");
      await page.evaluate(()=>window.finishStats({liked:true,likes:10,downloads:20}));
      assert.ok(await heart.isEnabled());assert.equal(await heart.getAttribute("aria-pressed"),"true");
      await heart.click();
      assert.deepEqual(await page.evaluate(()=>window.pendingLike),
        {version:1,scope:"wallpaper",action:"like",id:"official-a",value:false});
      assert.ok(await heart.isDisabled());assert.ok(await page.locator(".wallpaper-apply").isEnabled());
      await heart.evaluate(button=>button.click());
      assert.equal(await page.evaluate(()=>window.wallpaperCommands.filter(c=>c.action==="like").length),1);
      await page.evaluate(()=>window.finishLike());
      assert.ok(await heart.isEnabled());assert.equal(await heart.getAttribute("aria-pressed"),"false");
      await page.evaluate(()=>{
        window.emitEngagement({busy:true});
        window.emitWallpaper({status:"busy",download:{id:"official-a",status:"complete",received:100,total:100,error:""}});
        window.controlsBeforeRecordingNotice=document.querySelector(".wallpaper-detail-bar");
        window.posterBeforeRecordingNotice=document.querySelector(".wallpaper-detail-poster");
        window.emitHost({version:1,type:"wallpaper-engagement-notice",id:"official-b",error:"WALLPAPER_DOWNLOAD_STATS_FAILED"});
      });
      assert.ok(await page.locator("#notice").isVisible(),"recording notices are independent of selection and busy state");
      assert.equal(await page.locator("#notice").textContent(),"壁纸已下载，下载数量记录失败");
      assert.doesNotMatch(await page.locator("#notice").textContent(),/喜欢操作失败|Like operation failed|WALLPAPER_|\d/);
      assert.deepEqual(await totals(),["0","0"]);
      assert.ok(await heart.isDisabled());
      assert.ok(await page.locator(".wallpaper-apply").isDisabled(),"a recording notice does not settle wallpaper busy state");
      assert.ok(await page.evaluate(()=>window.controlsBeforeRecordingNotice===document.querySelector(".wallpaper-detail-bar") &&
        window.posterBeforeRecordingNotice===document.querySelector(".wallpaper-detail-poster")));
      assert.equal(await page.evaluate(()=>window.wallpaperFixture.library.some(item=>item.id==="downloaded-a")),true);
      assert.equal(await page.evaluate(()=>window.wallpaperFixture.download.status),"complete");
      await screenshot("recording-notice-busy-other-selection");
      await page.evaluate(()=>{
        window.emitEngagement({busy:false});
        window.emitWallpaper({status:"idle",download:{id:"",status:"idle",received:0,total:0,error:""}});
        window.emitEngagement({status:"failed",likes:null,downloads:null,error:"WALLPAPER_STATS_UNAVAILABLE"});
      });
      assert.deepEqual(await totals(),["--","--"]);assert.ok(await heart.isDisabled());
      assert.equal(await page.locator(".wallpaper-detail-notice").textContent(),"统计暂不可用，请稍后重试");
      await screenshot("failed");
      await page.evaluate(()=>window.finishStats({liked:true}));
      await page.evaluate(()=>window.emitAccount(""));
      assert.equal(await heart.getAttribute("aria-pressed"),"false");assert.ok(await heart.isDisabled());
      await page.evaluate(()=>window.finishStats());
      await page.evaluate(()=>window.emitAccount("account-2"));
      assert.ok(await heart.isDisabled());
      await page.evaluate(()=>window.finishStats({liked:true}));
      await page.evaluate(()=>window.emitAccount("account-3"));
      assert.equal(await heart.getAttribute("aria-pressed"),"false");assert.ok(await heart.isDisabled());
      await page.evaluate(()=>window.finishStats({likes:15,downloads:25}));
      await page.evaluate(()=>window.emitHost({...window.engagementFixture,revision:window.engagementFixture.revision-1,likes:777}));
      assert.deepEqual(await totals(),["15","25"]);
      assert.equal(await page.evaluate(()=>window.wallpaperCommands.filter(c=>c.action==="stats").length),1,
        "account publications never cause JS polling or account resend");
      await page.getByRole("button",{name:"返回壁纸列表",exact:true}).click();
      await page.getByRole("button",{name:"官方壁纸",exact:true}).click();
      await page.selectOption("#wallpaper-kind","image");
      assert.equal(await page.locator(".wallpaper-card [data-wallpaper-count]").count(),0);
      for (const [language,texts] of [
        ["zh-CN",["壁纸已下载，下载数量记录失败","今日下载额度已用尽"]],
        ["en",["Wallpaper downloaded, but recording the download count failed.",
          "Today's download quota is used up"]],
      ]) {
        await page.evaluate(language=>window.emitHost({version:1,type:"app-settings-state",
          revision:(window.noticeLanguageRevision=(window.noticeLanguageRevision || 0)+1),
          language,balancedMode:true,autostart:false,busy:false,error:"",
          update:{status:"idle",version:"",progress:0}}),language);
        for (const [index,error] of ["WALLPAPER_DOWNLOAD_STATS_FAILED","WALLPAPER_DOWNLOAD_STATS_QUOTA"].entries()) {
          await page.evaluate(error=>{
            window.gridBeforeRecordingNotice=document.querySelector(".wallpaper-grid");
            window.recordingNotice={version:1,type:"wallpaper-engagement-notice",id:"official-a",error};
            window.emitHost(window.recordingNotice);
          },error);
          const feedback=page.locator(index===0?"#notice":"[data-lume-modal-title]");
          assert.ok(await feedback.isVisible());
          assert.equal(await feedback.textContent(),texts[index]);
          assert.doesNotMatch(await feedback.textContent(),/喜欢操作失败|Like operation failed|WALLPAPER_|\d/);
          assert.ok(await page.evaluate(()=>window.gridBeforeRecordingNotice===document.querySelector(".wallpaper-grid")));
          await page.evaluate(()=>{
            window.emitHost({...window.recordingNotice,token:"private"});
            window.emitHost({...window.recordingNotice,error:"WALLPAPER_LIKE_FAILED"});
            window.emitHost({...window.recordingNotice,error:"UNKNOWN_PUBLIC_ERROR"});
            window.emitHost({...window.recordingNotice,likes:1234567,downloads:7654321});
          });
          assert.equal(await feedback.textContent(),texts[index],"invalid notices cannot replace public feedback");
        }
        await screenshot(`recording-notice-library-${language}`);
        await page.keyboard.press("Escape");
      }
      await page.evaluate(()=>window.emitHost({version:1,type:"app-settings-state",
        revision:++window.noticeLanguageRevision,language:"zh-CN",balancedMode:true,autostart:false,busy:false,error:"",
        update:{status:"idle",version:"",progress:0}}));
      await page.locator('.wallpaper-card[data-wallpaper-id="official-b"]').click();
      await page.locator('[data-wallpaper-panel="settings"]').click();
      assert.deepEqual(await totals(),["--","--"]);
      await page.evaluate(()=>window.emitEngagement({id:"official-a",status:"ready",likes:777,downloads:888,liked:true,canLike:true}));
      assert.deepEqual(await totals(),["--","--"]);assert.equal(await heart.getAttribute("aria-pressed"),"false");
      await page.evaluate(()=>window.finishStats({id:"official-b",likes:2,downloads:3}));
      assert.deepEqual(await totals(),["2","3"]);
      await page.getByRole("button",{name:"返回壁纸列表",exact:true}).click();
      await page.getByRole("button",{name:"我的壁纸",exact:true}).click();
      await page.locator('.wallpaper-card[data-wallpaper-id="downloaded-a"]').click();
      await page.locator('[data-wallpaper-panel="settings"]').click();
      await page.evaluate(()=>window.finishStats({id:"official-a",likes:4,downloads:5}));
      assert.deepEqual(await totals(),["4","5"]);
      assert.deepEqual(await page.evaluate(()=>window.wallpaperCommands.filter(c=>c.action==="stats").map(c=>c.id)),
        ["official-a","official-b","official-a"],"downloaded official artwork uses its canonical catalog ID");
      await page.getByRole("button",{name:"返回壁纸列表",exact:true}).click();
      await page.locator('.wallpaper-card[data-wallpaper-id="imported-a"]').click();
      await page.locator('[data-wallpaper-panel="settings"]').click();
      assert.equal(await heart.count(),0);assert.equal(await page.locator("[data-wallpaper-count]").count(),0);
      assert.equal(await page.evaluate(()=>window.wallpaperCommands.filter(c=>c.action==="stats").length),3);
      assert.equal(await page.evaluate(()=>window.wallpaperCommands.filter(c=>c.action==="endStats").length),3);
      assert.ok(remoteRequests.every(url=>/\/(?:wide|imported)\.png$/.test(url)),
        "the UI requests artwork only, never engagement HTTP or credentials");
      assert.deepEqual(errors,[]);
    } finally {
      await browser?.close();
      await new Promise(resolve=>server.close(resolve));
    }
  });
}
