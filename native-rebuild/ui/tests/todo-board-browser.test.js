import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createBrowserHarness} from './component-settings-browser.mjs';

// In-memory stand-in for the native shared to-do store and its page protocol.
function fixture({language,page}){
  const listeners=new Set(),commands=[];
  const pad=v=>String(v).padStart(2,'0');
  const key=d=>`${d.getFullYear()}-${pad(d.getMonth()+1)}-${pad(d.getDate())}`;
  const now=new Date(2026,10,5,10,0),day=n=>key(new Date(2026,10,5+n));
  let next=10;
  let tasks=[
    {id:'a',title:language==='en'?'Product review':'产品评审',notes:'',completed:false,repeat:false,color:'orange',date:day(0),time:'14:00',end:'15:00'},
    {id:'b',title:language==='en'?'Morning stand-up':'晨会',notes:'',completed:true,repeat:true,color:'blue',date:day(0),time:'10:00',end:'10:30'},
    {id:'c',title:language==='en'?'Send the invoice':'发送报价单',notes:'客户 A',completed:false,repeat:false,color:'red',date:day(-2),time:'11:00',end:'12:00'},
    {id:'d',title:language==='en'?'Design walkthrough':'设计走查',notes:'',completed:false,repeat:false,color:'none',date:day(1),time:'09:30',end:'10:30'},
    {id:'e',title:language==='en'?'Release day':'发版日',notes:'',completed:false,repeat:false,color:'green',date:day(3),time:'',end:''},
    {id:'f',title:language==='en'?'Gym':'健身',notes:'',completed:false,repeat:true,color:'green',date:day(0),time:'19:30',end:'20:30'},
  ];
  const base={notes:'',completed:false,repeat:false,color:'none',time:'',end:'',allDay:false,days:1,endDate:'',frequency:'none',reminder:-1,subtasks:[]};
  tasks=tasks.map(task=>({...base,...task,endDate:task.date,allDay:!task.time,frequency:task.repeat?'daily':'none'}));
  tasks[0].subtasks=[{id:'s1',title:language==='en'?'Draft agenda':'准备议程',completed:true},{id:'s2',title:language==='en'?'Book room':'预订会议室',completed:false}];
  const emit=value=>listeners.forEach(listener=>listener({data:value}));
  const state=(result='snapshot',error='')=>({version:1,type:'todo-board-state',result,error,available:true,writable:true,today:key(now),tasks:structuredClone(tasks)});
  window.todoFixture={commands,tasks:()=>tasks};
  window.chrome={webview:{
    addEventListener(name,listener){if(name==='message')listeners.add(listener);},
    removeEventListener(name,listener){if(name==='message')listeners.delete(listener);},
    postMessage(command){commands.push(command);
      if(command==='v1|ready')queueMicrotask(()=>{
        emit({version:1,type:'app-settings-state',revision:1,language,balancedMode:true,autostart:false,
          busy:false,error:'',update:{status:'idle',version:'',progress:0}});
        emit({version:1,type:'navigate',page});
      });
      if(command?.scope!=='todo-board')return;
      const {action,id,...fields}=command;
      queueMicrotask(()=>{
        if(action==='snapshot')return emit(state());
        const task=tasks.find(item=>item.id===id);
        const clean=Object.fromEntries(Object.entries(fields).filter(([name])=>!['version','scope'].includes(name)));
        if(action==='add')tasks=[{...base,id:String(next++),...clean,repeat:!!clean.frequency&&clean.frequency!=='none',endDate:clean.date},...tasks];
        else if(!task)return emit(state('error','not-found'));
        else if(action==='complete')task.completed=true;
        else if(action==='restore')task.completed=false;
        else if(action==='delete')tasks=tasks.filter(item=>item!==task);
        else if(action==='update')Object.assign(task,Object.fromEntries(Object.entries(fields).filter(([name])=>!['version','scope'].includes(name))));
        emit(state('applied'));
      });
    },
  }};
}

