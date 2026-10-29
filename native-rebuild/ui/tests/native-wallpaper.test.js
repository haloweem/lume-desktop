import test from "node:test";
import assert from "node:assert/strict";
import {readFile} from "node:fs/promises";
import {createBridge, validHostMessage} from "../bridge.js";
import {initialState, reduce} from "../state.js";
import {home, library, search, wallpaperCard, wallpaperDetail} from "../pages.js";
import * as model from "../wallpapers.js";
import {getLanguage,setLanguage} from "../localization.js";
import {escapeHTML} from "../dom.js";

const localUrl = "https://lume-wallpaper.local/library-1.mp4";
test("download admission distinguishes hard daily quota from authorization failure",()=>{
  const previous=getLanguage();
  try {
    setLanguage("zh-CN");
    assert.equal(model.wallpaperError("WALLPAPER_DOWNLOAD_QUOTA"),"今日下载额度已用尽");
    assert.match(model.wallpaperError("WALLPAPER_DOWNLOAD_AUTHORIZATION"),/额度校验失败/);
    setLanguage("en");
    assert.equal(model.wallpaperError("WALLPAPER_DOWNLOAD_QUOTA"),"Today's download quota is used up");
  } finally {setLanguage(previous);}
});
test("a temporary CDN failure allows retry on reopening",()=>{
  const url="https://static.lumedesktop.com/lume/wallpapers/official/thumbnails/reopen.jpg";
  model.rememberWallpaperAssetFailure(url,url);
  assert.equal(model.wallpaperAssetPreferredUrl(url),url);
});
test("browsing prioritizes the first eight covers and leaves the rest lazy",()=>{
  const catalog=Array.from({length:36},(_,i)=>item({id:`priority-${i}`}));
  const state=reduce(ready({catalog}),{type:'navigate',view:'wallpaperOfficial'});
  const html=library(state);
  assert.equal((html.match(/loading="eager"/g)||[]).length,8);
  assert.equal((html.match(/loading="lazy"/g)||[]).length,28);
});
test("buffering video keeps the poster and can start after the image timeout",async()=>{
  const {video,visible}=previewFixture();video.dataset={};let finish;
  video.play=()=>new Promise(resolve=>{finish=resolve;});
  const changes=[],errors=[];
  const controller=model.createWallpaperPreviewController({getVideo:()=>video,timeoutMs:15,
    onChange:v=>changes.push(v),onError:v=>errors.push(v)});
  const pending=controller.play(local(),visible);
  assert.equal(controller.playing,false);assert.equal(video.dataset.ready,undefined);
  await new Promise(resolve=>setTimeout(resolve,40));
  assert.equal(controller.playing,false);assert.equal(video.src,localUrl);assert.equal(errors.length,0);
  assert.ok(!changes.includes(true));finish();
  assert.equal(await pending,true);assert.equal(video.dataset.ready,"true");
  controller.dispose();assert.equal(video.dataset.ready,undefined);
});
test("leaving a hung preview settles immediately and ignores late play",async()=>{
  const {video,visible}=previewFixture();let finish;video.play=()=>new Promise(resolve=>{finish=resolve;});
  const changes=[];const controller=model.createWallpaperPreviewController({getVideo:()=>video,onChange:v=>changes.push(v)});
  const pending=controller.play(local(),visible);controller.stop();assert.equal(await pending,false);
  finish();await Promise.resolve();assert.equal(controller.playing,false);assert.ok(!changes.includes(true));
});
const officialUrl = "https://static.lumedesktop.com/wallpaper/forest.mp4";
const item = (fields = {}) => ({
  id:"official-1",title:"Forest",kind:"video",thumbnail:"https://static.lumedesktop.com/wallpaper/forest.jpg",
  url:officialUrl,width:1920,height:1080,bytes:1024,libraryId:"",...fields,
});
const local = (fields = {}) => ({
  id:"library-1",title:"My video",kind:"video",thumbnail:"",url:localUrl,bytes:1024,missing:false,...fields,
});
const display = (fields = {}) => ({
  id:"18446744073709551615",name:"主显示器",width:1920,height:1080,primary:true,wallpaperId:"",
  state:"stopped",autoPaused:false,fit:"cover",volume:0,error:"",...fields,
});
const message = (fields = {}) => ({
  version:1,type:"wallpaper-state",revision:0,status:"idle",error:"",loadStatus:"fresh",playerAvailable:true,
  catalogStatus:"ready",catalogError:"",catalog:[item()],library:[local()],displays:[display()],
  download:{id:"",status:"idle",received:0,total:0,error:""},pauseFullscreen:false,...fields,
});
const ready = fields => reduce(initialState(),{type:"host",message:message(fields)});
const engagement = (fields = {}) => ({
  version:1,type:"wallpaper-engagement-state",revision:0,id:"official-1",status:"ready",
  likes:0,downloads:0,liked:false,canLike:true,busy:false,error:"",...fields,
});
const engaged = (state,fields) => reduce(state,{type:"host",message:engagement(fields)});
const account = (fields = {}) => ({
  version:1,type:"account-state",revision:0,status:"signed-in",busy:false,error:"",
  profile:{id:"account-1",displayName:"Fixture",avatarUrl:"",vip:false,vipExpiresAt:""},...fields,
});
const sidebarOf = state => wallpaperDetail(state).match(/<aside[\s\S]*?<\/aside>/)?.[0] || "";
const engagementNotice = (fields = {}) => ({
  version:1,type:"wallpaper-engagement-notice",id:"official-1",error:"WALLPAPER_DOWNLOAD_STATS_FAILED",...fields,
});

test("public recording notices require exact fields and only the two recording failure codes", () => {
  for (const error of ["WALLPAPER_DOWNLOAD_STATS_FAILED","WALLPAPER_DOWNLOAD_STATS_QUOTA"]) {
    assert.equal(validHostMessage(engagementNotice({error})),true);
    assert.equal(validHostMessage(engagementNotice({error,id:"4k-retrolake-v.13.6"})),true);
  }
  for (const fields of [
    {version:2},{version:"1"},{type:"engagement-notice"},{id:""},{id:1},{id:"../x"},
    {id:"CON.txt"},{id:"x".repeat(97)},{error:""},{error:null},{error:1},
    {error:"WALLPAPER_LIKE_FAILED"},{error:"WALLPAPER_STATS_UNAVAILABLE"},{error:"AUTH_REQUIRED"},
    {error:"DOWNLOAD_STATS_FAILED"},{error:"network"},{error:"UNKNOWN_PUBLIC_ERROR"},
    {token:"private"},{accessToken:"private"},{profile:{id:"private"}},{revision:1},
    {likes:1234567},{downloads:7654321},{extra:true},
  ]) assert.equal(validHostMessage(engagementNotice(fields)),false,JSON.stringify(fields));
  for (const key of Object.keys(engagementNotice())) {
    const value=engagementNotice();
    delete value[key];
    assert.equal(validHostMessage(value),false,key);
  }
});

test("validated recording notices are delivered without reducing wallpaper success or engagement", () => {
  let receive;
  const messages=[],bridge=createBridge({addEventListener:(_name,listener)=>{receive=listener;}});
  const unsubscribe=bridge.subscribe(value=>messages.push(value));
  const notice=engagementNotice();
  receive({data:notice});
  receive({data:engagementNotice({token:"private"})});
  receive({data:engagementNotice({error:"WALLPAPER_LIKE_FAILED"})});
  assert.deepEqual(messages,[notice]);
  const successful=engaged(ready({catalog:[item({libraryId:"library-1"})],
    download:{id:"official-1",status:"complete",received:100,total:100,error:""}}),
    {likes:12,downloads:34});
  for (const state of [successful,reduce(successful,{type:"request",scope:"wallpaper",key:"apply"})]) {
    assert.equal(reduce(state,{type:"host",message:notice}),state);
  }
  unsubscribe();
});

const engagementFeedback=[
  ["WALLPAPER_STATS_UNAVAILABLE","统计暂不可用，请稍后重试","Statistics are unavailable. Please try again later."],
  ["WALLPAPER_LIKE_FAILED","喜欢操作失败，请稍后重试","Like operation failed. Please try again later."],
  ["AUTH_REQUIRED","登录 Lume 后可喜欢壁纸","Sign in to Lume to like wallpapers"],
  ["WALLPAPER_AUTH_REQUIRED","登录 Lume 后可喜欢壁纸","Sign in to Lume to like wallpapers"],
  ["WALLPAPER_DOWNLOAD_STATS_FAILED","壁纸已下载，下载数量记录失败","Wallpaper downloaded, but recording the download count failed."],
  ["DOWNLOAD_STATS_FAILED","壁纸已下载，下载数量记录失败","Wallpaper downloaded, but recording the download count failed."],
  ["WALLPAPER_DOWNLOAD_STATS_QUOTA","壁纸已下载，下载数量记录受服务端每日额度限制","Wallpaper downloaded, but recording the download count is limited by the server's daily quota."],
  ["DOWNLOAD_STATS_QUOTA","壁纸已下载，下载数量记录受服务端每日额度限制","Wallpaper downloaded, but recording the download count is limited by the server's daily quota."],
  ["UNKNOWN_PUBLIC_ERROR","互动信息暂不可用，请稍后重试","Engagement information is unavailable. Please try again later."],
];
for (const [error,chinese,english] of engagementFeedback) {
  test(`detail engagement feedback distinguishes ${error} without exposing codes or counts`,t=>{
    const language=getLanguage();
    t.after(()=>setLanguage(language));
    const state=engaged(reduce(ready({catalog:[item({libraryId:"library-1"})],
      download:{id:"official-1",status:"complete",received:100,total:100,error:""}}),
      {type:"wallpaper-select",source:"official",id:"official-1"}),
      {status:"failed",likes:null,downloads:null,error});
    assert.equal(validHostMessage(engagement({status:"failed",likes:null,downloads:null,error})),true);
    for (const [locale,expected] of [["zh-CN",chinese],["en",english]]) {
      setLanguage(locale);
      const html=wallpaperDetail(state);
      const note=html.match(/class="wallpaper-detail-notice" role="status">([^<]*)<\/div>/)?.[1];
      const text=model.wallpaperEngagementError(error);
      assert.equal(text,expected);
      assert.equal(note,escapeHTML(expected));
      assert.doesNotMatch(text,/WALLPAPER_|STATS_|AUTH_REQUIRED|UNKNOWN_PUBLIC_ERROR|\d/);
      if(error!=="WALLPAPER_LIKE_FAILED")assert.doesNotMatch(text,/喜欢操作失败|Like operation failed/);
      assert.equal((sidebarOf(state).match(/<strong>--<\/strong>/g)||[]).length,2);
      assert.equal(state.wallpaperStatus,"idle");
      assert.equal(state.wallpaper.download.status,"complete");
      assert.doesNotMatch(html,/data-wallpaper-panel="targets"[^>]*disabled/);
    }
  });
}

