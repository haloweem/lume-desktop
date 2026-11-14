// The one "add / edit task" sheet. The to-do board shows it as an overlay; the
// calendar widget, desktop to-do list and island open it in a small native
// popup window (todo-editor.html). Both submit the same command fields.
import {escapeHTML as h} from './dom.js';
import {t} from './localization.js';
import {COLORS,COLOR_RGB,dateRe,timeRe,fromKey,addDays,dayLabel,icon} from './todo-common.js';

const reminderOptions=()=>[[-1,t('不提醒','None')],[0,t('开始时','At start')],[5,t('提前 5 分钟','5 minutes before')],
  [10,t('提前 10 分钟','10 minutes before')],[15,t('提前 15 分钟','15 minutes before')],[30,t('提前 30 分钟','30 minutes before')],
  [60,t('提前 1 小时','1 hour before')],[1440,t('提前 1 天','1 day before')]];
const repeatOptions=()=>[['none',t('不重复','Never')],['daily',t('每天','Every day')],['weekdays',t('工作日','Weekdays')],['weekly',t('每周','Every week')],['monthly',t('每月','Every month')]];
export const colorNames=()=>({none:t('无','None'),red:t('红','Red'),orange:t('橙','Orange'),yellow:t('黄','Yellow'),green:t('绿','Green'),blue:t('蓝','Blue')});
const stepId=()=>'s'+Date.now().toString(36)+Math.random().toString(36).slice(2,7);
const option=(value,label,current)=>`<option value="${h(String(value))}" ${String(value)===String(current)?'selected':''}>${h(label)}</option>`;

// A task row (todo-board-state shape) or a fresh draft on a date.
export function draftOf(task,date){
  if(!task)return {id:'',title:'',notes:'',date,allDay:true,days:1,time:'09:00',end:'10:00',color:'none',reminder:-1,
    frequency:'none',completed:false,subtasks:[],more:false};
  return {id:task.id,title:task.title,notes:task.notes,date:task.date||date,allDay:task.allDay,days:Math.max(1,task.days||1),
    time:task.time||'09:00',end:task.end||'10:00',color:task.color,reminder:task.reminder,frequency:task.frequency,
    completed:task.completed,subtasks:task.subtasks.map(step=>({...step})),more:!!(task.notes||task.subtasks.length)};
}

