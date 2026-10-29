// Test-only host fixture. No preview commands or account/network access in the product.
import assert from "node:assert/strict";
import {createRequire} from "node:module";
import {fileURLToPath} from "node:url";
import fs from "node:fs";
import path from "node:path";
import http from "node:http";

const require = createRequire(import.meta.url);
const {chromium} = require(path.resolve(process.argv[2]));
const {PNG} = require(path.resolve(process.argv[2], "../pngjs"));
const ui = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const output = path.resolve(process.argv[3]);
fs.mkdirSync(output, {recursive: true});
function installFixture() {
  const listeners = new Set(), commands = [];
  let settings = {version:1,type:"app-settings-state",revision:1,language:"zh-CN",balancedMode:true,
    autostart:false,busy:false,error:"",update:{status:"idle",version:"",progress:0}};
  let account = {version:1,type:"account-state",revision:1,status:"signed-out",busy:false,error:"",profile:null};
  let wallpaperRevision=0;
  const receive = message => listeners.forEach(listener => listener({data:message}));
  window.harness = {
    commands, receive,
    settings(fields) {settings = {...settings,...fields,revision:settings.revision+1};receive(settings);},
    account(signedIn) {
      account = {...account,revision:account.revision+1,status:signedIn?"signed-in":"signed-out",
        profile:signedIn?{id:"fixture-user",displayName:"测试用户",avatarUrl:"",vip:false,vipExpiresAt:""}:null};
      receive(account);
    },
    quota() {receive({version:1,type:"wallpaper-engagement-notice",id:"fixture",error:"WALLPAPER_DOWNLOAD_STATS_QUOTA"});},
    hardQuota(error="WALLPAPER_DOWNLOAD_QUOTA") {receive({version:1,type:"wallpaper-state",revision:++wallpaperRevision,
      status:"idle",error:"",loadStatus:"loaded",playerAvailable:true,catalogStatus:"ready",catalogError:"",
      catalog:[],library:[],displays:[],pauseFullscreen:false,
      download:{id:"fixture",status:"failed",received:0,total:100,error}});},
  };
  window.chrome = {webview:{
    addEventListener(type,listener) {if(type==="message")listeners.add(listener);},
    removeEventListener(type,listener) {if(type==="message")listeners.delete(listener);},
    postMessage(command) {
      if(window.harness.rejectUpdates && ["downloadUpdate","installUpdate"].includes(command?.action))
        throw new Error("fixture disconnected");
      commands.push(command);
      if(command==="v1|ready")queueMicrotask(()=>{receive(settings);receive(account);});
      if(command?.scope==="app-settings" && command.action==="snapshot")queueMicrotask(()=>receive(settings));
      if(command?.scope==="account" && command.action==="snapshot")queueMicrotask(()=>receive(account));
    },
  }};
}
const server = http.createServer((req,res) => {
  const route = new URL(req.url,"http://localhost").pathname;
  if(route === "/modal-fixture") {
    res.setHeader("Content-Type","text/html;charset=utf-8");
    res.end('<!doctype html><html lang="zh-CN"><head><link rel="stylesheet" href="/app.css"></head><body><button id="anchor">Focus target</button></body></html>');
    return;
  }
  const file = path.resolve(ui,"."+(route==="/"?"/index.html":route));
  if(!file.startsWith(ui+path.sep) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {
    res.writeHead(404).end();return;
  }
  res.setHeader("Content-Type",({".html":"text/html;charset=utf-8",".js":"text/javascript",".css":"text/css",
    ".webp":"image/webp",".png":"image/png",".ttf":"font/ttf"})[path.extname(file)] || "application/octet-stream");
  res.end(fs.readFileSync(file));
});
await new Promise(resolve => server.listen(0,"127.0.0.1",resolve));
const url = `http://127.0.0.1:${server.address().port}`;
let browser;
const errors = [], remote = [];
try {
  browser = await chromium.launch({headless:true,channel:"msedge"});
  const unit = await browser.newPage();
  unit.on("pageerror",e=>errors.push(e.message));
  await unit.goto(url+"/modal-fixture");
  await unit.evaluate(async () => {
    const {createLumeModalController} = await import("/lume-modal.js");
    window.calls = [];
    window.modal = createLumeModalController(document,{
      onLogin:()=>calls.push("login"),onUpdateDownload:()=>calls.push("download"),onUpdateInstall:()=>calls.push("install"),
    });
    document.querySelector("#anchor").focus();
    modal.showQuota(false);
  });
  const dialog = unit.locator(".lume-modal"), primary = unit.locator("[data-lume-modal-primary]");
  assert.equal(await unit.locator("[data-lume-modal-body]").textContent(),"游客共可免费下载 10 张，注册登录后每天可下载 10 张");
  assert.equal(await dialog.evaluate(e=>e.matches(":modal")),true);
  await primary.click();
  assert.deepEqual(await unit.evaluate(()=>calls),["login"]);
  assert.equal(await unit.evaluate(()=>document.activeElement.id),"anchor");
  await unit.evaluate(()=>{modal.showQuota(false);modal.close();modal.showQuota(true);});
  await unit.waitForTimeout(80);
  assert.equal(await primary.evaluate(e=>document.activeElement===e),true,"old close events cannot steal new dialog focus");
  await unit.keyboard.press("Escape");
  assert.equal(await unit.evaluate(()=>document.activeElement.id),"anchor");
  await unit.evaluate(()=>{modal.close();modal.showQuota(false);modal.showQuota(true);modal.setLanguage("en");});
  assert.equal(await primary.textContent(),"OK");
  await unit.keyboard.press("Escape");
  assert.equal(await unit.evaluate(()=>document.activeElement.id),"anchor");
  await unit.evaluate(()=>modal.showUpdate({status:"available",version:"<img src=x onerror=alert(1)>"}));
  assert.equal(await dialog.locator("img").count(),0,"version is plain text");
  await unit.evaluate(()=>{modal.close();modal.setLanguage("zh-CN");modal.showUpdate({status:"ready",version:"1.0.2.2"});});
  assert.equal(await primary.textContent(),"立即安装");
  assert.equal(await unit.locator("[data-lume-modal-body]").textContent(),"更新已下载，安装后即可使用");
  await primary.click();
  assert.deepEqual(await unit.evaluate(()=>calls),["login","install"]);
  await unit.evaluate(()=>{
    modal.showQuota(false);
    modal.syncUpdate({status:"ready",version:"1.0.2.3"});
    modal.close();
  });
  await unit.waitForTimeout(20);
  assert.equal(await primary.textContent(),"立即安装","latest deferred update replays without another snapshot");
  await unit.evaluate(()=>modal.showQuota(false));
  assert.equal(await primary.evaluate(e=>document.activeElement===e),true,"kind replacement focuses a visible action");
  await unit.evaluate(()=>{
    modal.close();
    modal.showUpdate({status:"available",version:"1.0.2.4"});
    document.querySelector("[data-lume-modal-primary]").focus();
    modal.syncUpdate({status:"ready",version:"1.0.2.4"});
  });
  assert.equal(await unit.locator("[data-lume-modal-secondary]").evaluate(e=>document.activeElement===e),true,
    "new install semantics do not inherit download focus");
  await unit.evaluate(()=>{modal.showQuota(false);modal.close();});
  await unit.waitForTimeout(20);
  assert.equal(await dialog.isVisible(),true,"quota interruption restores the already visible update");
  assert.equal(await primary.textContent(),"立即安装");
  await unit.evaluate(()=>modal.close());
  await unit.evaluate(()=>modal.syncUpdate({status:"ready",version:"1.0.2.4"}));
  assert.equal(await dialog.isVisible(),false,"explicit update dismissal stays consumed");
  await unit.evaluate(()=>{
    modal.showQuota(false);
    modal.syncUpdate({status:"ready",version:"1.0.2.4"});
    modal.close();
  });
  await unit.waitForTimeout(20);
  assert.equal(await dialog.isVisible(),false,"identical snapshots behind quota respect explicit dismissal");
  await unit.evaluate(()=>{
    modal.syncUpdate({status:"ready",version:"1.0.2.5"});
    modal.syncUpdate({status:"ready",version:"1.0.2.5"},true);
    modal.showQuota(false);
    modal.close();
  });
  await unit.waitForTimeout(20);
  assert.equal(await dialog.isVisible(),false,"interrupted busy updates wait for availability");
  await unit.evaluate(()=>modal.syncUpdate({status:"ready",version:"1.0.2.5"},false));
  assert.equal(await dialog.isVisible(),true,"interrupted busy update resumes on nonbusy snapshot");
  await unit.evaluate(()=>{
    modal.close();
    modal.showQuota(false);
    modal.showUpdateResult({kind:"error",updateVersion:"",manual:true});
    modal.close();
    modal.showUpdateResult({kind:"current",updateVersion:"1.0.2.1",manual:true});
  });
  await primary.click();
  await unit.waitForTimeout(20);
  assert.equal(await dialog.isVisible(),false,"newer result supersedes the queued old error");
  await unit.evaluate(()=>{
    modal.showQuota(false);
    window.oldButton=document.querySelector("[data-lume-modal-primary]");
    modal.dispose();oldButton.click();modal.showQuota(false);
  });
  assert.equal(await dialog.count(),0);
  assert.deepEqual(await unit.evaluate(()=>calls),["login","install"]);
  await unit.close();

  for(const viewport of [{width:1280,height:800},{width:390,height:760}]) {
    const page = await browser.newPage({viewport});
    page.setDefaultTimeout(8000);
    page.on("pageerror",e=>errors.push(e.message));
    page.on("request",r=>{if(!r.url().startsWith(url)&&!r.url().startsWith("data:"))remote.push(r.url());});
    await page.addInitScript(installFixture);
    await page.goto(url);
    await page.waitForFunction(()=>harness.commands.includes("v1|ready"));
    const modal = page.locator(".lume-modal");
    const action = page.locator("[data-lume-modal-primary]");
    const snapshot = async name => {
      await page.waitForTimeout(240);
      const layout = await modal.evaluate(e=>{
        const r=e.getBoundingClientRect(),s=getComputedStyle(e);
        return {left:r.left,right:r.right,top:r.top,bottom:r.bottom,overflow:e.scrollWidth>e.clientWidth,
          blur:s.backdropFilter,background:s.backgroundColor,letterSpacing:s.letterSpacing};
      });
      assert.ok(layout.left>=0 && layout.right<=viewport.width && layout.top>=0 && layout.bottom<=viewport.height);
      assert.equal(layout.overflow,false);
      assert.match(layout.blur,/blur\(22px\)/);
      assert.equal(layout.background,"rgba(26, 36, 52, 0.46)");
      const pixels = PNG.sync.read(await modal.screenshot());
      const colors = new Set();
      for(let i=0;i<pixels.data.length;i+=64)colors.add(pixels.data.subarray(i,i+3).toString("hex"));
      assert.ok(colors.size>30,"dialog is visibly rendered");
      await page.screenshot({path:path.join(output,`${viewport.width}-${name}.png`)});
    };
    await page.evaluate(()=>harness.quota());
    assert.equal(await modal.isVisible(),true,"real quota notice opens a Lume dialog");
    assert.equal(await page.locator("[data-lume-modal-title]").textContent(),"免费下载额度已用完");
    await snapshot("guest-quota");
    await page.keyboard.press("Escape");
    await page.evaluate(()=>harness.hardQuota());
    assert.equal(await modal.isVisible(),true,"download admission failure opens quota dialog through the real wallpaper-state bridge");
    await page.evaluate(()=>harness.account(true));
    assert.equal(await action.textContent(),"知道了","open quota follows accepted login");
    await page.evaluate(()=>harness.account(false));
    assert.equal(await action.textContent(),"登录","open quota follows accepted logout");
    await action.click();
    assert.equal(await page.locator(".application-settings").getAttribute("data-settings-section"),"user");
    assert.equal(await modal.isVisible(),false);
    await page.evaluate(()=>harness.account(true));
    await page.evaluate(()=>harness.quota());
    assert.equal(await action.textContent(),"知道了");
    assert.equal(await page.locator("[data-lume-modal-body]").isVisible(),false);
    await snapshot("signed-in-quota");
    await page.keyboard.press("Escape");
    await page.evaluate(()=>harness.receive({version:1,type:"wallpaper-engagement-notice",id:"fixture",error:"WALLPAPER_DOWNLOAD_STATS_FAILED"}));
    assert.equal(await modal.isVisible(),false);
    assert.match(await page.locator("#notice").textContent(),/下载数量记录失败/);
    await page.evaluate(()=>harness.settings({update:{status:"available",version:"1.0.4",progress:0}}));
    assert.equal(await modal.isVisible(),false,"background availability stays in settings");
    for (const kind of ['available','disabled','incompatible','current','error']) {
      await page.evaluate(kind=>harness.receive({version:1,type:"app-update-notice",kind,updateVersion:"1.0.4",error:"",manual:false}),kind);
      assert.equal(await modal.isVisible(),false,"automatic notices cannot show a dialog");
    }
    for (const status of ['downloading','ready','installing']) {
      await page.evaluate(status=>harness.settings({update:{status,version:"1.0.4",progress:50}}),status);
      assert.equal(await modal.isVisible(),false,"restored transfer snapshots cannot open a dialog");
    }
    await page.evaluate(()=>{
      harness.settings({update:{status:'available',version:'1.0.4',progress:0}});
      document.hasFocus=()=>false;
      harness.receive({version:1,type:"app-update-notice",kind:"available",updateVersion:"1.0.4",error:"",manual:true});
      delete document.hasFocus;
    });
    assert.equal(await modal.isVisible(),false,"late manual results do not interrupt another application");
    await page.waitForTimeout(80);
    assert.equal(await modal.isVisible(),false,"returning focus does not replay the suppressed result");
    await page.evaluate(()=>harness.receive({version:1,type:"app-update-notice",kind:"available",updateVersion:"1.0.4",error:"",manual:true}));
    assert.equal(await action.textContent(),"下载并安装");
    await snapshot("update");
    await page.locator("[data-lume-modal-secondary]").click();
    await page.evaluate(()=>harness.settings({}));
    assert.equal(await modal.isVisible(),false,"identical snapshots do not repeat dismissed updates");
    await page.evaluate(()=>harness.receive({version:1,type:"app-update-notice",kind:"available",updateVersion:"1.0.4",error:"",manual:true}));
    assert.equal(await modal.isVisible(),true,"a fresh explicit check can show the same version");
    await page.evaluate(()=>harness.settings({busy:true}));
    assert.equal(await action.isDisabled(),true);
    await page.evaluate(()=>harness.settings({busy:false}));
    await page.evaluate(()=>{harness.rejectUpdates=true;});
    await action.click();
    assert.equal(await modal.isVisible(),true,"rejected dispatch retains retryable update prompt");
    await page.evaluate(()=>{harness.rejectUpdates=false;});
    await action.click();
    await page.evaluate(()=>document.querySelector("[data-lume-modal-primary]").click());
    assert.equal(await page.evaluate(()=>harness.commands.filter(c=>c?.action==="downloadUpdate").length),1);
    await page.evaluate(()=>harness.settings({busy:true,update:{status:"downloading",version:"1.0.4",progress:50}}));
    assert.equal(await modal.isVisible(),true);
    assert.match(await page.locator("[data-lume-modal-body]").textContent(),/50%/);
    await snapshot("update-progress");
    await page.evaluate(()=>harness.settings({busy:false,update:{status:"installing",version:"1.0.4",progress:100}}));
    assert.match(await page.locator("[data-lume-modal-body]").textContent(),/Windows/);
    await page.evaluate(()=>harness.settings({busy:false,error:"update-install-cancelled",update:{status:"ready",version:"1.0.4",progress:100}}));
    assert.equal(await action.textContent(),"立即安装");
    await snapshot("update-ready");
    await action.click();
    assert.equal(await page.evaluate(()=>harness.commands.filter(c=>c?.action==="installUpdate").length),1);
    await page.evaluate(()=>harness.settings({busy:false,error:"update-checksum",update:{status:"error",version:"1.0.4",progress:0}}));
    assert.equal(await modal.isVisible(),true,"download failure is visible");
    await page.keyboard.press("Escape");
    for(const status of ["incompatible","current","checking","idle"]) {
      await page.evaluate(status=>harness.settings({update:{status,version:"1.0.4",progress:0}}),status);
      assert.equal(await modal.isVisible(),false);
    }
    await page.evaluate(()=>{
      harness.quota();
      harness.settings({error:"",update:{status:"available",version:"1.0.8",progress:0}});
      harness.receive({version:1,type:"app-update-notice",kind:"available",updateVersion:"1.0.8",error:"",manual:true});
    });
    assert.equal(await page.locator("[data-lume-modal-title]").textContent(),"免费下载额度已用完","background updates do not replace a quota prompt");
    await page.keyboard.press("Escape");
    await page.waitForFunction(()=>document.querySelector("[data-lume-modal-title]").textContent==="发现新版本");
    await page.evaluate(()=>harness.settings({language:"en"}));
    assert.equal(await page.locator("[data-lume-modal-title]").textContent(),"Update available");
    await page.keyboard.press("Escape");
    await page.evaluate(()=>harness.quota());
    assert.equal(await page.locator("[data-lume-modal-title]").textContent(),"Today's download quota is used up");
    await page.keyboard.press("Escape");
    await page.evaluate(()=>harness.receive({version:1,type:"test-update-preview",versionText:"preview"}));
    assert.equal(await modal.isVisible(),false,"no production preview protocol");
    await page.emulateMedia({reducedMotion:"reduce"});
    await page.evaluate(()=>harness.quota());
    assert.equal(await modal.evaluate(e=>getComputedStyle(e).animationName),"none");
    await page.evaluate(()=>harness.receive({version:1,type:"app-update-notice",kind:"disabled",
      updateVersion:"1.0.3",error:"update-disabled",manual:true}));
    assert.equal(await page.locator("[data-lume-modal-title]").textContent(),"Today's download quota is used up");
    await page.keyboard.press("Escape");
    await page.waitForFunction(()=>document.querySelector("[data-lume-modal-title]").textContent==="Update available");
    assert.equal(await action.textContent(),"OK","unsupported installs never offer download/install");
    assert.match(await page.locator("[data-lume-modal-body]").textContent(),/not supported/);
    await page.keyboard.press("Escape");
    await page.evaluate(()=>{
      harness.settings({language:"zh-CN",update:{status:"incompatible",version:"1.0.3",progress:0}});
      harness.receive({version:1,type:"app-update-notice",kind:"incompatible",updateVersion:"1.0.3",
        error:"update-incompatible",manual:true});
    });
    assert.equal(await page.locator("[data-lume-modal-body]").textContent(),"此版本暂不支持直接安装更新");
    await snapshot("actual-update-notice");
    await action.click();
    await page.evaluate(()=>harness.receive({version:1,type:"app-update-notice",kind:"current",
      updateVersion:"1.0.2.1",error:"",manual:true}));
    assert.equal(await page.locator("[data-lume-modal-title]").textContent(),"已是最新版本");
    await action.click();
    await page.evaluate(()=>harness.receive({version:1,type:"app-update-notice",kind:"error",
      updateVersion:"",error:"update-offline",manual:true}));
    assert.equal(await page.locator("[data-lume-modal-title]").textContent(),"更新失败");
    await action.click();
    await page.close();
  }
  assert.deepEqual(errors,[]);
  assert.deepEqual(remote,[],"fixtures do not access live services");
  console.log("Edge passed: modal lifecycle, official host messages, update states, focus/ESC, 1280px/390px glass screenshots.");
} finally {
  await browser?.close();
  await new Promise(resolve=>server.close(resolve));
}