test("engagement messages accept only public fields, valid IDs and nullable safe unsigned totals", () => {
  for (const status of ["idle","loading","ready","failed"]) {
    for (const count of [null,0,Number.MAX_SAFE_INTEGER]) {
      assert.equal(validHostMessage(engagement({status,likes:count,downloads:count})),true);
    }
  }
  assert.equal(validHostMessage(engagement({id:""})),true);
  assert.equal(validHostMessage(engagement({id:"4k-retrolake-v.13.6",error:"network"})),true);
  for (const fields of [
    {version:2},{revision:-1},{revision:1.5},{revision:Number.MAX_SAFE_INTEGER+1},
    {id:"../x"},{id:"CON.txt"},{id:"x".repeat(97)},{status:"busy"},
    {likes:-1},{likes:0.5},{likes:"0"},{likes:Number.MAX_SAFE_INTEGER+1},
    {downloads:-1},{downloads:NaN},{downloads:Infinity},{downloads:true},
    {liked:1},{canLike:1},{busy:1},{error:1},{error:"https://private.invalid/token"},
    {error:"private\nmessage"},{token:"secret"},{profile:{id:"private"}},{extra:true},
  ]) assert.equal(validHostMessage(engagement(fields)),false,JSON.stringify(fields));
  for (const key of Object.keys(engagement())) {
    const value=engagement();
    delete value[key];
    assert.equal(validHostMessage(value),false,key);
  }
});

test("engagement reduction rejects stale revisions and survives wallpaper progress", () => {
  const start=initialState();
  assert.deepEqual(start.wallpaperEngagement,{
    revision:-1,id:"",status:"idle",likes:null,downloads:null,liked:false,canLike:false,busy:false,error:"",
  });
  let state=engaged(ready({download:{id:"official-1",status:"running",received:1,total:10,error:""}}),
    {revision:7,likes:12,downloads:34,liked:true});
  assert.equal(state.wallpaperEngagement.likes,12);
  assert.equal(state.wallpaperEngagement.liked,true);
  assert.equal(engaged(state,{revision:6,likes:99}),state);
  const previous=state.wallpaperEngagement;
  state=reduce(state,{type:"host",message:{version:1,type:"wallpaper-progress",
    download:{id:"official-1",status:"running",received:5,total:10,error:""}}});
  assert.equal(state.wallpaperEngagement,previous);
  state=reduce(state,{type:"host",message:message({revision:1})});
  assert.equal(state.wallpaperEngagement,previous);
});

test("selection and leaving detail clear engagement without losing its revision floor", () => {
  let state=reduce(ready(),{type:"wallpaper-select",source:"official",id:"official-1"});
  state=reduce(state,{type:"navigate",view:"wallpaperDetail"});
  state=engaged(state,{revision:8,likes:12,liked:true});
  assert.equal(reduce(state,{type:"wallpaper-select",source:"official",id:"official-1"}).wallpaperEngagement,
    state.wallpaperEngagement,"selecting the same item preserves its native state");
  state=reduce(state,{type:"wallpaper-select",source:"local",id:"library-1"});
  assert.equal(state.wallpaperEngagement.likes,null);
  assert.equal(state.wallpaperEngagement.liked,false);
  assert.equal(state.wallpaperEngagement.revision,8);
  assert.equal(engaged(state,{revision:7,liked:true}),state);
  state=engaged(state,{revision:9});
  state=reduce(state,{type:"back"});
  assert.equal(state.wallpaperEngagement.id,"");
  assert.equal(state.wallpaperEngagement.canLike,false);
  assert.equal(state.wallpaperEngagement.revision,9);
});

test("music clears detail engagement without changing idempotent music reentry", () => {
  let state=engaged({...ready(),view:"wallpaperDetail"},{revision:8,liked:true});
  state=reduce(state,{type:"navigate",view:"music"});
  assert.equal(state.wallpaperEngagement.liked,false);
  assert.equal(state.wallpaperEngagement.revision,8);
  assert.equal(reduce(state,{type:"navigate",view:"music"}),state);
});

test("account changes immediately clear account likes but repeated or stale accounts do not", () => {
  let state=reduce(ready(),{type:"host",message:account()});
  state=engaged(state,{revision:3,likes:12,liked:true});
  state=reduce(state,{type:"host",message:account({revision:1})});
  assert.equal(state.wallpaperEngagement.liked,true,"a profile refresh is not an account change");
  state=reduce(state,{type:"host",message:account({revision:2,profile:{...account().profile,id:"account-2"}})});
  assert.equal(state.wallpaperEngagement.liked,false);
  assert.equal(state.wallpaperEngagement.canLike,false);
  assert.equal(state.wallpaperEngagement.likes,12,"public totals do not belong to the account");
  state=engaged(state,{revision:4,liked:true});
  const previous=state;
  assert.equal(reduce(state,{type:"host",message:account({revision:1,status:"signed-out",profile:null})}),previous);
  state=reduce(state,{type:"host",message:account({revision:3,status:"signed-out",profile:null})});
  assert.equal(state.wallpaperEngagement.liked,false);
  assert.equal(state.wallpaperEngagement.canLike,false);
});

test("duplicate engagement cannot restore the previous account heart after logout", () => {
  let state=reduce(ready(),{type:"host",message:account()});
  const previous=engagement({revision:3,liked:true,canLike:true});
  state=reduce(state,{type:"host",message:previous});
  state=reduce(state,{type:"host",message:account({revision:1,status:"signed-out",profile:null})});
  assert.equal(reduce(state,{type:"host",message:previous}),state);
  assert.equal(engaged(state,{revision:4,liked:false,canLike:false}).wallpaperEngagement.canLike,false);
});

test("only matching ready native totals appear in the sidebar, including zero", () => {
  const selected=reduce(ready(),{type:"wallpaper-select",source:"official",id:"official-1"});
  for (const fields of [
    {likes:0,downloads:0},{likes:1234567,downloads:7654321},
    {likes:Number.MAX_SAFE_INTEGER,downloads:null},
  ]) {
    const state=engaged(selected,fields),html=wallpaperDetail(state),sidebar=sidebarOf(state);
    for (const key of ["likes","downloads"]) {
      assert.match(sidebar,new RegExp(`data-wallpaper-count="${key}"[^>]*>[\\s\\S]*?<strong>${fields[key] ?? "--"}<\\/strong>`));
    }
    assert.doesNotMatch(html.replace(sidebar,""),/data-wallpaper-count=/);
    assert.doesNotMatch(wallpaperCard(item()),/data-wallpaper-count=|1234567|7654321/);
  }
});

test("unknown, failed and mismatched engagement never leaks totals or another wallpaper heart", () => {
  const selected=reduce(ready({likedIds:["official-1"]}),{type:"wallpaper-select",source:"official",id:"official-1"});
  for (const fields of [
    {status:"idle"},{status:"loading"},{status:"failed",error:"network"},
    {error:"network"},{id:"official-2"},
  ]) {
    const state=engaged(selected,{...fields,likes:1234567,downloads:7654321,liked:true});
    assert.equal((sidebarOf(state).match(/<strong>--<\/strong>/g)||[]).length,2);
    assert.doesNotMatch(wallpaperDetail(state),/1234567|7654321/);
    assert.match(wallpaperDetail(state),/data-wallpaper-action="like"[^>]*aria-pressed="false"[^>]*disabled/);
  }
  assert.match(wallpaperDetail(selected),/data-wallpaper-action="like"[^>]*aria-pressed="false"[^>]*disabled/);
  assert.doesNotMatch(wallpaperDetail(selected),/保存在本机|saved on this device/);
  const imported=reduce(selected,{type:"wallpaper-select",source:"local",id:"library-1"});
  assert.doesNotMatch(sidebarOf(imported),/data-wallpaper-count=/);
});

test("native ready account capability and both busy states gate the heart", () => {
  const selected=reduce(ready(),{type:"wallpaper-select",source:"official",id:"official-1"});
  const state=engaged(selected,{liked:true});
  assert.match(wallpaperDetail(state),/data-wallpaper-action="like"[^>]*aria-pressed="true"/);
  assert.doesNotMatch(wallpaperDetail(state),/data-wallpaper-action="like"[^>]*disabled/);
  const guest=engaged(selected,{canLike:false});
  assert.match(wallpaperDetail(guest),/data-wallpaper-action="like"[^>]*title="[^"]*登录[^"]*"[^>]*disabled/);
  for (const busy of [
    engaged(selected,{busy:true}),reduce(state,{type:"request",scope:"wallpaper"}),
    reduce(state,{type:"unavailable",scope:"wallpaper"}),
    {...state,wallpaper:{...state.wallpaper,status:"busy"}},
  ]) assert.match(wallpaperDetail(busy),/data-wallpaper-action="like"[^>]*disabled/);
});