test('shared to-do board in the island and to-do list pages',{
  skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:120000,
},async t=>{
  const output=fileURLToPath(new URL('../../../build-artifacts/todo-board-browser',import.meta.url));
  const harness=await createBrowserHarness(output);
  try{
    for(const [page,width,language] of [['dynamicIsland',1536,'zh-CN'],['todayTodo',1536,'zh-CN'],['todayTodo',1100,'en']])
    await t.test(`${page} ${width}px ${language}`,async()=>{
      const tab=await harness.browser.newPage({viewport:{width,height:864},reducedMotion:'no-preference'});
      tab.setDefaultTimeout(8000);
      const errors=[];tab.on('pageerror',error=>errors.push(error.message));
      try{
        await tab.addInitScript(fixture,{language,page});await tab.goto(harness.origin);
        if(page==='dynamicIsland')await tab.locator('[data-tab="todo"]').click();
        const board=tab.locator('[data-todo-board]');await board.waitFor({state:'visible'});
        await tab.waitForFunction(()=>document.querySelectorAll('[data-tb-card]').length===6).catch(error=>{throw new Error(error.message+' page errors: '+errors.join(' | '));});
        assert.equal(await board.locator('[data-tb-column="today"] [data-tb-card]').count(),3,'today holds today and overdue tasks');
        assert.equal(await board.locator('[data-tb-column="later"] [data-tb-card]').count(),2);
        assert.equal(await board.locator('[data-tb-column="done"] [data-tb-card]').count(),1);
        assert.equal(await board.locator('.tb-chip.is-overdue').count(),1,'overdue task is flagged');
        assert.equal(await board.locator('.tb-sub__list li').count(),0,'collapsed sub-steps do not allocate hidden rows');
        await tab.waitForTimeout(400);
        await tab.screenshot({path:path.join(output,`${page}-${width}-${language}.png`)});
        // Quick add into Today.
        await board.locator('[data-tb-column="today"] .tb-ghost').click();
        await tab.keyboard.type(language==='en'?'Call the bank':'给银行打电话');await tab.keyboard.press('Enter');
        await tab.waitForFunction(()=>document.querySelectorAll('[data-tb-card]').length===7);
        // Complete from the card checkbox.
        await board.locator('[data-tb-toggle="a"]').first().click();
        await tab.waitForFunction(()=>window.todoFixture.tasks().find(task=>task.id==='a').completed);
        assert.equal(await board.locator('[data-tb-column="done"] [data-tb-card="a"]').count(),1);
        // Edit through the sheet: rename and recolour.
        await board.locator('[data-tb-edit="d"]').first().click();
        const sheet=board.locator('[data-tb-form]');await sheet.waitFor({state:'visible'});
        await sheet.locator('[data-tb-field="title"]').fill(language==='en'?'Design review':'设计评审');
        await sheet.locator('[data-tb-color="blue"]').click();
        await tab.waitForTimeout(350);
        await tab.screenshot({path:path.join(output,`${page}-${width}-${language}-sheet.png`)});
        await sheet.locator('button[type="submit"]').click();
        await tab.waitForFunction(()=>window.todoFixture.tasks().find(task=>task.id==='d').color==='blue');
        const update=(await tab.evaluate(()=>window.todoFixture.commands)).find(command=>command?.action==='update');
        assert.deepEqual(Object.keys(update).sort(),['action','color','id','scope','title','version'],'only changed fields are sent');
        // Drag onto a calendar day reschedules.
        await board.locator('[data-tb-card="e"]').dragTo(board.locator('[data-tb-day="2026-11-12"]'));
        await tab.waitForFunction(()=>window.todoFixture.tasks().find(task=>task.id==='e').date==='2026-11-12');
        // Sub-steps expand on the card and toggle through the shared store.
        const chip=board.locator('[data-tb-card="a"] [data-tb-steps-toggle]');
        assert.match(await chip.textContent(),/1\/2/);
        await chip.click();
        await board.locator('[data-tb-card="a"] [data-tb-step-check="a"][data-step="s2"]').click();
        await tab.waitForFunction(()=>window.todoFixture.tasks().find(task=>task.id==='a').subtasks.every(step=>step.completed));
        await tab.waitForTimeout(400);await board.locator('[data-tb-card="a"]').screenshot({path:path.join(output,`${page}-${width}-${language}-steps.png`)});
        // The full "add task" sheet: timed slot, quick date, reminder, repeat, sub-steps and notes.
        await board.locator('[data-tb-new]').click();
        const form=board.locator('[data-tb-form]');await form.waitFor({state:'visible'});
        assert.equal(await form.locator('[data-tb-save]').isDisabled(),true,'save waits for a title');
        await form.locator('[data-tb-field="title"]').fill(language==='en'?'Team dinner':'团队聚餐');
        await form.locator('[data-tb-quick-date="1"]').click();
        await form.locator('[data-tb-allday="false"]').click();
        const choose=async(field,value)=>{
          await form.locator(`.settings-choice:has([data-tb-field="${field}"]) [data-settings-choice-trigger]`).click();
          if(value.includes(':')){
            const [hour,minute]=value.split(':');
            await tab.locator(`[data-settings-choice-hour="${hour}"]`).click();
            await tab.locator(`[data-settings-choice-minute="${minute}"]`).click();
            await tab.locator('[data-settings-choice-apply]').click();
          }else await tab.locator(`[data-settings-choice-option="${value}"]`).click();
        };
        await choose('time','18:30');await choose('end','20:00');
        await choose('reminder','30');await choose('frequency','weekly');
        await form.locator('[data-tb-more]').click();
        const stepInput=form.locator('[data-tb-step-input]');
        await stepInput.fill(language==='en'?'Book a table':'订位');await stepInput.press('Enter');
        await stepInput.fill(language==='en'?'Tell everyone':'通知大家');await stepInput.press('Enter');
        assert.equal(await form.locator('[data-tb-steps] li').count(),2);
        await form.locator('[data-tb-field="notes"]').fill(language==='en'?'Somewhere quiet':'找个安静的地方');
        assert.match(await form.locator('[data-tb-span]').textContent(),/18:30 – 20:00/);
        await tab.waitForTimeout(400);
        await tab.screenshot({path:path.join(output,`${page}-${width}-${language}-add-sheet.png`)});
        await form.locator('[data-tb-field="title"]').press('Control+Enter');
        await tab.waitForFunction(()=>window.todoFixture.commands.some(command=>command?.action==='add'&&command.subtasks));
        const added=(await tab.evaluate(()=>window.todoFixture.commands)).find(command=>command?.action==='add'&&command.subtasks);
        assert.equal(added.allDay,false);assert.equal(added.time,'18:30');assert.equal(added.end,'20:00');assert.equal(added.days,1);
        assert.equal(added.date,'2026-11-06');assert.equal(added.reminder,30);assert.equal(added.frequency,'weekly');
        assert.equal(added.subtasks.length,2);assert.equal(added.subtasks[0].completed,false);
        // An all-day span over several days.
        await board.locator('[data-tb-new]').click();await form.waitFor({state:'visible'});
        await form.locator('[data-tb-field="title"]').fill(language==='en'?'Trip':'出差');
        await form.locator('[data-tb-days="1"]').click();await form.locator('[data-tb-days="1"]').click();
        assert.match(await form.locator('[data-tb-span]').textContent(),language==='en'?/3 days/:/共 3 天/);
        await form.locator('[data-tb-save]').click();
        await tab.waitForFunction(()=>window.todoFixture.commands.some(command=>command?.action==='add'&&command.days===3&&command.allDay===true));
        // Selecting a day shows its agenda.
        await board.locator('[data-tb-day="2026-11-06"]').click();
        assert.match(await board.locator('.tb-agenda').textContent(),language==='en'?/Design review/:/设计评审/);
        // Filters narrow the columns.
        await board.locator('[data-tb-filter="overdue"]').click();
        assert.equal(await board.locator('[data-tb-card]').count(),1);
        await board.locator('[data-tb-filter="all"]').click();
        await tab.locator('[data-tb-search]').fill(language==='en'?'bank':'银行');
        await tab.waitForFunction(()=>document.querySelectorAll('[data-tb-card]').length===1);
        assert.equal(await board.locator('[data-tb-card]').count(),1);
        assert.equal(await tab.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false,'no horizontal page overflow');
        assert.deepEqual(errors,[]);
      }finally{await tab.close();}
    });
  }finally{await harness.close();}
});