function markup(e,{standalone}){
  const reminders=[...(e.reminder===-2?[[-2,t('保留原提醒','Keep the existing reminder')]]:[]),...reminderOptions()];
  const heading=e.id?t('编辑任务','Edit task'):t('添加任务','Add task');
  const names=colorNames();
  return `${standalone?'':'<div class="tb-sheet__scrim" data-tb-close></div>'}
    <form class="tb-sheet__panel" data-tb-form role="dialog" aria-modal="true" aria-label="${heading}" novalidate>
      <header class="tb-sheet__head" ${standalone?'data-tb-drag':''}><div><h3>${heading}</h3><p>${e.id?t('保存后，日历、桌面清单和灵动岛会一起更新。','Changes appear in the calendar, desktop list and island together.'):t('把想做的事，安排进每一天。','Give the things you want to do a place in your days.')}</p></div>
        <button type="button" class="tb-icon" data-tb-close aria-label="${t('关闭','Close')}"><svg viewBox="0 0 16 16"><path d="m4 4 8 8M12 4l-8 8" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/></svg></button></header>
      <div class="tb-sheet__body">
        <input class="tb-sheet__title" data-tb-field="title" maxlength="500" placeholder="${t('输入任务名称','Task name')}" value="${h(e.title)}" autocomplete="off">
        <div class="tb-sheet__grid">
          ${e.id?`<div class="tb-field"><span class="tb-label">${t('状态','Status')}</span><div class="tb-seg" role="radiogroup" aria-label="${t('状态','Status')}">
            <button type="button" role="radio" data-tb-status="false" aria-checked="${!e.completed}">${t('待办','To do')}</button>
            <button type="button" role="radio" data-tb-status="true" aria-checked="${e.completed}">${t('已完成','Done')}</button></div></div>`:''}
          <div class="tb-field${e.id?'':' tb-field--span'}"><span class="tb-label">${t('颜色标签','Colour')}<em data-tb-color-name>${h(names[e.color])}</em></span>
            <div class="tb-sheet__colors" role="radiogroup" aria-label="${t('颜色','Colour')}">${COLORS.map(color=>
              `<button type="button" role="radio" data-tb-color="${color}" aria-checked="${e.color===color}" style="--c:${COLOR_RGB[color]||'transparent'}" aria-label="${h(names[color])}"></button>`).join('')}</div></div>
        </div>
        <div class="tb-field"><span class="tb-label">${t('排期','Schedule')}</span>
          <div class="tb-seg tb-seg--wide" role="radiogroup" aria-label="${t('排期','Schedule')}">
            <i class="tb-seg__thumb" aria-hidden="true"></i>
            <button type="button" role="radio" data-tb-allday="true" aria-checked="${e.allDay}">${icon.sun}${t('全天','All day')}</button>
            <button type="button" role="radio" data-tb-allday="false" aria-checked="${!e.allDay}">${icon.clock}${t('指定时间','At a time')}</button></div></div>
        <div class="tb-sheet__grid tb-sheet__grid--date">
          <label class="tb-field"><span class="tb-label">${t('日期','Date')}</span><input type="date" data-tb-field="date" value="${h(e.date)}"></label>
          <div class="tb-field"><span class="tb-label">${t('持续天数','Duration')}</span>
            <div class="tb-stepper"><button type="button" data-tb-days="-1" aria-label="${t('减少一天','One day less')}">−</button>
              <output data-tb-days-value>${e.days}</output><span>${t('天','days')}</span>
              <button type="button" data-tb-days="1" aria-label="${t('增加一天','One day more')}">+</button></div></div>
        </div>
        <div class="tb-quick-dates">${[['0',t('今天','Today')],['1',t('明天','Tomorrow')],['weekend',t('本周末','This weekend')],['monday',t('下周一','Next Monday')]].map(([value,label])=>
          `<button type="button" class="tb-pill" data-tb-quick-date="${value}">${label}</button>`).join('')}</div>
        <div class="tb-collapse${e.allDay?'':' is-open'}" data-tb-times><div class="tb-collapse__inner"><div class="tb-sheet__grid">
          <label class="tb-field"><span class="tb-label">${t('开始','Starts')}</span><input type="time" data-tb-field="time" value="${h(e.time)}"></label>
          <label class="tb-field"><span class="tb-label">${t('结束','Ends')}</span><input type="time" data-tb-field="end" value="${h(e.end)}"></label>
        </div></div></div>
        <p class="tb-hint" data-tb-span>${icon.clock}<span></span></p>
        <div class="tb-sheet__grid">
          <label class="tb-field"><span class="tb-label">${t('提醒','Reminder')}</span><span class="tb-select">${icon.bell}<select data-tb-field="reminder">${reminders.map(([value,label])=>option(value,label,e.reminder)).join('')}</select>${icon.chevron}</span></label>
          <label class="tb-field"><span class="tb-label">${t('重复','Repeat')}</span><span class="tb-select">${icon.repeat}<select data-tb-field="frequency">${repeatOptions().map(([value,label])=>option(value,label,e.frequency)).join('')}</select>${icon.chevron}</span></label>
        </div>
        <section class="tb-more">
          <button type="button" class="tb-more__toggle" data-tb-more aria-expanded="${e.more}">${icon.gear}<span>${t('更多选项','More options')}</span><em data-tb-more-summary></em>${icon.chevron}</button>
          <div class="tb-collapse${e.more?' is-open':''}" data-tb-more-body><div class="tb-collapse__inner"><div class="tb-more__content">
            <span class="tb-label">${t('子任务','Sub-steps')}</span>
            <ul class="tb-steps" data-tb-steps></ul>
            <div class="tb-step-add">${icon.plus}<input data-tb-step-input maxlength="500" placeholder="${t('拆成一个小步骤，回车添加','Break it into a small step, then press Enter')}" autocomplete="off"><kbd>↵</kbd></div>
            <label class="tb-field"><span class="tb-label">${t('备注','Notes')}</span><textarea data-tb-field="notes" maxlength="10000" rows="4" placeholder="${t('补充一些细节…','Add some detail…')}">${h(e.notes)}</textarea></label>
          </div></div></div>
        </section>
        <p class="tb-banner" data-tb-sheet-error role="alert" hidden></p>
      </div>
      <footer class="tb-sheet__foot">${e.id?`<button type="button" class="tb-link is-danger" data-tb-sheet-delete>${icon.trash}${t('删除','Delete')}</button>`:''}
        <span></span><button type="button" class="tb-soft" data-tb-close>${t('取消','Cancel')}</button>
        <button type="submit" class="tb-primary" data-tb-save>${e.id?t('保存','Save'):t('添加任务','Add task')}<kbd>Ctrl ↵</kbd></button></footer>
    </form>`;
}