test("a like request locks only engagement until native settlement, not wallpaper controls", () => {
  let state=engaged(reduce(ready(),{type:"wallpaper-select",source:"official",id:"official-1"}));
  state=reduce(state,{type:"wallpaper-like-request",id:"official-1"});
  assert.equal(state.wallpaperEngagement.busy,true);
  assert.equal(state.wallpaperStatus,"idle");
  assert.match(wallpaperDetail(state),/data-wallpaper-action="like"[^>]*disabled/);
  assert.doesNotMatch(wallpaperDetail(state),/data-wallpaper-panel="targets"[^>]*disabled/);
  state=engaged(state,{revision:1,liked:true,likes:1});
  assert.equal(state.wallpaperEngagement.busy,false);
  assert.equal(state.wallpaperEngagement.liked,true);
});

test("sidebar artwork is absolute centered cover and small metadata wraps without horizontal scrolling", async () => {
  const css=await readFile(new URL("../app.css",import.meta.url),"utf8");
  assert.match(css,/\.wallpaper-info-thumbnail img\s*\{[^}]*position:\s*absolute[^}]*inset:\s*0[^}]*width:\s*100%[^}]*height:\s*100%[^}]*object-fit:\s*cover[^}]*object-position:\s*center/s);
  assert.match(css,/\.wallpaper-info-meta\s*\{[^}]*flex-wrap:\s*wrap/s);
  assert.doesNotMatch(css,/\.wallpaper-info-meta\s*\{[^}]*overflow-x:\s*auto/s);
  assert.match(css,/\.wallpaper-info-meta strong\s*\{[^}]*font-size:\s*11px/s);
});

test("wallpaper and desktop beauty scroll containers use the original per-surface scrollbars", async () => {
  const css=await readFile(new URL("../app.css",import.meta.url),"utf8");
  assert.match(css,/:where\([^)]*\.settings[^)]*\)\s*\{[^}]*scrollbar-width:\s*thin[^}]*scrollbar-color:\s*rgba\(96,\s*108,\s*124,\s*0\.72\)\s+rgba\(8,\s*10,\s*14,\s*0\.55\)/s);
  assert.match(css,/:where\([^)]*\.settings[^)]*\)::?-webkit-scrollbar\s*\{[^}]*width:\s*10px[^}]*height:\s*10px/s);
  assert.match(css,/:where\([^)]*\.settings[^)]*\)::?-webkit-scrollbar-track\s*\{[^}]*background:\s*rgba\(8,\s*10,\s*14,\s*0\.55\)/s);
  assert.match(css,/:where\([^)]*\.settings[^)]*\)::?-webkit-scrollbar-thumb\s*\{[^}]*min-height:\s*36px[^}]*border:\s*2px\s+solid\s+rgba\(8,\s*10,\s*14,\s*0\.55\)[^}]*background:\s*rgba\(96,\s*108,\s*124,\s*0\.72\)/s);
  assert.match(css,/\.wallpaper-grid\s*\{[^}]*scrollbar-width:\s*thin[^}]*scrollbar-color:\s*rgba\(255,\s*255,\s*255,\s*0\.14\)\s+transparent/s);
  assert.match(css,/\.wallpaper-grid::-webkit-scrollbar\s*\{[^}]*width:\s*4px/s);
  assert.match(css,/\.wallpaper-grid::-webkit-scrollbar-thumb\s*\{[^}]*background:\s*rgba\(255,\s*255,\s*255,\s*0\.14\)/s);
  assert.match(css,/:where\([^)]*\.CardGrid[^)]*\)/s);
});

test("wallpaper detail like action reserves the full original icon box without clipping the SVG", async () => {
  const css=await readFile(new URL("../app.css",import.meta.url),"utf8");
  assert.match(css, /\.wallpaper-detail-engagement\s+button\s*\{[^}]*width:\s*30px[^}]*height:\s*30px/s);
  assert.match(css, /\.wallpaper-detail-engagement\s+button\s*\{[^}]*overflow:\s*visible/s);
  assert.match(css, /\.wallpaper-detail-engagement\s+button\s+svg\s*\{[^}]*width:\s*1em[^}]*height:\s*1em/s);
  assert.match(css, /\.wallpaper-detail-engagement\s+button\s+svg\s*\{[^}]*overflow:\s*visible/s);
});

test("wallpaper commands are structured objects with only the defined action fields", () => {
  const sent=[],bridge=createBridge({postMessage:value=>sent.push(value)});
  assert.equal(typeof bridge.wallpaper,"function");
  const commands=[
    ["snapshot"],["refresh"],["import"],["stopAll"],["download",{id:"official-1"}],["cancel"],
    ["remove",{id:"library-1"}],["apply",{id:"library-1",display:"18446744073709551615"}],
    ["pause",{display:"1"}],["resume",{display:"1"}],["stop",{display:"1"}],
    ["fit",{display:"1",value:"cover"}],["fit",{display:"1",value:"contain"}],
    ["volume",{display:"1",value:0}],["volume",{display:"1",value:100}],
    ["pauseFullscreen",{value:false}],["pauseFullscreen",{value:true}],["preview",{id:"library-1"}],["endPreview"],
    ["like",{id:"official-1",value:true}],["like",{id:"official-1",value:false}],["applyAll",{id:"official-1"}],
    ["stats",{id:"official-1"}],["endStats"],
  ];
  for(const args of commands) assert.equal(bridge.wallpaper(...args),true,JSON.stringify(args));
  assert.deepEqual(sent,commands.map(([action,fields={}])=>({version:1,scope:"wallpaper",action,...fields})));
});

test("wallpaper commands reject paths, URLs, extra fields and non-uint64 display identities", () => {
  let sends=0;const bridge=createBridge({postMessage(){sends++;}});
  assert.equal(typeof bridge.wallpaper,"function");
  const invalid=[
    ["download",{id:"../outside"}],["download",{id:"x".repeat(97)}],["download",{id:""}],
    ["download",{id:"x",url:officialUrl}],["import",{path:"C:\\private.mp4"}],
    ["apply",{id:"x",display:1}],["apply",{id:"x",display:"18446744073709551616"}],
    ["apply",{id:"x",display:"01"}],["apply",{id:"x",display:"0"}],["apply",{id:"x",display:"-1"}],
    ["pause",{display:"1",args:"--script=x"}],["cancel",{id:"x"}],
    ["fit",{display:"1",value:"stretch"}],["volume",{display:"1",value:0.5}],
    ["volume",{display:"1",value:-1}],["volume",{display:"1",value:101}],
    ["pauseFullscreen",{value:1}],["preview",{id:"../media"}],["endPreview",{id:"x"}],
    ["like",{id:"official-1",value:1}],["like",{id:"official-1",value:true,extra:1}],
    ["applyAll",{id:"official-1",display:"1"}],
    ["snapshot",{extra:true}],["unknown"],["import",null],
    ["stats",{id:""}],["stats",{id:"../outside"}],["stats",{id:"x",token:"private"}],
    ["stats",{}],["endStats",{id:"x"}],
  ];
  for(const args of invalid) assert.equal(bridge.wallpaper(...args),false,JSON.stringify(args));
  assert.equal(sends,0);
  assert.equal(createBridge().wallpaper("snapshot"),false);
  assert.equal(createBridge({postMessage(){throw Error("offline");}}).wallpaper("snapshot"),false);
});

test("wallpaper snapshots validate complete records and approved media origins", () => {
  assert.equal(validHostMessage(message()),true);
  assert.equal(validHostMessage(message({
    catalog:[item({id:"4k-retrolake-v.13.6"})],
    library:[local({id:"library.4k-retrolake-v.13.6"})],
    likedIds:["4k-retrolake-v.13.6"],
    displays:[display({wallpaperId:"library.4k-retrolake-v.13.6"})],
    download:{id:"4k-retrolake-v.13.6",status:"idle",received:0,total:0,error:""},
  })),true,"official IDs may contain dots just like the native catalog policy");
  assert.equal(validHostMessage(message({catalog:[],library:[],displays:[]})),true);
  assert.equal(validHostMessage(message({library:[local({url:""})]})),true);
  assert.equal(validHostMessage(message({library:[local({official:true,width:1920,height:1080})]})),true);
  assert.equal(validHostMessage(message({library:[local({official:false,width:0,height:0})]})),true);
  assert.equal(validHostMessage(message({catalog:[item({url:"",libraryId:"library-1"})]})),true);
  assert.equal(validHostMessage(message({library:[local({url:`http://127.0.0.1:49152/${"a".repeat(64)}/media.mp4`})]})),true);
  assert.equal(validHostMessage(message({catalog:[item({url:"https://lume-12345-1444884137.cos.ap-nanjing.myqcloud.com/lume/video.mp4"})]})),true);
  const badFields=[
    {revision:-1},{revision:1.5},{extra:true},{status:"saved"},{pauseFullscreen:"false"},
    {playerAvailable:1},{loadStatus:"backup"},{catalogStatus:"complete"},
    {catalog:[item({id:"../x"})]},{catalog:[item({kind:"html"})]},
    {catalog:[item({url:"http://static.lumedesktop.com/a.mp4"})]},
    {catalog:[item({url:"https://static.lumedesktop.com.evil.invalid/a.mp4"})]},
    {catalog:[item({url:"https://user@static.lumedesktop.com/a.mp4"})]},
    {catalog:[item({thumbnail:"file:///C:/private.jpg"})]},
    {catalog:[item({url:"javascript:alert(1)"})]},
    {catalog:[item({url:""})]},
    {catalog:[item({width:-1})]},{catalog:[item({libraryId:"../x"})]},
    {catalog:[item(),item()]},{library:[local({url:"https://other.invalid/local.mp4"})]},
    {library:[local({missing:1})]},{library:[local({path:"C:\\private.mp4"})]},
    {library:[local({official:1})]},{library:[local({width:-1})]},{library:[local({height:"1080"})]},
    {library:[local({url:"http://127.0.0.1:49152/media.mp4"})]},
    {library:[local({url:`http://127.0.0.1:65536/${"a".repeat(64)}/media.mp4`})]},
    {library:[local({url:`http://localhost:49152/${"a".repeat(64)}/media.mp4`})]},
    {library:[local({url:`http://127.0.0.1:49152/${"a".repeat(64)}/media.mp4?extra=1`})]},
    {displays:[display({id:"18446744073709551616"})]},{displays:[display({volume:101})]},
    {displays:[display({id:"0"})]},
    {displays:[display({fit:"stretch"})]},{displays:[display({state:"unknown"})]},
    {download:{id:"x",status:"running",received:-1,total:100,error:""}},
    {download:{id:"x",status:"finished",received:100,total:100,error:""}},
  ];
  for(const fields of badFields)assert.equal(validHostMessage(message(fields)),false,JSON.stringify(fields));
});

