// Development-only original UI contract capture; no Electron or real user data.
// node capture-calendar-todos-reference.mjs <widgets> <output> <playwright>
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {createRequire} from 'node:module';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
const [sourceArg,outputArg,playwrightArg]=process.argv.slice(2);
assert(sourceArg&&outputArg&&playwrightArg,'Pass original widgets, output and Playwright paths');
const source=await fs.realpath(sourceArg),output=path.resolve(outputArg);
await fs.mkdir(output,{recursive:true});
const files=['calendar-todos.js','calendar-todos.css','calendar-theme.css','calendar-picker.css','todo.css','todo-model.js','todo-picker.js','lunar-calendar.js','vendor/lunar.js','widget.css','widget-assets.js'];
const hashes=Object.fromEntries(await Promise.all(files.map(async file=>[file,createHash('sha256').update(await fs.readFile(path.join(source,file))).digest('hex')])));
const {chromium}=createRequire(import.meta.url)(path.resolve(playwrightArg));
const browser=await chromium.launch({headless:true,channel:'msedge'});
const results=[];
try {
  const context=await browser.newContext({bypassCSP:true,timezoneId:'Asia/Shanghai',offline:true});
  await context.addInitScript(()=>{
    const listeners=new Set();
    const start=Date.parse('2026-09-25T09:00:00+08:00');
    const tasks=Array.from({length:5},(_,i)=>({id:`reference-${i}`,title:['项目例会','设计评审','整理日程','完成记录','明日计划'][i],notes:i===1?'保留原版布局与交互':'',status:i===3?'completed':'open',dueAt:start+(i+1)*3600000,reminderAt:start+i*3600000-600000,tags:[`lume:calendar-color:${['red','orange','yellow','green','blue'][i]}`,`lume:calendar-schedule:${JSON.stringify([start+i*3600000,start+(i+1)*3600000])}`],sortKey:i,createdAt:start,updatedAt:start}));
    const snapshot={accountKey:'local',syncStatus:'local',tasks};
    window.desktopWidget={
      onUpdate(fn){listeners.add(fn);return()=>listeners.delete(fn);},
      calendarTodoRequest:async type=>{if(type!=='read')throw new Error('Reference capture never persists');return structuredClone(snapshot);},
      setGlassPanels(){},setGlassDiscs(){},
    };
  });
  const page=await context.newPage();page.setDefaultTimeout(5000);
  const errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.clock.setFixedTime(new Date('2026-09-25T04:00:00Z'));
  const selectors={
    agenda:['#calendar','.calendar-header','.weekdays','#days','#calendar-agenda','.calendar-agenda-heading','#calendar-agenda h2','.calendar-todo-list','.calendar-todo-row','.calendar-todo-content','.calendar-todo-actions'],
    editor:['#calendar-todo-panel','.calendar-todo-heading','.calendar-todo-form','[name="title"]','.calendar-schedule-field','#todo-start-trigger','#todo-end-trigger','#todo-lead-trigger','.calendar-todo-colors','[name="notes"]','.calendar-todo-form-actions','.calendar-todo-feedback'],
    picker:['#todo-popover','.todo-picker-header','.todo-picker-weekdays','.todo-picker-days','.todo-picker-time','.todo-picker-footer'],
    lead:['#todo-popover','.todo-picker-number-row','.todo-picker-number-none','.todo-picker-footer'],
  };
  async function measure(mode){return page.evaluate(list=>{
    const rect=node=>{const r=node.getBoundingClientRect(),s=getComputedStyle(node);return {x:r.x,y:r.y,width:r.width,height:r.height,fontSize:s.fontSize,lineHeight:s.lineHeight,padding:s.padding,gap:s.gap,visible:s.display!=='none'&&s.visibility!=='hidden'};};
    return {selectors:Object.fromEntries(list.map(selector=>[selector,[...document.querySelectorAll(selector)].map(rect)])),cells:[...document.querySelectorAll('#days .day')].filter(n=>n.classList.contains('has-todos')).map(n=>({date:n.dataset.date,cell:rect(n),number:rect(n.querySelector('.day-number')),previews:[...n.querySelectorAll('.calendar-todo-preview')].map(rect),count:n.querySelector('.calendar-todo-count')?rect(n.querySelector('.calendar-todo-count')):null}))};
  },selectors[mode]);}
  for(const theme of ['spacious','split'])for(const [width,height]of [[820,500],[600,420],[480,390],[440,400],[380,390],[320,320],[1440,720]]){
    await page.setViewportSize({width,height});
    const url=pathToFileURL(path.join(source,'index.html'));url.search=new URLSearchParams({kind:'calendar',theme}).toString();
    await page.goto(url.href);await page.locator('[data-storage-status]').filter({hasText:'本机保存'}).waitFor({state:'attached'});
    await page.addStyleTag({content:'body{background:#202020}'});await page.evaluate(()=>document.fonts.ready);
    await page.locator('#calendar-agenda-toggle').click();
    const stem=`${theme}-${width}x${height}`,entry={theme,width,height};
    entry.agenda=await measure('agenda');await page.screenshot({path:path.join(output,`${stem}-agenda.png`)});
    await page.locator('[data-task-edit="reference-1"]').click();
    assert.equal(await page.locator('[name="title"]').inputValue(),'设计评审');
    entry.editor=await measure('editor');await page.screenshot({path:path.join(output,`${stem}-editor.png`)});
    await page.locator('#todo-start-trigger').click();
    entry.picker=await measure('picker');await page.screenshot({path:path.join(output,`${stem}-picker.png`)});
    await page.keyboard.press('Escape');assert.equal(await page.locator('#todo-start-trigger').evaluate(n=>n===document.activeElement),true);
    await page.locator('#todo-lead-trigger').click();
    entry.lead=await measure('lead');await page.screenshot({path:path.join(output,`${stem}-lead.png`)});
    await page.keyboard.press('Escape');await page.keyboard.press('Escape');
    assert.equal(await page.locator('#calendar-todo-panel').isVisible(),false);
    results.push(entry);
  }
  assert.deepEqual(errors,[]);
  await fs.writeFile(path.join(output,'reference-todos-layout.json'),JSON.stringify({fixedDate:'2026-09-25',timezone:'Asia/Shanghai',source,sourceHashes:hashes,scope:'Original agenda/editor/date-time/reminder field geometry with fixture data; neutral background, no native glass or persistence',results},null,2));
  console.log(`CALENDAR_TODOS_REFERENCE_PASS: ${results.length} layouts, ${results.length*4} screenshots`);
}finally{await browser.close();}