// Opens the sheet inside `container`. onSave receives the commands to send,
// in order: [{action:'add'|'update'|'complete'|'restore', fields}]. Returns
// {close, showError, setBusy}.
export function openTodoSheet(container,{draft,today,original=null,standalone=false,onSave,onDelete,onClose,onDrag}){
  const doc=container.ownerDocument,view=doc.defaultView,e=draft;
  container.innerHTML=markup(e,{standalone});container.hidden=false;
  const form=container.querySelector('[data-tb-form]');
  const field=key=>form.querySelector(`[data-tb-field="${key}"]`);
  const reduced=()=>view.matchMedia('(prefers-reduced-motion: reduce)').matches;
  let closed=false;
  function renderSteps(){
    form.querySelector('[data-tb-steps]').innerHTML=e.subtasks.map(step=>`<li class="${step.completed?'is-done':''}">
      <button type="button" class="tb-step-check" data-tb-step-edit-check="${h(step.id)}" role="checkbox" aria-checked="${step.completed}" aria-label="${h(step.title)}">${icon.check}</button>
      <input data-tb-step-title="${h(step.id)}" value="${h(step.title)}" maxlength="500" aria-label="${t('子任务','Sub-step')}">
      <button type="button" class="tb-icon" data-tb-step-remove="${h(step.id)}" aria-label="${t('删除子任务','Remove sub-step')}"><svg viewBox="0 0 16 16"><path d="m5 5 6 6M11 5l-6 6" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/></svg></button></li>`).join('');
    sync();
  }
  function sync(){
    const date=field('date').value,start=field('time').value,end=field('end').value;
    form.querySelectorAll('[data-tb-allday]').forEach(node=>node.setAttribute('aria-checked',String(node.dataset.tbAllday===String(e.allDay))));
    form.querySelector('.tb-seg--wide').classList.toggle('is-second',!e.allDay);
    form.querySelector('[data-tb-times]').classList.toggle('is-open',!e.allDay);
    form.querySelector('[data-tb-days-value]').textContent=String(e.days);
    form.querySelector('[data-tb-days="-1"]').disabled=e.days<=1;
    form.querySelector('[data-tb-days="1"]').disabled=e.days>=366;
    let hint;
    if(!dateRe.test(date))hint=t('请选择日期','Choose a date');
    else{
      const label=key=>dayLabel(key,today),last=addDays(date,e.days-1);
      if(e.allDay)hint=e.days>1?t(`全天 · ${label(date)} 至 ${label(last)}，共 ${e.days} 天`,`All day · ${label(date)} to ${label(last)}, ${e.days} days`):
        t(`全天 · 结束于 ${label(date)}`,`All day · ends ${label(date)}`);
      else if(!timeRe.test(start))hint=t('请选择开始时间','Choose a start time');
      else{
        const finish=timeRe.test(end)?end:'',overnight=e.days===1&&finish&&finish<=start;
        const endDay=overnight?addDays(date,1):last;
        hint=`${label(date)} ${start}`+(finish?` – ${endDay!==date?label(endDay)+' ':''}${finish}`:'');
      }
    }
    form.querySelector('[data-tb-span] span').textContent=hint;
    const notes=field('notes').value.trim();
    const parts=[e.subtasks.length?t(`${e.subtasks.length} 个子任务`,`${e.subtasks.length} sub-steps`):'',notes?t('有备注','notes'):''].filter(Boolean);
    form.querySelector('[data-tb-more-summary]').textContent=parts.length?parts.join(' · '):t('子任务与备注','Sub-steps and notes');
    form.querySelector('[data-tb-save]').disabled=busy||!field('title').value.trim();
  }
  let busy=false;
  function addStep(input){
    const title=input.value.trim();if(!title||e.subtasks.length>=100)return;
    e.subtasks.push({id:stepId(),title,completed:false});input.value='';renderSteps();
    form.querySelector('[data-tb-steps] li:last-child')?.animate?.([{opacity:0,transform:'translateY(-4px)'},{opacity:1,transform:'none'}],{duration:200,easing:'ease-out'});
  }
  function quickDate(kind){
    const day=fromKey(today).getDay();
    if(kind==='weekend')return addDays(today,day===0?0:6-day);
    if(kind==='monday')return addDays(today,((8-day)%7)||7);
    return addDays(today,Number(kind));
  }
  function submit(){
    if(busy)return;
    const pendingStep=form.querySelector('[data-tb-step-input]');if(pendingStep.value.trim())addStep(pendingStep);
    const next={title:field('title').value.trim(),notes:field('notes').value,date:field('date').value,time:field('time').value,end:field('end').value,
      allDay:e.allDay,days:e.days,color:e.color,reminder:Number(field('reminder').value),frequency:field('frequency').value,
      subtasks:e.subtasks.map(({id,title,completed})=>({id,title:title.trim(),completed})).filter(step=>step.title)};
    const invalid=node=>{node.focus();node.animate?.([{transform:'translateX(0)'},{transform:'translateX(-5px)'},{transform:'translateX(5px)'},{transform:'translateX(0)'}],{duration:260});};
    if(!next.title)return invalid(field('title'));
    if(!dateRe.test(next.date))return invalid(field('date'));
    if(!next.allDay&&!timeRe.test(next.time))return invalid(field('time'));
    const schedule={date:next.date,allDay:next.allDay,days:next.days,...(next.allDay?{}:{time:next.time,...(timeRe.test(next.end)?{end:next.end}:{})})};
    const commands=[];
    if(e.id){
      const old=original;if(!old){close();return;}
      const fields={id:e.id};
      for(const key of ['title','notes','color','frequency'])if(next[key]!==old[key])fields[key]=next[key];
      if(next.reminder!==old.reminder&&next.reminder!==-2)fields.reminder=next.reminder;
      const moved=next.date!==old.date||next.allDay!==old.allDay||next.days!==Math.max(1,old.days||1)||
        (!next.allDay&&(next.time!==old.time||next.end!==old.end));
      if(moved)Object.assign(fields,schedule);
      if(JSON.stringify(next.subtasks)!==JSON.stringify(old.subtasks))fields.subtasks=next.subtasks;
      if(Object.keys(fields).length>1)commands.push({action:'update',fields});
      if(e.completed!==old.completed)commands.push({action:e.completed?'complete':'restore',fields:{id:e.id}});
    }else{
      const fields={title:next.title,color:next.color,...schedule};
      if(next.notes.trim())fields.notes=next.notes;
      if(next.reminder>=0)fields.reminder=next.reminder;
      if(next.frequency!=='none')fields.frequency=next.frequency;
      if(next.subtasks.length)fields.subtasks=next.subtasks;
      commands.push({action:'add',fields});
    }
    if(!commands.length){close();return;}
    onSave?.(commands,next);
  }
  const onClick=event=>{
    const target=event.target.closest('button,[data-tb-close]');if(!target||!container.contains(target))return;
    const d=target.dataset;
    if(d.tbClose!==undefined)close();
    else if(d.tbColor){e.color=d.tbColor;form.querySelectorAll('[data-tb-color]').forEach(node=>node.setAttribute('aria-checked',String(node===target)));
      form.querySelector('[data-tb-color-name]').textContent=colorNames()[d.tbColor];}
    else if(d.tbStatus){e.completed=d.tbStatus==='true';form.querySelectorAll('[data-tb-status]').forEach(node=>node.setAttribute('aria-checked',String(node===target)));}
    else if(d.tbAllday){e.allDay=d.tbAllday==='true';sync();}
    else if(d.tbDays){e.days=Math.min(366,Math.max(1,e.days+Number(d.tbDays)));sync();}
    else if(d.tbQuickDate){field('date').value=quickDate(d.tbQuickDate);sync();
      form.querySelectorAll('[data-tb-quick-date]').forEach(node=>node.classList.toggle('is-active',node===target));}
    else if(d.tbMore!==undefined){e.more=!e.more;target.setAttribute('aria-expanded',String(e.more));
      form.querySelector('[data-tb-more-body]').classList.toggle('is-open',e.more);
      if(e.more)setTimeout(()=>form.querySelector('[data-tb-step-input]')?.focus(),140);}
    else if(d.tbStepEditCheck){const step=e.subtasks.find(item=>item.id===d.tbStepEditCheck);if(step){step.completed=!step.completed;renderSteps();}}
    else if(d.tbStepRemove){e.subtasks=e.subtasks.filter(item=>item.id!==d.tbStepRemove);renderSteps();}
    else if(d.tbSheetDelete!==undefined){if(e.id)onDelete?.(e.id);}
  };
  const onInput=event=>{
    if(event.target.matches('[data-tb-step-title]')){const step=e.subtasks.find(item=>item.id===event.target.dataset.tbStepTitle);if(step)step.title=event.target.value;}
    else sync();
    showError('');
  };
  const onKey=event=>{
    if(event.key==='Escape'){event.stopPropagation();close();return;}
    if(event.key!=='Enter')return;
    if(event.ctrlKey||event.metaKey){event.preventDefault();submit();return;}
    if(event.target.matches('[data-tb-step-input]')){event.preventDefault();addStep(event.target);return;}
    if(event.target.matches('[data-tb-step-title]')){event.preventDefault();form.querySelector('[data-tb-step-input]')?.focus();}
  };
  const onSubmit=event=>{event.preventDefault();event.stopPropagation();submit();};
  const onPointer=event=>{if(onDrag&&event.button===0&&event.target.closest('[data-tb-drag]')&&!event.target.closest('button'))onDrag();};
  container.addEventListener('click',onClick);container.addEventListener('input',onInput);container.addEventListener('change',onInput);
  container.addEventListener('keydown',onKey,true);form.addEventListener('submit',onSubmit);container.addEventListener('pointerdown',onPointer);
  function detach(){
    container.removeEventListener('click',onClick);container.removeEventListener('input',onInput);container.removeEventListener('change',onInput);
    container.removeEventListener('keydown',onKey,true);container.removeEventListener('pointerdown',onPointer);
  }
  function close(){
    if(closed)return;closed=true;detach();container.classList.remove('is-open');
    const done=()=>{container.hidden=true;container.innerHTML='';onClose?.();};
    if(reduced()||standalone)done();else setTimeout(done,220);
  }
  function showError(text){const node=form.querySelector('[data-tb-sheet-error]');if(node){node.textContent=text;node.hidden=!text;}}
  function setBusy(value){busy=value;form.toggleAttribute('aria-busy',value);sync();}
  renderSteps();
  requestAnimationFrame(()=>{container.classList.add('is-open');field('title').focus();field('title').setSelectionRange(e.title.length,e.title.length);});
  return {close,showError,setBusy,get closed(){return closed;}};
}