test("partial wallpaper snapshots retain omitted collections and require complete initial state", () => {
  const {catalog,library,...partial}=message({revision:2,displays:[display({volume:55})]});
  assert.equal(validHostMessage(partial),true);
  assert.equal(validHostMessage({...partial,catalog}),true);
  assert.equal(validHostMessage({...partial,library}),true);
  assert.equal(validHostMessage({...partial,catalog:null}),false);
  assert.equal(validHostMessage({...partial,library:undefined}),false);
  const start=initialState();
  assert.equal(reduce(start,{type:"host",message:partial}),start);
  const previous=ready({revision:1});
  let state=reduce(previous,{type:"host",message:partial});
  assert.equal(state.wallpaper.catalog,previous.wallpaper.catalog);
  assert.equal(state.wallpaper.library,previous.wallpaper.library);
  assert.equal(state.wallpaper.displays[0].volume,55);
  state=reduce(state,{type:"host",message:{...partial,revision:3,library:[]}});
  assert.deepEqual(state.wallpaper.library,[]);
  assert.equal(state.wallpaper.catalog,previous.wallpaper.catalog);
  state=reduce(state,{type:"host",message:{...partial,revision:4,catalog:[]}});
  assert.deepEqual(state.wallpaper.catalog,[]);
  assert.deepEqual(state.wallpaper.library,[]);
});

test("legacy liked IDs remain protocol-compatible but are not authoritative engagement", () => {
  assert.equal(validHostMessage(message({likedIds:["official-1"]})),true);
  for (const likedIds of [null,["official-1","official-1"],["../x"],[1]]) {
    assert.equal(validHostMessage(message({likedIds})),false);
  }
  let state=ready({likedIds:["official-1"]});
  const previous=state.wallpaper.likedIds;
  state=reduce(state,{type:"request",scope:"wallpaper",key:"like"});
  assert.deepEqual(state.wallpaper.likedIds,["official-1"]);
  const {catalog,library,...partial}=message({revision:2});
  state=reduce(state,{type:"host",message:partial});
  assert.equal(state.wallpaper.likedIds,previous);
  state=reduce(state,{type:"host",message:{...partial,revision:3,likedIds:[]}});
  assert.deepEqual(state.wallpaper.likedIds,[]);
});

test("original detail controls keep liking and target selection separate from the preview control", () => {
  const state=engaged(reduce(ready(),{type:"wallpaper-select",source:"official",id:"official-1"}),{liked:true});
  const html=wallpaperDetail(state);
  assert.match(html,/data-wallpaper-action="like"[^>]*aria-pressed="true"/);
  assert.match(html,/data-wallpaper-panel="targets"[^>]*>[\s\S]*?下载并应用/);
  assert.doesNotMatch(html,/data-wallpaper-action="applyAll"/);
  assert.match(html,/data-wallpaper-action="apply"[^>]*data-wallpaper-display="18446744073709551615"/);
  assert.match(html,/wallpaper-info-sidebar/);
  assert.match(html,/data-wallpaper-panel="settings"/);
  assert.match(html,/data-icon="faArrowLeft"/);
  assert.match(html,/data-icon="faDownload"/);
  assert.match(html,/data-icon="faTrashCan"/);
  assert.match(html,/data-icon="Settings2"/);
  assert.match(html,/data-icon="faHeart"/);
  assert.match(html,/data-icon="faCropSimple"/);
  assert.match(html,/data-icon="faHardDrive"/);
  assert.doesNotMatch(html,/data-icon="faClock"/);
  assert.doesNotMatch(html,/深度强度|景深开关/);
  const localState=reduce(ready(),{type:"wallpaper-select",source:"local",id:"library-1"});
  assert.doesNotMatch(wallpaperDetail(localState),/data-wallpaper-action="like"/);
  const downloaded=engaged(reduce(ready({catalog:[item({libraryId:"library-1"})]}),
    {type:"wallpaper-select",source:"local",id:"library-1"}),{liked:true});
  assert.match(wallpaperDetail(downloaded),/data-wallpaper-action="like"[^>]*data-wallpaper-id="official-1"[^>]*aria-pressed="true"/);
  const archivedOfficial=reduce(ready({catalog:[],library:[local({official:true,width:3840,height:2160})]}),
    {type:"wallpaper-select",source:"local",id:"library-1"});
  assert.match(wallpaperDetail(archivedOfficial),/data-wallpaper-action="like"[^>]*data-wallpaper-id="library-1"/);
  assert.match(wallpaperDetail(archivedOfficial),/>3840x2160<\/strong>/);
  const imported=reduce(ready({catalog:[item({libraryId:"library-1"})],library:[local({official:false})]}),
    {type:"wallpaper-select",source:"local",id:"library-1"});
  assert.doesNotMatch(wallpaperDetail(imported),/data-wallpaper-action="like"/);
});

test("wallpaper info sidebar mirrors the original metadata-only surface", () => {
  const state=reduce(ready(),{type:"wallpaper-select",source:"official",id:"official-1"});
  const html=wallpaperDetail(state);
  const sidebar=html.match(/<aside class="wallpaper-info-sidebar"[\s\S]*?<\/aside>/)?.[0] || "";
  assert.notEqual(sidebar,"");
  assert.match(sidebar,/class="wallpaper-info-thumbnail"/);
  assert.match(sidebar,/class="wallpaper-info-overlay"/);
  assert.match(sidebar,/class="wallpaper-info-meta"/);
  assert.doesNotMatch(sidebar,/wallpaper-playback-settings|wallpaper-setting-row|wallpaper-screen-state|wallpaper-playback-buttons|wallpaper-volume|wallpaper-pauseFullscreen|wallpaper-stop-all/);
  assert.doesNotMatch(sidebar,/显示器|画面适配|壁纸音量|全屏应用时暂停|停止全部壁纸/);
});

test("cancellation requires confirmed playback and remains independently available per screen", () => {
  let state=reduce(ready({catalog:[item({libraryId:"library-1"})],
    displays:[display({wallpaperId:"library-1",state:"playing"})]}),
    {type:"wallpaper-select",source:"official",id:"official-1"});
  assert.match(wallpaperDetail(state),/class="wallpaper-apply[^"]*is-cancel[^"]*" data-wallpaper-action="stop" data-wallpaper-display="18446744073709551615"/);
  assert.match(wallpaperDetail(state),/>取消应用<\/span>/);
  state=reduce(state,{type:"host",message:message({revision:1,catalog:[item({libraryId:"library-1"})],
    displays:[display({wallpaperId:"library-1",state:"failed",error:"WALLPAPER_PLAYER_EXITED"})]})});
  assert.doesNotMatch(wallpaperDetail(state),/>取消应用<\/span>/);
  state=reduce(state,{type:"wallpaper-panel",panel:"targets"});
  assert.equal(state.wallpaperPanel,"targets");
  state=reduce(state,{type:"wallpaper-panel",panel:"settings"});
  assert.equal(state.wallpaperPanel,"settings");
  state=reduce(state,{type:"wallpaper-apply-target",target:"all"});
  state=reduce(state,{type:"host",message:message({revision:2,catalog:[item({libraryId:"library-1"})],
    displays:[display({wallpaperId:"library-1",state:"playing"}),display({id:"2",primary:false,state:"failed"})]})});
  assert.match(wallpaperDetail(state),/>取消应用<\/span>/);
  assert.match(wallpaperDetail(state),/data-wallpaper-action="stop" data-wallpaper-display="18446744073709551615"/);
  assert.match(wallpaperDetail(state),/data-wallpaper-action="apply" data-wallpaper-id="library-1" data-wallpaper-display="2"/);
  state=reduce(state,{type:"host",message:message({revision:3,catalog:[item({libraryId:"library-1"})],
    displays:[display({wallpaperId:"library-1",state:"playing"}),display({id:"2",primary:false,wallpaperId:"library-1",state:"paused"})]})});
  assert.match(wallpaperDetail(state),/>取消应用<\/span>/);
  const html=wallpaperDetail(state);
  assert.doesNotMatch(html,/全部显示器|主显示器|applyAll/);
  assert.match(html,/>取消屏幕1<\/button>/);
  assert.match(html,/>取消屏幕2<\/button>/);
});

test("screen target menu matches connected screens and never cancels another wallpaper", () => {
  const state=reduce(ready({catalog:[item({libraryId:"library-1"})],
    displays:[display({name:"\\\\.\\DISPLAY2",wallpaperId:"another-wallpaper",state:"playing"}),
      display({id:"2",name:"\\\\.\\DISPLAY1",primary:false})]}),
    {type:"wallpaper-select",source:"official",id:"official-1"});
  const html=wallpaperDetail(state);
  assert.equal((html.match(/role="menuitem"/g)||[]).length,2);
  assert.doesNotMatch(html,/DISPLAY|全部显示器|data-wallpaper-action="stop"/);
  assert.match(html,/>屏幕1<\/button>/);
  assert.match(html,/>屏幕2<\/button>/);
});

