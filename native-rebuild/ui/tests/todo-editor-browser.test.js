import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createBrowserHarness} from './component-settings-browser.mjs';

// Stand-in for the native TodoSheetWindow host (platform/windows/calendar/todo_sheet_window.cpp).
function host({language,task}){
  const listeners=new Set(),sent=[];
  window.editorHost={sent};
  window.chrome={webview:{
    addEventListener(name,listener){if(name==='message')listeners.add(listener);},
    removeEventListener(name,listener){listeners.delete(listener);},
    postMessage(message){sent.push(message);
      const emit=data=>queueMicrotask(()=>listeners.forEach(listener=>listener({data})));
      if(message.action==='ready')emit({version:1,type:'todo-editor-init',english:language==='en',today:'2026-11-05',date:'2026-11-06',task});
      if(message.action==='save'&&window.editorHost.failNext){window.editorHost.failNext=false;emit({version:1,type:'todo-editor-result',ok:false,error:'invalid-time'});}
    }}};
}
const existing={id:'a',title:'产品评审',notes:'带上数据',completed:false,repeat:false,color:'orange',date:'2026-11-05',time:'14:00',end:'15:00',
  allDay:false,days:1,endDate:'2026-11-05',frequency:'none',reminder:15,subtasks:[{id:'s1',title:'准备议程',completed:true}]};

test('task editor window hosts the shared sheet',{skip:process.env.LUME_UI_BROWSER_TESTS!=='1'},async()=>{
  const output=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../build-artifacts/todo-editor-browser');
  const harness=await createBrowserHarness(output);
  try{
    for(const language of ['zh','en']){
      const tab=await harness.browser.newPage({viewport:{width:560,height:820},reducedMotion:'no-preference'});
      const errors=[];tab.on('pageerror',error=>errors.push(error.message));
      // New task on the day the caller chose.
      await tab.addInitScript(host,{language,task:null});await tab.goto(harness.origin+'/todo-editor.html');
      await tab.waitForSelector('[data-tb-form]');await tab.waitForTimeout(300);
      assert.equal(await tab.inputValue('[data-tb-field="date"]'),'2026-11-06');
      await tab.screenshot({path:path.join(output,`editor-new-${language}.png`)});
      await tab.fill('[data-tb-field="title"]',language==='en'?'Call the bank':'给银行打电话');
      await tab.click('[data-tb-allday="false"]');await tab.fill('[data-tb-field="time"]','16:00');await tab.fill('[data-tb-field="end"]','16:30');
      await tab.click('[data-tb-more]');await tab.fill('[data-tb-step-input]',language==='en'?'Find the card':'找到卡号');await tab.press('[data-tb-step-input]','Enter');
      await tab.evaluate(()=>{window.editorHost.failNext=true;});
      await tab.keyboard.press('Control+Enter');await tab.waitForTimeout(100);
      assert.equal(await tab.isVisible('[data-tb-sheet-error]'),true,'host rejection shows inline error');
      await tab.screenshot({path:path.join(output,`editor-new-error-${language}.png`)});
      await tab.keyboard.press('Control+Enter');
      const saves=await tab.evaluate(()=>window.editorHost.sent.filter(m=>m.action==='save'));
      assert.equal(saves.length,2);
      const add=saves[1].commands[0];
      assert.equal(add.action,'add');assert.equal(add.fields.date,'2026-11-06');assert.equal(add.fields.time,'16:00');assert.equal(add.fields.allDay,false);
      assert.equal(add.fields.subtasks.length,1);
      await tab.keyboard.press('Escape');
      assert.ok((await tab.evaluate(()=>window.editorHost.sent.map(m=>m.action))).includes('close'));
      assert.deepEqual(errors,[]);await tab.close();

      // Editing an existing task sends only the changes.
      const edit=await harness.browser.newPage({viewport:{width:560,height:820}});
      await edit.addInitScript(host,{language,task:existing});await edit.goto(harness.origin+'/todo-editor.html');
      await edit.waitForSelector('[data-tb-form]');await edit.waitForTimeout(300);
      await edit.screenshot({path:path.join(output,`editor-edit-${language}.png`)});
      await edit.click('[data-tb-status="true"]');await edit.click('[data-tb-color="blue"]');await edit.click('[data-tb-save]');
      const save=(await edit.evaluate(()=>window.editorHost.sent.filter(m=>m.action==='save')))[0];
      assert.deepEqual(save.commands,[{action:'update',fields:{id:'a',color:'blue'}},{action:'complete',fields:{id:'a'}}]);
      await edit.click('[data-tb-sheet-delete]');
      assert.ok((await edit.evaluate(()=>window.editorHost.sent.map(m=>m.action))).includes('delete'));
      await edit.close();
    }
  }finally{await harness.close();}
});
