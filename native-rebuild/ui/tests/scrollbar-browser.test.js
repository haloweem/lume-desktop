import test from "node:test";
import assert from "node:assert/strict";
import {readFile, mkdir} from "node:fs/promises";
import {homedir} from "node:os";
import path from "node:path";
import {pathToFileURL} from "node:url";

const enabled=process.env.LUME_UI_BROWSER_TESTS==="1";
test("scrollbars render original dimensions without system arrow buttons", {skip:!enabled}, async()=>{
  const playwrightDirectory=process.env.LUME_PLAYWRIGHT_DIR ||
    path.join(homedir(),".cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright");
  const {chromium}=await import(pathToFileURL(path.join(playwrightDirectory,"index.mjs")).href);
  const browser=await chromium.launch({headless:true,ignoreDefaultArgs:["--hide-scrollbars"],
    ...(process.env.LUME_BROWSER_EXECUTABLE?{executablePath:process.env.LUME_BROWSER_EXECUTABLE}:{})});
  try {
    const page=await browser.newPage({viewport:{width:760,height:540}});
    const css=await readFile(new URL("../app.css",import.meta.url),"utf8");
    const surfaces=["settings","CardGrid","wallpaper-grid","wallpaper-detail-target-menu",
      "wallpaper-info-sidebar","wallpaper-info-meta"];
    await page.setContent(`<style>${css}
      body {display:grid;grid-template-columns:repeat(3,220px);gap:16px;padding:16px;align-content:start}
      [data-scrollbar-surface] {position:relative!important;inset:auto!important;display:block!important;
        width:220px!important;height:220px!important;max-height:none!important;padding:0!important;
        margin:0!important;border:0!important;overflow:scroll!important;transform:none!important}
      [data-scrollbar-surface]>div {width:440px;height:2000px;background:#101218}
      </style>${surfaces.map(name=>`<section><p>${name}</p><div class="${name}" data-scrollbar-surface>
        <div></div></div></section>`).join("")}`);
    const rendered=await page.locator("[data-scrollbar-surface]").evaluateAll(elements=>elements.map(element=>{
      const thumb=getComputedStyle(element,"::-webkit-scrollbar-thumb");
      const buttons=getComputedStyle(element,"::-webkit-scrollbar-button");
      const track=getComputedStyle(element,"::-webkit-scrollbar-track");
      const corner=getComputedStyle(element,"::-webkit-scrollbar-corner");
      return {name:element.className,width:element.offsetWidth-element.clientWidth,
        height:element.offsetHeight-element.clientHeight,buttons:buttons.display,
        track:track.backgroundColor,thumb:thumb.backgroundColor,border:thumb.borderTopWidth,
        corner:corner.backgroundColor};
    }));
    if(process.env.LUME_UI_SCREENSHOT_DIR) {
      await mkdir(process.env.LUME_UI_SCREENSHOT_DIR,{recursive:true});
      await page.screenshot({path:path.join(process.env.LUME_UI_SCREENSHOT_DIR,"scrollbar-surfaces.png")});
    }
    for(const surface of rendered) {
      const wallpaper=surface.name==="wallpaper-grid";
      assert.equal(surface.width,wallpaper?4:10,`${surface.name} rendered vertical gutter`);
      assert.equal(surface.height,wallpaper?4:10,`${surface.name} rendered horizontal gutter`);
    }
    for(const surface of rendered) {
      const wallpaper=surface.name==="wallpaper-grid";
      assert.equal(surface.buttons,"none",`${surface.name} has no system arrows`);
      assert.equal(surface.track,wallpaper?"rgba(0, 0, 0, 0)":"rgba(8, 10, 14, 0.55)");
      assert.equal(surface.thumb,wallpaper?"rgba(255, 255, 255, 0.14)":"rgba(96, 108, 124, 0.72)");
      assert.equal(surface.border,wallpaper?"0px":"2px");
      assert.equal(surface.corner,"rgba(0, 0, 0, 0)",`${surface.name} has no white system corner`);
    }
    await page.locator(".settings").evaluate(element=>element.scrollTop=180);
    assert.equal(await page.locator(".settings").evaluate(element=>element.scrollTop),180);
    await page.locator(".wallpaper-grid").evaluate(element=>element.scrollTop=180);
    assert.equal(await page.locator(".wallpaper-grid").evaluate(element=>element.scrollTop),180);
  } finally {
    await browser.close();
  }
});