test("protected media URLs accept the native supported extensions with the same restricted lease path", () => {
  for (const extension of ["mp4","webm","mov","m4v","mkv","avi","jpg","jpeg","png","webp","gif","bmp","avif"]) {
    const url=`http://127.0.0.1:49152/${"a".repeat(64)}/media.${extension}`;
    assert.equal(validHostMessage(message({library:[local({url})]})),true,extension);
  }
  for (const suffix of ["html","svg","exe","mp4?key=1","mp4#fragment","../media.mp4"]) {
    const url=`http://127.0.0.1:49152/${"a".repeat(64)}/media.${suffix}`;
    assert.equal(validHostMessage(message({library:[local({url})]})),false,suffix);
  }
});

test("download progress snapshots update only the current transfer without replacing catalog or previews", () => {
  const progress={version:1,type:"wallpaper-progress",download:{id:"official-1",status:"running",received:700,total:1000,error:""}};
  assert.equal(validHostMessage(progress),true);
  assert.equal(validHostMessage({...progress,extra:true}),false);
  assert.equal(validHostMessage({...progress,download:{...progress.download,received:-1}}),false);
  let state=ready({revision:7,status:"busy",download:{id:"official-1",status:"running",received:100,total:1000,error:""}});
  const {catalog,library,displays}=state.wallpaper;
  state=reduce(state,{type:"host",message:progress});
  assert.equal(state.wallpaper.download.received,700);
  assert.equal(state.wallpaper.catalog,catalog);
  assert.equal(state.wallpaper.library,library);
  assert.equal(state.wallpaper.displays,displays);
  assert.equal(state.wallpaper.revision,7);
  assert.equal(state.wallpaperStatus,"saving");
  assert.equal(reduce(state,{type:"host",message:{...progress,download:{...progress.download,id:"old-transfer"}}}),state);
  state=reduce(state,{type:"host",message:message({revision:8,download:{id:"official-1",status:"complete",received:1000,total:1000,error:""}})});
  assert.equal(reduce(state,{type:"host",message:progress}),state);
});

test("wallpaper state starts empty and only advances to confirmed newer snapshots", () => {
  const start=initialState();
  assert.deepEqual(start.wallpaper?.library,[]);
  assert.deepEqual(start.wallpaper?.catalog,[]);
  assert.equal(start.wallpaper?.pauseFullscreen,false);
  let state=ready({revision:5});
  assert.equal(state.wallpaperDisplay,"18446744073709551615");
  state=reduce(state,{type:"request",scope:"wallpaper",key:"apply"});
  assert.equal(state.wallpaperStatus,"saving");
  assert.equal(state.wallpaper.displays[0].state,"stopped");
  state=reduce(state,{type:"host",message:message({revision:5})});
  assert.equal(state.wallpaperStatus,"saving");
  const pending=state;
  assert.equal(reduce(state,{type:"host",message:message({revision:4})}),pending);
  state=reduce(state,{type:"host",message:message({revision:6,status:"busy"})});
  assert.equal(state.wallpaperStatus,"saving");
  state=reduce(state,{type:"host",message:message({revision:7,displays:[display({state:"playing",wallpaperId:"library-1"})]})});
  assert.equal(state.wallpaperStatus,"idle");
  assert.equal(state.wallpaper.displays[0].state,"playing");
  assert.equal(state.wallpaperRequestRevision,null);
  assert.equal(state.status,"loading");
});

test("wallpaper failures and display removal settle independently of other modules", () => {
  let state=ready({revision:1,displays:[display(),display({id:"2",primary:false})]});
  state=reduce(state,{type:"wallpaper-display",display:"2"});
  assert.equal(state.wallpaperDisplay,"2");
  state=reduce(state,{type:"request",scope:"wallpaper",key:"apply"});
  state=reduce(state,{type:"host",message:message({revision:2,error:"player-start"})});
  assert.equal(state.wallpaperStatus,"error");
  assert.equal(state.wallpaperDisplay,"18446744073709551615");
  assert.equal(state.clock.enabled,false);
  assert.equal(reduce(state,{type:"unavailable",scope:"wallpaper"}).wallpaperStatus,"unavailable");
  assert.equal(reduce(state,{type:"unavailable",scope:"all"}).wallpaperStatus,"unavailable");
});

test("wallpaper query, kind and page changes preserve separate local and official browsing", () => {
  let state=ready();
  state=reduce(state,{type:"navigate",view:"wallpaperOfficial"});
  state=reduce(state,{type:"wallpaper-filter",kind:"image",query:"sun"});
  assert.deepEqual(state.wallpaperBrowse.official,{kind:"image",query:"sun",page:1});
  state=reduce(state,{type:"wallpaper-page",page:3});
  assert.equal(state.wallpaperBrowse.official.page,3);
  state=reduce(state,{type:"wallpaper-filter",query:"rain"});
  assert.equal(state.wallpaperBrowse.official.page,1);
  state=reduce(state,{type:"navigate",view:"wallpaperLocal"});
  assert.equal(state.wallpaperBrowse.local.query,"");
  assert.equal(state.wallpaperBrowse.local.page,1);
});

test("real catalog filtering and pagination use source records instead of bundled previews", () => {
  assert.equal(typeof model.wallpaperCollection,"function");
  let state=ready({catalog:Array.from({length:78},(_,n)=>item({id:`entry-${n}`,title:`Video ${n}`}))});
  state=reduce(state,{type:"navigate",view:"wallpaperOfficial"});
  let result=model.wallpaperCollection(state);
  assert.equal(result.items.length,36);
  assert.equal(result.pages,3);
  state=reduce(state,{type:"wallpaper-page",page:3});
  result=model.wallpaperCollection(state);
  assert.equal(result.items.length,6);
  assert.equal(result.items[0].id,"entry-72");
  state=reduce(state,{type:"wallpaper-filter",query:"video 77"});
  result=model.wallpaperCollection(state);
  assert.deepEqual(result.items.map(value=>value.id),["entry-77"]);
  state=reduce(state,{type:"wallpaper-filter",kind:"image"});
  assert.equal(model.wallpaperCollection(state).total,0);
});

test("home carousel uses official catalog records and opens a downloadable detail", () => {
  const state=reduce(ready({catalog:[item({id:"official-home",title:"Official home cover"})]}),
    {type:"navigate",view:"home"});
  const html=home(state);
  assert.match(html,/data-wallpaper-id="official-home"/);
  assert.match(html,/data-wallpaper-source="official"/);
  assert.doesNotMatch(html,/data-wallpaper="0"/);
  assert.doesNotMatch(html,/内置预览|assets\/forest-2\.jpg/);
  const detail=wallpaperDetail(reduce(state,{type:"wallpaper-select",source:"official",id:"official-home"}));
  assert.match(detail,/data-wallpaper-action="download"[^>]*data-wallpaper-id="official-home"/);
  assert.doesNotMatch(detail,/前往官方库/);
});

test("corrected catalog names agree across home, official listing, detail and search", () => {
  const catalog=[item({id:"forest-2",title:"Forest"}),
    item({id:"eb-sunflower-girl",title:"Sunflower Girl"})];
  const state=ready({catalog});
  const official=reduce(state,{type:"navigate",view:"wallpaperOfficial"});
  const listing=library(official);
  const slides=model.wallpaperHomeItems(state);
  for (const entry of catalog) {
    const carousel=home({...state,slide:slides.findIndex(value=>value.id===entry.id)});
    const detail=wallpaperDetail(reduce(official,
      {type:"wallpaper-select",source:"official",id:entry.id}));
    for (const html of [carousel,listing,detail]) {
      assert.match(html,new RegExp(entry.title));
      assert.doesNotMatch(html,/Forest 2|EB[+十]\s*Sunflower Girl/);
    }
    const searched=reduce(official,{type:"wallpaper-filter",query:entry.title});
    assert.deepEqual(model.wallpaperCollection(searched).items.map(value=>value.id),[entry.id]);
  }
});

test("home carousel follows the final reference-source order and uses hero thumbnails", () => {
  const cover=id=>`https://static.lumedesktop.com/lume/wallpapers/official/thumbnails/${id}.jpg`;
  const ordered=[
    item({id:"eb-sunflower-girl",title:"Sunflower Girl",homeOrder:99,thumbnail:cover("eb-sunflower-girl")}),
    item({id:"forest-2",title:"Forest",homeOrder:98,thumbnail:cover("forest-2")}),
    item({id:"girl-under-rain-4k",title:"Girl under Rain [4K]",homeOrder:97,thumbnail:cover("girl-under-rain-4k")}),
    item({id:"floating-in-space-by-visualdon",homeOrder:96,thumbnail:cover("floating-in-space-by-visualdon")}),
    item({id:"gargantua-bgm",homeOrder:95,thumbnail:cover("gargantua-bgm")}),
    item({id:"the-dock",title:"The Dock",homeOrder:94,thumbnail:cover("the-dock")}),
    item({id:"mountain-landscape-and-house-art-wallpaper",homeOrder:93,thumbnail:cover("mountain-landscape-and-house-art-wallpaper")}),
    item({id:"wavy-grass-4k60fps",homeOrder:92,thumbnail:cover("wavy-grass-4k60fps")}),
    item({id:"atlantic-ocean-4k",homeOrder:91,thumbnail:cover("atlantic-ocean-4k")}),
    item({id:"ender-miles-morales-snow-time-wuhd-4k",homeOrder:90,thumbnail:cover("ender-miles-morales-snow-time-wuhd-4k")}),
    item({id:"alps-mountains-windy-snowing",title:"Alps Mountains - Windy & Snowing",homeOrder:0,thumbnail:cover("alps-mountains-windy-snowing")}),
  ];
  const state=ready({catalog:ordered});
  assert.deepEqual(model.wallpaperHomeItems(state).map(entry=>entry.id),[
    "forest-2",
    "floating-in-space-by-visualdon",
    "gargantua-bgm",
    "the-dock",
    "mountain-landscape-and-house-art-wallpaper",
    "wavy-grass-4k60fps",
    "atlantic-ocean-4k",
    "girl-under-rain-4k",
    "ender-miles-morales-snow-time-wuhd-4k",
    "eb-sunflower-girl",
  ]);
  const dock=ordered.find(entry=>entry.id==="the-dock");
  assert.equal(model.wallpaperHeroImageUrl(dock),
    "https://static.lumedesktop.com/lume/wallpapers/official/hero-thumbnails/the-dock.jpg");
  const explicit={...dock,
    heroThumbnailUrl:"https://static.lumedesktop.com/lume/wallpapers/official/hero-thumbnails/explicit.jpg"};
  assert.equal(model.wallpaperHeroImageUrl(explicit),explicit.heroThumbnailUrl);
  const html=home({...state,slide:0});
  assert.match(html,/hero-thumbnails\/forest-2\.jpg/);
  assert.doesNotMatch(html,/official\/thumbnails\/forest-2\.jpg/);
  assert.doesNotMatch(html,/--image:url\(/,"the same cover is not decoded again as a CSS image");
});

test("home carousel falls back to actual directory order when no cover ranks are provided", () => {
  const catalog=[item({id:"directory-first"}),item({id:"directory-second"}),
    item({id:"directory-third"})];
  const state=ready({catalog});
  assert.deepEqual(model.wallpaperHomeItems(state).map(entry=>entry.id),catalog.map(entry=>entry.id));
});

test("official wallpaper assets load only through the CDN, never the COS bucket", () => {
  const staticUrl="https://static.lumedesktop.com/lume/wallpapers/official/thumbnails/asset-cache-test.jpg";
  const cosUrl="https://lume-12345-1444884137.cos.ap-nanjing.myqcloud.com/lume/wallpapers/official/thumbnails/asset-cache-test.jpg";
  assert.deepEqual(model.wallpaperAssetCandidates(staticUrl),[staticUrl]);
  assert.deepEqual(model.wallpaperAssetCandidates(cosUrl),[staticUrl]);
  assert.equal(model.wallpaperAssetFallbackUrl(staticUrl),"");
  model.rememberWallpaperAssetSuccess(staticUrl,staticUrl);
  assert.equal(model.wallpaperAssetPreferredUrl(staticUrl),staticUrl);
});

test("wallpaper surfaces render the CDN source without a COS fallback", () => {
  const source="https://static.lumedesktop.com/lume/wallpapers/official/thumbnails/surface-cache-test.jpg";
  const preferred=source;
  const cover=item({id:"surface-cache-test",thumbnail:source,heroThumbnail:source});
  const state=reduce(ready({catalog:[cover]}),{type:"wallpaper-select",source:"official",id:cover.id});
  const card=wallpaperCard(cover);
  const detail=wallpaperDetail(state);
  for (const html of [card,detail]) {
    assert.match(html,new RegExp(`src="${preferred.replace(/[.*+?^${}()|[\]\\]/g,"\\$&")}"`));
    assert.match(html,new RegExp(`data-wallpaper-asset-source="${source.replace(/[.*+?^${}()|[\]\\]/g,"\\$&")}"`));
    assert.doesNotMatch(html,/data-wallpaper-fallback-src|myqcloud/);
  }
});

test("COS and static aliases share the same remembered asset outcome", () => {
  const source="https://static.lumedesktop.com/lume/wallpapers/official/thumbnails/alias-cache-test.jpg";
  const cos="https://lume-12345-1444884137.cos.ap-nanjing.myqcloud.com/lume/wallpapers/official/thumbnails/alias-cache-test.jpg";
  assert.deepEqual(model.wallpaperAssetCandidates(cos),[source]);
  model.rememberWallpaperAssetSuccess(cos,source);
  assert.equal(model.wallpaperAssetPreferredUrl(source),source);
});

test("wallpaper preload targets stay bounded to visible covers", () => {
  const catalog=Array.from({length:20},(_,index)=>item({
    id:`preload-${index}`,
    thumbnail:`https://static.lumedesktop.com/lume/wallpapers/official/thumbnails/preload-${index}.jpg`,
    heroThumbnail:`https://static.lumedesktop.com/lume/wallpapers/official/hero-thumbnails/preload-${index}.jpg`,
  }));
  const homeState={...ready({catalog}),view:"home",slide:0};
  const homeTargets=model.wallpaperPreloadTargets(homeState);
  assert.equal(homeTargets.length,2);
  assert.deepEqual(homeTargets.map(target=>target.kind),["hero","hero"]);
  assert.ok(homeTargets.every(target=>target.url.includes("hero-thumbnails/")));
  const officialState={...ready({catalog}),view:"wallpaperOfficial"};
  const officialTargets=model.wallpaperPreloadTargets(officialState);
  assert.equal(officialTargets.length,8);
  assert.ok(officialTargets.every(target=>target.kind==="thumbnail"));
  assert.ok(officialTargets.every(target=>target.url.includes("thumbnails/")));
  assert.doesNotMatch(officialTargets.at(-1).url,/preload-8\.jpg/);
});

test("local wallpaper leases remain the only candidate", () => {
  const localMedia="https://lume-wallpaper.local/abc/media.mp4";
  assert.deepEqual(model.wallpaperAssetCandidates(localMedia),[localMedia]);
});

test("official cover metadata validates and survives partial native snapshots", () => {
  const heroThumbnail="https://static.lumedesktop.com/lume/wallpapers/official/hero-thumbnails/custom.jpg";
  const first=item({id:"cover-first",heroThumbnail,homeOrder:1});
  const second=item({id:"cover-second",homeOrder:0});
  const snapshot=message({catalog:[first,second]});
  assert.equal(validHostMessage(snapshot),true);
  assert.equal(validHostMessage({...snapshot,catalog:[{...first,heroThumbnail:"file:///C:/private.jpg"}]}),false);
  for (const homeOrder of [-1,0.5,20000,"0"])
    assert.equal(validHostMessage({...snapshot,catalog:[{...first,homeOrder}]}),false);
  let state=reduce(initialState(),{type:"host",message:snapshot});
  assert.deepEqual(model.wallpaperHomeItems(state).map(entry=>entry.id),["cover-second","cover-first"]);
  assert.equal(model.wallpaperHeroImageUrl(state.wallpaper.catalog[0]),heroThumbnail);
  const downloaded=message({catalog:[{...first,libraryId:"library-1"}],
    library:[local({official:true,heroThumbnail})]});
  assert.equal(validHostMessage(downloaded),true);
  const localState=reduce(ready({catalog:downloaded.catalog,library:downloaded.library}),
    {type:"wallpaper-select",source:"local",id:"library-1"});
  assert.equal(model.wallpaperHeroImageUrl(model.selectedWallpaper(localState)),heroThumbnail);
  const legacyLocal=reduce(ready({catalog:downloaded.catalog,library:[local({official:true})]}),
    {type:"wallpaper-select",source:"local",id:"library-1"});
  assert.equal(model.wallpaperHeroImageUrl(model.selectedWallpaper(legacyLocal)),heroThumbnail,
    "an old downloaded record can use the refreshed official cover");
  const {catalog,library,...partial}=message({revision:1});
  state=reduce(state,{type:"host",message:partial});
  assert.deepEqual(model.wallpaperHomeItems(state).map(entry=>entry.id),["cover-second","cover-first"]);
  assert.equal(model.wallpaperHeroImageUrl(state.wallpaper.catalog[0]),heroThumbnail);
  const oldSelection=state.wallpaperSelection;
  assert.equal(reduce(state,{type:"wallpaper-select",source:"builtin",id:"forest-2"}).wallpaperSelection,oldSelection);
});

test("official detail never renders the removed bundled preview path", async () => {
  const source=await readFile(new URL("../wallpapers.js",import.meta.url),"utf8");
  const pages=await readFile(new URL("../pages.js",import.meta.url),"utf8");
  assert.doesNotMatch(source,/assets\/[A-Za-z0-9-]+\.jpg|source\s*===\s*"builtin"|source\s*===\s*'builtin'/);
  assert.doesNotMatch(pages,/source\s*===\s*"builtin"|source\s*===\s*'builtin'|前往官方库/);
});

test("wallpaper images keep an explicit gradient fallback on card, detail and sidebar surfaces", () => {
  const state=reduce(ready(),{type:"wallpaper-select",source:"official",id:"official-1"});
  const card=wallpaperCard(item());
  const detail=wallpaperDetail(state);
  assert.match(card,/data-wallpaper-preview/);
  assert.match(card,/data-wallpaper-poster-fallback="true"/);
  assert.match(detail,/class="wallpaper-detail-placeholder"[^>]*data-wallpaper-poster-fallback="true"/);
  assert.match(detail,/class="wallpaper-detail-poster"[^>]*data-wallpaper-preview/);
  assert.match(detail,/data-wallpaper-info-fallback="true"/);
});
test("library renders true empty states, import, original pagination and no list video decoder", () => {
  let state=reduce(ready({library:[],catalog:[]}),{type:"navigate",view:"wallpaperLocal"});
  let html=library(state);
  assert.match(html,/data-wallpaper-action="import"/);
  assert.match(html,/尚未添加壁纸/);
  assert.doesNotMatch(html,/Forest|Floating In Space|内置预览|<video/);
  assert.doesNotMatch(html,/wallpaper-library-actions|刷新目录|停止全部壁纸/);
  state=reduce(ready(),{type:"navigate",view:"wallpaperOfficial"});
  html=library(state);
  assert.match(html,/data-wallpaper-id="official-1"/);
  assert.match(html,/data-wallpaper-source="official"/);
  assert.match(html,/data-wallpaper-page="1"/);
  assert.match(html,/loading="eager"/);
  assert.doesNotMatch(html,/wallpaper-library-actions|data-wallpaper-action="refresh"|data-wallpaper-action="stopAll"/);
  assert.doesNotMatch(html,/<video|autoplay|暂未接入/);
});

test("wallpaper cards use source type and file size metadata instead of resolution", () => {
  const html=wallpaperCard(item({bytes:37*1024*1024}));
  const metadata=html.match(/<div class="CardMeta">([\s\S]*?)<\/div>/)?.[1] || "";
  assert.match(metadata,/视频壁纸/);
  assert.match(metadata,/37\.0 MB/);
  assert.doesNotMatch(metadata,/1920\s*×\s*1080/);
});

test("wallpaper cards keep a poster fallback and tone gradient when the preview is unavailable", () => {
  const html=wallpaperCard(item({thumbnail:""}));
  assert.match(html,/class="wallpaper-card-placeholder video"/);
  assert.match(html,/data-wallpaper-poster-fallback="true"/);
  assert.match(html,/--wallpaper-tone-a:/);
  assert.match(html,/--wallpaper-tone-b:/);
  assert.doesNotMatch(html,/src="https:\/\/static\.lumedesktop\.com\/wallpaper\/forest\.mp4"/);
});

test("the custom wallpaper entry is a centered plus card without source-only copy", async () => {
  const state=reduce(ready({library:[],catalog:[]}),{type:"navigate",view:"wallpaperLocal"});
  const html=library(state);
  assert.match(html,/class="wallpaper-card wallpaper-import-card"/);
  assert.match(html,/class="wallpaper-import-plus"[^>]*><svg data-icon="Plus"/);
  assert.match(html,/data-wallpaper-drop-zone="local"/);
  assert.doesNotMatch(html,/从本地导入视频或图片/);
  const attribution=await readFile(new URL("../assets/sources/wallpaper-detail-icons.md",import.meta.url),"utf8");
  assert.match(attribution,/ISC License/);
  assert.match(attribution,/The MIT License \(MIT\)/);
  assert.match(attribution,/Copyright \(c\) 2013-present Cole Bemis/);
});

test("only my wallpaper exposes the native drop zone", () => {
  const local=library(reduce(ready({library:[],catalog:[]}),{type:"navigate",view:"wallpaperLocal"}));
  const official=library(reduce(ready(),{type:"navigate",view:"wallpaperOfficial"}));
  const detail=wallpaperDetail(reduce(ready(),{type:"wallpaper-select",source:"official",id:"official-1"}));
  assert.match(local,/data-wallpaper-drop-zone="local"/);
  assert.doesNotMatch(official,/data-wallpaper-drop-zone="local"/);
  assert.doesNotMatch(detail,/data-wallpaper-drop-zone="local"/);
});

test("wallpaper import affordance is vertically centered in its card", async () => {
  const css=await readFile(new URL("../app.css",import.meta.url),"utf8");
  assert.match(css,/\.wallpaper-import-plus\s*\{[^}]*top:\s*50%[^}]*transform:\s*translate\(-50%,\s*-50%\)/s);
});

test("page drag wiring delegates only blank content surfaces to the native window", async () => {
  const source=await readFile(new URL("../app.js",import.meta.url),"utf8");
  const interaction=await readFile(new URL("../window-interaction.js",import.meta.url),"utf8");
  assert.match(source,/content\.addEventListener\(\s*"pointerdown"/);
  assert.match(source,/isWindowDragTarget/);
  assert.match(interaction,/button|textarea|select|label/);
  assert.match(interaction,/wallpaper-grid|CardGrid|ModuleToolbar/);
});

test("a failed video catalog beside loaded images offers retry instead of an empty library", () => {
  const image=item({id:"static-1",kind:"image",url:"https://static.lumedesktop.com/lume/wallpapers/static/images/a.jpg"});
  for (const catalogError of ["video:WALLPAPER_HTTP_SEND","WALLPAPER_CATALOG_OFFLINE:video:WALLPAPER_HTTP_SEND"]) {
    const state=reduce(ready({catalogStatus:"ready",catalogError,catalog:[image]}),{type:"navigate",view:"wallpaperOfficial"});
    const html=library(state);
    assert.doesNotMatch(html,/暂无官方壁纸/);
    assert.match(html,/官方目录加载失败/);
    assert.match(html,/data-wallpaper-action="refresh"/);
  }
  assert.equal(model.wallpaperError("video:WALLPAPER_HTTP_SEND"),"动态壁纸：官方壁纸暂时无法访问，请稍后重试");
  const healthy=reduce(ready({catalogStatus:"ready",catalogError:"",catalog:[image]}),{type:"navigate",view:"wallpaperOfficial"});
  assert.match(library(healthy),/暂无官方壁纸/);
});

test("official wallpaper loading renders source-sized skeleton cards before catalog records arrive", () => {
  const state=reduce(ready({catalogStatus:"loading",catalog:[]}),{type:"navigate",view:"wallpaperOfficial"});
  const html=library(state);
  assert.equal((html.match(/wallpaper-card-loading/g) || []).length,36);
  assert.doesNotMatch(html,/正在读取官方壁纸|暂无官方壁纸/);
  assert.doesNotMatch(html,/data-wallpaper-page=/);
});

test("wallpaper pagination uses the original text controls and page summary", () => {
  const state=reduce(ready({catalog:Array.from({length:78},(_,n)=>item({id:`entry-${n}`}))}),
    {type:"navigate",view:"wallpaperOfficial"});
  const html=library(state);
  assert.match(html,/aria-label="上一页"[^>]*>上一页<\/button>/);
  assert.match(html,/aria-label="下一页"[^>]*>下一页<\/button>/);
  assert.match(html,/共 3 页，跳至/);
  assert.match(html,/>页<\/label>/);
  assert.doesNotMatch(html,/共 78 张|>‹<\/button>|>›<\/button>/);
});

test("wallpaper filters render dark glass menus with native value hooks", () => {
  const official=library(reduce(ready(),{type:"navigate",view:"wallpaperOfficial"}));
  assert.match(official,/data-wallpaper-select="wallpaper-kind"/);
  assert.match(official,/data-wallpaper-option="wallpaper-kind" data-value="video"/);
  assert.match(official,/data-wallpaper-option="wallpaper-kind" data-value="image"/);
  assert.match(official,/class="wallpaper-select-menu"[^>]*role="listbox"/);
  const detail=wallpaperDetail(reduce(ready(),{type:"wallpaper-select",source:"official",id:"official-1"}));
  assert.doesNotMatch(detail,/data-wallpaper-select="wallpaper-display"|data-wallpaper-select="wallpaper-fit"/);
  assert.match(detail,/data-wallpaper-action="apply"[^>]*data-wallpaper-display="18446744073709551615"/);
  assert.doesNotMatch(detail,/wallpaper-download-status|wallpaper-detail-download/);
});

test("search includes host catalog and local library but never advertises bundled previews as downloaded", () => {
  const state={...ready(),query:"video"};
  const html=search(state);
  assert.match(html,/My video/);
  assert.match(html,/data-wallpaper-source="local"/);
  assert.doesNotMatch(html,/内置壁纸预览|Floating In Space/);
});

test("detail shows download progress and recovers after failure, and application uses the downloaded library identity", () => {
  let state=reduce(ready(),{type:"wallpaper-select",source:"official",id:"official-1"});
  let html=wallpaperDetail(state);
  assert.match(html,/data-wallpaper-action="download"/);
  assert.match(html,/data-wallpaper-action="preview"/);
  assert.match(html,/<video[^>]*preload="none"/);
  assert.doesNotMatch(html,/<video[^>]*\ssrc=|autoplay/);
  state=reduce(state,{type:"host",message:message({revision:1,
    download:{id:"official-1",status:"running",received:500,total:1000,error:""}})});
  html=wallpaperDetail(state);
  assert.match(html,/class="wallpaper-apply"[^>]*>[\s\S]*50%/);
  assert.doesNotMatch(html,/data-wallpaper-action="cancel"|wallpaper-download-status|wallpaper-detail-download/);
  state=reduce(state,{type:"host",message:message({revision:2,
    download:{id:"official-1",status:"failed",received:500,total:1000,error:"network"}})});
  html=wallpaperDetail(state);
  assert.match(html,/data-wallpaper-action="download"[^>]*data-wallpaper-id="official-1"/);
  assert.doesNotMatch(html,/data-wallpaper-action="download"[^>]*disabled/);
  state=reduce(state,{type:"host",message:message({revision:3,catalog:[item({libraryId:"library-1"})]})});
  html=wallpaperDetail(state);
  assert.match(html,/data-wallpaper-action="apply"[^>]*data-wallpaper-id="library-1"/);
  assert.match(html,/data-wallpaper-action="apply"[^>]*data-wallpaper-display="18446744073709551615"/);
  assert.doesNotMatch(html,/id="wallpaper-volume"|data-wallpaper-action="pauseFullscreen"|id="wallpaper-fit"/);
  assert.doesNotMatch(html,/wallpaper-download-status|wallpaper-detail-download/);
});

test("missing files and unavailable player never present an enabled apply action", () => {
  let state=reduce(ready({library:[local({missing:true,url:""})]}),{type:"wallpaper-select",source:"local",id:"library-1"});
  assert.match(wallpaperDetail(state),/文件已丢失/);
  assert.match(wallpaperDetail(state),/data-wallpaper-action="apply"[^>]*disabled/);
  state=reduce(ready({playerAvailable:false}),{type:"wallpaper-select",source:"local",id:"library-1"});
  assert.match(wallpaperDetail(state),/播放器不可用/);
  assert.match(wallpaperDetail(state),/data-wallpaper-action="apply"[^>]*disabled/);
});

test("static images remain applicable without the video player", () => {
  const image=local({kind:"image",title:"Native image",url:"https://lume-wallpaper.local/photo.png"});
  const localState=reduce(ready({playerAvailable:false,library:[image]}),
    {type:"wallpaper-select",source:"local",id:image.id});
  const downloadedState=reduce(ready({playerAvailable:false,
    catalog:[item({kind:"image",libraryId:image.id})],library:[image]}),
    {type:"wallpaper-select",source:"official",id:"official-1"});
  for (const state of [localState,downloadedState]) {
    const html=wallpaperDetail(state);
    assert.match(html,/data-wallpaper-action="apply"/);
    assert.doesNotMatch(html,/data-wallpaper-action="apply"[^>]*disabled/);
    assert.doesNotMatch(html,/播放器不可用|Player unavailable/);
  }
});

test("missing static image files remain disabled without the video player", () => {
  const state=reduce(ready({playerAvailable:false,
    library:[local({kind:"image",missing:true,url:""})]}),
    {type:"wallpaper-select",source:"local",id:"library-1"});
  const html=wallpaperDetail(state);
  assert.match(html,/文件已丢失/);
  assert.match(html,/data-wallpaper-action="apply"[^>]*disabled/);
});

test("downloaded catalog entries use missing local media state for preview availability", () => {
  const state=reduce(ready({catalog:[item({libraryId:"library-1"})],library:[local({missing:true,url:""})]}),
    {type:"wallpaper-select",source:"official",id:"official-1"});
  assert.match(wallpaperDetail(state),/data-wallpaper-action="preview"[^>]*disabled/);
});

test("unleased downloaded media can be applied and preview requested without exposing a path", () => {
  const state=reduce(ready({library:[local({url:""})]}),{type:"wallpaper-select",source:"local",id:"library-1"});
  const html=wallpaperDetail(state);
  assert.doesNotMatch(html,/data-wallpaper-action="apply"[^>]*disabled/);
  assert.doesNotMatch(html,/data-wallpaper-action="preview"[^>]*disabled/);
});

test("detail escapes untrusted titles and errors instead of injecting markup", () => {
  const state=reduce(ready({library:[local({title:'<img src=x onerror="run()">'})],error:"<script>bad()</script>"}),
    {type:"wallpaper-select",source:"local",id:"library-1"});
  const html=wallpaperDetail(state);
  assert.doesNotMatch(html,/<img src=x|<script>/);
  assert.match(html,/&lt;img/);
});

test("native wallpaper failures present clear recovery actions in Simplified Chinese", () => {
  const messages={
    WALLPAPER_PLAYER_EXITED:"壁纸播放器意外退出，请点击“应用”重试",
    WALLPAPER_RUNTIME_FAILED:"壁纸服务异常，请重新启动 Lume",
    WALLPAPER_BUSY:"正在处理壁纸，请稍候",
    WALLPAPER_SAVE_FAILED:"壁纸设置保存失败，请重试",
    WALLPAPER_FAVORITES_LOAD:"本地喜欢记录读取失败，请重试",
    WALLPAPER_FAVORITES_SAVE:"本地喜欢记录保存失败，请重试",
    WALLPAPER_CATALOG_CONFLICT:"官方壁纸信息已更改，请刷新目录后重试",
    WALLPAPER_MEDIA_IN_USE:"这张壁纸正在使用，请先停止壁纸和预览",
    WALLPAPER_DISPLAY_MISSING:"所选显示器已断开，请重新选择显示器",
    WALLPAPER_PLAYER_MISSING:"播放器不可用，请检查安装文件",
    WALLPAPER_HTTP_503:"官方壁纸暂时无法访问，请稍后重试",
  };
  for (const [code,text] of Object.entries(messages)) assert.equal(model.wallpaperError(code),text);
  const state=reduce(ready({catalogStatus:"failed",catalogError:"WALLPAPER_HTTP_503"}),
    {type:"navigate",view:"wallpaperOfficial"});
  assert.match(library(state),/官方壁纸暂时无法访问，请稍后重试/);
  assert.doesNotMatch(library(state),/WALLPAPER_HTTP_503/);
});

test("normal cached catalogs stay quiet without hiding wallpapers or pagination", () => {
  assert.equal(model.wallpaperError("WALLPAPER_CATALOG_CACHED"),"");
  const state=reduce(ready({catalogStatus:"ready",catalogError:"WALLPAPER_CATALOG_CACHED"}),
    {type:"navigate",view:"wallpaperOfficial"});
  const html=library(state);
  assert.doesNotMatch(html,/已载入本地壁纸目录|刷新目录|wallpaper-inline-notice|WALLPAPER_CATALOG_CACHED/);
  assert.match(html,/data-wallpaper-id="official-1"/);
  assert.match(html,/aria-label="壁纸分页"/);
  state.wallpaper.error="WALLPAPER_IMPORT_FORMAT";
  assert.match(library(state),/不支持此壁纸格式/);
});

test("catalog failures remain visible when cached records are still usable", () => {
  const messages={
    "WALLPAPER_CATALOG_OFFLINE:WALLPAPER_HTTP_SEND":"暂时无法更新官方目录，正在显示已缓存的壁纸",
    "WALLPAPER_CATALOG_CACHE_SAVE:WALLPAPER_LIBRARY_WRITE":"官方目录已更新，但本地缓存保存失败",
    "video:WALLPAPER_HTTP_503":"动态壁纸：官方壁纸暂时无法访问，请稍后重试",
    "image:WALLPAPER_HTTP_503":"静态壁纸：官方壁纸暂时无法访问，请稍后重试",
  };
  for (const [code,text] of Object.entries(messages)) {
    assert.equal(model.wallpaperError(code),text);
    const state=reduce(ready({catalogStatus:"ready",catalogError:code}),{type:"navigate",view:"wallpaperOfficial"});
    assert.ok(library(state).includes(text));
    assert.doesNotMatch(library(state),/WALLPAPER_/);
    assert.match(library(state),/data-wallpaper-id="official-1"/);
  }
});

function previewFixture() {
  const events=[];
  const video={src:"",pause(){events.push("pause");},play(){events.push("play");return Promise.resolve();},
    removeAttribute(name){events.push(`remove:${name}`);if(name==="src")this.src="";},load(){events.push("load");}};
  const controller=model.createWallpaperPreviewController({getVideo:()=>video});
  const visible={view:"wallpaperDetail",nativeVisible:true,documentVisible:true,id:"library-1"};
  return {video,events,controller,visible};
}

test("preview controller attaches only on request and hiding or navigating releases its media source", async () => {
  assert.equal(typeof model.createWallpaperPreviewController,"function");
  const {video,events,controller,visible}=previewFixture();
  controller.update(visible);
  assert.equal(video.src,"");
  assert.deepEqual(events,[]);
  assert.equal(await controller.play(local(),visible),true);
  assert.equal(video.src,localUrl);
  assert.equal(events.filter(value=>value==="play").length,1);
  controller.update({...visible,nativeVisible:false});
  assert.equal(video.src,"");
  assert.deepEqual(events.slice(-3),["pause","remove:src","load"]);
  controller.update(visible);
  assert.equal(video.src,"");
  await controller.play(local(),visible);
  controller.update({...visible,view:"wallpaperOfficial"});
  assert.equal(video.src,"");
  controller.dispose();
});

test("preview rejects hidden, untrusted or absent media, including pending play after disposal", async () => {
  assert.equal(typeof model.createWallpaperPreviewController,"function");
  const {video,controller,visible}=previewFixture();
  assert.equal(await controller.play(local(),{...visible,documentVisible:false}),false);
  assert.equal(await controller.play(local({url:"file:///C:/secret.mp4"}),visible),false);
  assert.equal(await controller.play(local({kind:"image"}),visible),false);
  assert.equal(video.src,"");
  let settle;
  video.play=()=>new Promise(resolve=>{settle=resolve;});
  const pending=controller.play(local(),visible);
  controller.dispose();
  settle();
  assert.equal(await pending,false);
  assert.equal(video.src,"");
});

test("preview releases a removed selection and reports failed play without retaining its source", async () => {
  const {video,controller,visible}=previewFixture();
  await controller.play(local(),visible);
  controller.update({...visible,id:""});
  assert.equal(video.src,"");
  video.play=()=>Promise.reject(Error("decode"));
  assert.equal(await controller.play(local(),visible),false);
  assert.equal(video.src,"");
});

test("image detail acquires the full media source and hiding releases the image decoder", async () => {
  const image={src:"",decodes:0,decode(){this.decodes++;return Promise.resolve();},
    removeAttribute(name){if(name==="src")this.src="";}};
  const controller=model.createWallpaperPreviewController({getVideo:()=>null,getImage:()=>image});
  const visible={view:"wallpaperDetail",nativeVisible:true,documentVisible:true,id:"library-1"};
  const full="https://lume-wallpaper.local/full-image.jpg";
  assert.equal(await controller.play(local({kind:"image",thumbnail:"https://lume-wallpaper.local/thumb.jpg",url:full}),visible),true);
  assert.equal(image.src,full);
  assert.equal(image.decodes,1);
  controller.update(visible);
  assert.equal(image.decodes,1);
  controller.update({...visible,documentVisible:false});
  assert.equal(image.src,"");
});

test("wallpaper CSP permits only trusted local media mappings and the official CDN", async () => {
  const html=await readFile(new URL("../index.html",import.meta.url),"utf8");
  const images=html.match(/img-src ([^;]+);/)[1].split(/\s+/);
  assert.deepEqual(images,["'self'","data:","https://lume-music.local",
    "https://lume-wallpaper.local","https://static.lumedesktop.com",
    "https://lume-12345-1444884137.cos.ap-nanjing.myqcloud.com",
    "https://p1.music.126.net","https://p2.music.126.net","https://p3.music.126.net","https://p4.music.126.net",
    "https://y.gtimg.cn","https://y.qq.com","https://qpic.y.qq.com","https://q1.qlogo.cn","https://q2.qlogo.cn","https://thirdqq.qlogo.cn",
    "https://imge.kugou.com","https://imgessl.kugou.com","https://img.kgimg.com","http://127.0.0.1:*"]);
  assert.match(html,/media-src https:\/\/lume-music\.local https:\/\/lume-wallpaper\.local https:\/\/static\.lumedesktop\.com https:\/\/lume-12345-1444884137\.cos\.ap-nanjing\.myqcloud\.com http:\/\/127\.0\.0\.1:\*;/);
  assert.match(html,/connect-src 'self' https:\/\/lume-music\.local https:\/\/lume-fonts\.local http:\/\/127\.0\.0\.1:\*;/);
  assert.doesNotMatch(html,/media-src\s+\*|img-src[^;]*https:;/);
});
