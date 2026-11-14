// Shared to-do board for the Dynamic Island "待办" page and the desktop to-do
// list page. Every board reads and writes the one native to-do store (the same
// tasks as the calendar, the desktop list and the island), so all views agree.
import {escapeHTML as h} from './dom.js';
import {t} from './localization.js';

import {COLORS,COLOR_RGB,FREQUENCIES,dateRe,timeRe,pad,toKey,fromKey,addDays,dayDiff,weekdays,weekdayName,dayLabel,weekRange,icon} from './todo-common.js';
import {openTodoSheet,draftOf} from './todo-sheet.js';
const ACTIONS=new Set(['snapshot','add','update','complete','restore','delete','open']);
const FIELDS=new Set(['id','title','notes','date','time','color','end','allDay','days','reminder','frequency','subtasks']);
const validSteps=list=>Array.isArray(list)&&list.length<=100&&new Set(list.map(step=>step?.id)).size===list.length&&
  list.every(step=>step&&typeof step.id==='string'&&step.id&&step.id.length<=200&&typeof step.title==='string'&&
    step.title.trim()&&step.title.length<=500&&typeof step.completed==='boolean'&&Object.keys(step).length===3);

export function validTodoBoardCommand(action,fields={}){
  if(!ACTIONS.has(action)||!fields||typeof fields!=='object')return false;
  for(const [key,value] of Object.entries(fields)){
    if(!FIELDS.has(key))return false;
    if(key==='allDay'){if(typeof value!=='boolean')return false;continue;}
    if(key==='days'){if(!Number.isInteger(value)||value<1||value>366)return false;continue;}
    if(key==='reminder'){if(!Number.isInteger(value)||value<-1||value>525600)return false;continue;}
    if(key==='subtasks'){if(!validSteps(value))return false;continue;}
    if(typeof value!=='string')return false;
    if(key==='end'&&!timeRe.test(value))return false;
    if(key==='frequency'&&!FREQUENCIES.includes(value))return false;
    if(key==='title'&&(value.trim()===''||value.length>500))return false;
    if(key==='notes'&&value.length>10000)return false;
    if(key==='date'&&!dateRe.test(value))return false;
    if(key==='time'&&value!==''&&!timeRe.test(value))return false;
    if(key==='color'&&!COLORS.includes(value))return false;
    if(key==='id'&&(value===''||value.length>500))return false;
  }
  if(action==='add')return typeof fields.title==='string'&&typeof fields.date==='string'&&!('id' in fields);
  if(action==='snapshot')return Object.keys(fields).length===0;
  return typeof fields.id==='string';
}
export function validTodoBoardState(m){
  if(!m||m.version!==1||m.type!=='todo-board-state')return false;
  const keys=['version','type','result','error','available','writable','today','tasks'];
  if(Object.keys(m).some(key=>!keys.includes(key))||keys.some(key=>!(key in m)))return false;
  if(!['snapshot','applied','error'].includes(m.result)||typeof m.error!=='string'||typeof m.available!=='boolean'||
     typeof m.writable!=='boolean'||(m.today!==''&&!dateRe.test(m.today))||!Array.isArray(m.tasks)||m.tasks.length>1000)return false;
  return m.tasks.every(task=>task&&typeof task.id==='string'&&task.id&&typeof task.title==='string'&&typeof task.notes==='string'&&
    typeof task.completed==='boolean'&&typeof task.repeat==='boolean'&&COLORS.includes(task.color)&&
    (task.date===''||dateRe.test(task.date))&&(task.time===''||timeRe.test(task.time))&&(task.end===''||timeRe.test(task.end))&&
    typeof task.allDay==='boolean'&&Number.isInteger(task.days)&&(task.endDate===''||dateRe.test(task.endDate))&&
    FREQUENCIES.includes(task.frequency)&&Number.isInteger(task.reminder)&&Array.isArray(task.subtasks)&&task.subtasks.length<=100&&
    task.subtasks.every(step=>step&&typeof step.id==='string'&&typeof step.title==='string'&&typeof step.completed==='boolean'));
}

const columns=()=>[
  {key:'today',label:t('今天','Today'),hint:t('今天与已逾期','Today and overdue'),dot:'#5b7bea'},
  {key:'later',label:t('计划中','Upcoming'),hint:t('之后的安排','Later this month and beyond'),dot:'#b48cff'},
  {key:'done',label:t('已完成','Completed'),hint:t('最近完成','Recently finished'),dot:'#3fcf8e'},
];
const filters=()=>[['all',t('全部','All')],['today',t('今天','Today')],['week',t('本周','This week')],['overdue',t('已逾期','Overdue')]];

export function todoBoardMarkup(variant='page'){
  return `<section class="todo-board todo-board--${h(variant)}" data-todo-board aria-label="${t('待办','To-dos')}"></section>`;
}

// One controller per content root; boards are re-rendered from shared data.
export function mountTodoBoards(root,{bridge,notify=()=>{}}={}){
  const doc=root.ownerDocument,view=doc.defaultView;
  let data={available:false,writable:false,today:toKey(new Date()),tasks:[],loaded:false};
  let pending=0,lastError='',refreshFrame=null,disposed=false;
  const refreshPending=new Map();
  const ui=new WeakMap();
  const send=(action,fields={})=>{
    if(!validTodoBoardCommand(action,fields))return false;
    let sent=false;try{sent=bridge?.todoBoard?.(action,fields)===true;}catch{sent=false;}
    if(sent&&action!=='snapshot'&&action!=='open'){pending++;boards().forEach(board=>board.setAttribute('aria-busy','true'));}
    if(!sent&&action!=='snapshot'){lastError='unavailable';boards().forEach(refresh);}
    return sent;
  };
  const boards=()=>[...root.querySelectorAll('[data-todo-board]')];
  const stateOf=board=>{
    let s=ui.get(board);
    if(!s){const today=data.today;s={filter:'all',search:'',expanded:new Set(),selected:today,month:today.slice(0,7),adding:null,sheet:null,drag:null,built:false};ui.set(board,s);}
    return s;
  };
  // --- derived data --------------------------------------------------------
  const today=()=>data.today||toKey(new Date());
  const columnOf=task=>task.completed?'done':!task.date||task.date<=today()?'today':'later';
  const visible=s=>{
    const query=s.search.trim().toLowerCase(),[weekStart,weekEnd]=weekRange(today());
    return data.tasks.filter(task=>{
      if(query&&!task.title.toLowerCase().includes(query)&&!task.notes.toLowerCase().includes(query))return false;
      if(s.filter==='today')return task.date===today()||(!task.completed&&task.date&&task.date<today());
      if(s.filter==='week')return task.date>=weekStart&&task.date<=weekEnd;
      if(s.filter==='overdue')return !task.completed&&task.date&&task.date<today();
      return true;
    });
  };
  const sortOpen=(a,b)=>(a.date||'9999').localeCompare(b.date||'9999')||(a.time||'99').localeCompare(b.time||'99')||a.title.localeCompare(b.title);
  // --- rendering -----------------------------------------------------------
  function shell(board){
    const s=stateOf(board);
    board.innerHTML=`
      <header class="tb-head">
        <div class="tb-head__title"><h2>${t('待办','To-dos')}</h2><p data-tb-summary></p></div>
        <div class="tb-head__tools">
          <label class="tb-search">${icon.search}<input type="search" data-tb-search placeholder="${t('搜索任务','Search tasks')}" value="${h(s.search)}" autocomplete="off" spellcheck="false"></label>
          <button type="button" class="tb-primary" data-tb-new>${icon.plus}<span>${t('新建待办','New task')}</span></button>
        </div>
      </header>
      <div class="tb-toolbar">
        <div class="tb-filters" role="tablist" aria-label="${t('筛选','Filter')}">${filters().map(([key,label])=>
          `<button type="button" role="tab" data-tb-filter="${key}" aria-selected="${s.filter===key}">${label}<span data-tb-count="${key}"></span></button>`).join('')}<i class="tb-filters__thumb" aria-hidden="true"></i></div>
        <p class="tb-banner" data-tb-banner role="status" hidden></p>
      </div>
      <div class="tb-main">
        <div class="tb-columns" data-tb-columns></div>
        <aside class="tb-side" data-tb-side aria-label="${t('日历','Calendar')}"></aside>
      </div>
      <div class="tb-sheet" data-tb-sheet hidden></div>`;
    s.built=true;
  }
  // Progress chip that expands the task's sub-steps in place.
  function stepsRow(task,s){
    const steps=task.subtasks||[];if(!steps.length)return '';
    const done=steps.filter(step=>step.completed).length,open=s.expanded.has(task.id);
    return `<div class="tb-sub${open?' is-open':''}">
      <button type="button" class="tb-sub__chip${done===steps.length?' is-complete':''}" data-tb-steps-toggle="${h(task.id)}" aria-expanded="${open}" aria-label="${h(t(`子任务 ${done}/${steps.length}`,`Sub-steps ${done} of ${steps.length}`))}">
        ${icon.steps}<span>${done}/${steps.length}</span><i style="--p:${done/steps.length}"></i>${icon.chevron}</button>
      <div class="tb-collapse${open?' is-open':''}"><div class="tb-collapse__inner"><ul class="tb-sub__list">${open?steps.map(step=>`<li class="${step.completed?'is-done':''}">
        <button type="button" class="tb-step-check" data-tb-step-check="${h(task.id)}" data-step="${h(step.id)}" role="checkbox" aria-checked="${step.completed}" aria-label="${h(step.title)}" ${data.writable?'':'disabled'}>${icon.check}</button>
        <span>${h(step.title)}</span></li>`).join(''):''}</ul></div></div>
    </div>`;
  }
  function card(task,s){
    const overdue=!task.completed&&task.date&&task.date<today();
    const span=task.days>1&&task.endDate?`${dayLabel(task.date,today())} – ${dayLabel(task.endDate,today())}`:dayLabel(task.date,today());
    const when=[span,task.allDay?t('全天','All day'):task.time].filter(Boolean).join(' ');
    return `<article class="tb-card${task.completed?' is-done':''}${overdue?' is-overdue':''}" data-tb-card="${h(task.id)}" draggable="${data.writable}" style="--tc:${COLOR_RGB[task.color]||'rgba(255,255,255,.34)'}">
      <button type="button" class="tb-check" data-tb-toggle="${h(task.id)}" role="checkbox" aria-checked="${task.completed}" aria-label="${h(task.completed?t('标记为未完成','Mark as not done'):t('完成','Complete'))} ${h(task.title)}" ${data.writable?'':'disabled'}>${icon.check}</button>
      <button type="button" class="tb-card__body" data-tb-edit="${h(task.id)}">
        <span class="tb-card__title">${h(task.title)}</span>
        <span class="tb-card__meta"><span class="tb-chip${overdue?' is-overdue':''}">${icon.clock}${h(overdue?t('逾期 · ','Overdue · ')+when:when)}</span>${task.repeat?`<span class="tb-meta-icon" title="${t('重复','Repeats')}">${icon.repeat}</span>`:''}${task.notes?`<span class="tb-meta-icon" title="${t('有备注','Has notes')}">${icon.note}</span>`:''}${task.reminder>=0?`<span class="tb-meta-icon" title="${t('有提醒','Reminder set')}">${icon.bell}</span>`:''}${task.color!=='none'?`<i class="tb-dot" aria-hidden="true"></i>`:''}</span>
      </button>
      <span class="tb-card__actions">
        <button type="button" data-tb-delete="${h(task.id)}" aria-label="${t('删除','Delete')}" title="${t('删除','Delete')}" ${data.writable?'':'disabled'}>${icon.trash}</button>
      </span>
      ${stepsRow(task,s)}
    </article>`;
  }
  function renderColumns(board,s){
    const host=board.querySelector('[data-tb-columns]');
    const tasks=visible(s);
    const markup=columns().map(column=>{
      let items=tasks.filter(task=>columnOf(task)===column.key);
      items=column.key==='done'?items.slice(0,40):items.sort(sortOpen);
      const adding=s.adding===column.key;
      return `<section class="tb-column" data-tb-column="${column.key}" style="--dot:${column.dot}" aria-label="${h(column.label)}">
        <header class="tb-column__head"><i aria-hidden="true"></i><h3>${h(column.label)}</h3><span class="tb-badge">${items.length}</span>
          ${column.key!=='done'&&data.writable?`<button type="button" class="tb-icon" data-tb-add="${column.key}" aria-label="${t('添加任务','Add task')}">${icon.plus}</button>`:''}</header>
        <div class="tb-column__list" data-tb-drop="${column.key}">
          ${adding?`<form class="tb-quick" data-tb-quick="${column.key}"><input data-tb-quick-input maxlength="500" placeholder="${column.key==='today'?t('今天要做什么？','What needs doing today?'):t('明天要做什么？','What’s next for tomorrow?')}" value="${h(s.addingText||'')}" autocomplete="off"><span>${t('回车添加','Enter to add')}</span></form>`:
            column.key!=='done'&&data.writable?`<button type="button" class="tb-ghost" data-tb-add="${column.key}">${icon.plus}${t('添加任务','Add task')}</button>`:''}
          ${items.map(task=>card(task,s)).join('')}
          ${items.length?'':`<p class="tb-column__empty">${column.key==='done'?t('完成的任务会出现在这里','Finished tasks appear here'):t('拖动任务到这里','Drop tasks here')}</p>`}
        </div>
      </section>`;
    }).join('');
    if(markup===s.columnsMarkup)return;
    s.columnsMarkup=markup;
    const before=new Map([...host.querySelectorAll('[data-tb-card]')].map(node=>[node.dataset.tbCard,node.getBoundingClientRect()]));
    host.innerHTML=markup;
    if(s.adding){
      const input=host.querySelector('[data-tb-quick-input]');
      if(input){input.focus({preventScroll:true});input.setSelectionRange(input.value.length,input.value.length);}
    }
    if(view.matchMedia('(prefers-reduced-motion: reduce)').matches)return;
    for(const node of host.querySelectorAll('[data-tb-card]')){
      const from=before.get(node.dataset.tbCard);
      if(!from){node.animate?.([{opacity:0,transform:'translateY(6px) scale(.98)'},{opacity:1,transform:'none'}],{duration:260,easing:'cubic-bezier(.2,.8,.2,1)'});continue;}
      const to=node.getBoundingClientRect(),x=from.left-to.left,y=from.top-to.top;
      if(Math.abs(x)+Math.abs(y)>.5)node.animate?.([{transform:`translate(${x}px,${y}px)`},{transform:'none'}],{duration:320,easing:'cubic-bezier(.2,.9,.25,1.05)'});
    }
  }
  function renderSide(board,s){
    const side=board.querySelector('[data-tb-side]');
    const [year,month]=s.month.split('-').map(Number);
    const first=new Date(year,month-1,1),offset=(first.getDay()+6)%7,days=new Date(year,month,0).getDate();
    const byDay=new Map();
    for(const task of data.tasks){if(!task.date)continue;const list=byDay.get(task.date)||[];list.push(task);byDay.set(task.date,list);}
    const names=weekdays();const header=[1,2,3,4,5,6,0].map(index=>`<span>${names[index]}</span>`).join('');
    let cells='';
    for(let index=0;index<42;index++){
      const day=index-offset+1,key=toKey(new Date(year,month-1,day)),outside=day<1||day>days;
      const list=byDay.get(key)||[],open=list.filter(task=>!task.completed);
      const dots=open.slice(0,3).map(task=>`<i style="--c:${COLOR_RGB[task.color]||'#8e9bff'}"></i>`).join('')||(list.length?'<i style="--c:rgba(255,255,255,.3)"></i>':'');
      cells+=`<button type="button" class="tb-day${outside?' is-outside':''}${key===today()?' is-today':''}${key===s.selected?' is-selected':''}" data-tb-day="${key}" data-tb-drop-day="${key}" aria-label="${h(dayLabel(key,today()))}${list.length?` · ${list.length}`:''}" aria-pressed="${key===s.selected}"><span>${new Date(year,month-1,day).getDate()}</span><span class="tb-day__dots">${dots}</span></button>`;
    }
    const list=(byDay.get(s.selected)||[]).slice().sort((a,b)=>Number(a.completed)-Number(b.completed)||(a.time||'99').localeCompare(b.time||'99'));
    const open=list.filter(task=>!task.completed).length;
    const monthName=t(`${year} 年 ${month} 月`,new Date(year,month-1,1).toLocaleDateString('en-US',{year:'numeric',month:'long'}));
    const selected=fromKey(s.selected);
    const markup=`
      <div class="tb-cal">
        <header class="tb-cal__head"><button type="button" class="tb-icon" data-tb-month="-1" aria-label="${t('上个月','Previous month')}">${icon.left}</button>
          <h3>${h(monthName)}</h3><button type="button" class="tb-icon" data-tb-month="1" aria-label="${t('下个月','Next month')}">${icon.right}</button>
          <button type="button" class="tb-today" data-tb-today ${s.selected===today()&&s.month===today().slice(0,7)?'disabled':''}>${t('今天','Today')}</button></header>
        <div class="tb-cal__week">${header}</div>
        <div class="tb-cal__grid">${cells}</div>
      </div>
      <div class="tb-agenda">
        <header><div><h3>${h(t(`${selected.getMonth()+1}月${selected.getDate()}日 ${weekdayName(s.selected)}`,`${weekdayName(s.selected)}, ${selected.toLocaleDateString('en-US',{month:'long',day:'numeric'})}`))}</h3>
          <p>${h(t(`${open} 项待办 · ${list.length-open} 项已完成`,`${open} open · ${list.length-open} done`))}</p></div>
          ${data.writable?`<button type="button" class="tb-icon" data-tb-new-day="${s.selected}" aria-label="${t('在这一天添加任务','Add a task on this day')}">${icon.plus}</button>`:''}</header>
        ${list.length?`<ul class="tb-agenda__list">${list.map(task=>`<li class="${task.completed?'is-done':''}" style="--tc:${COLOR_RGB[task.color]||'rgba(255,255,255,.34)'}">
            <button type="button" class="tb-check tb-check--small" data-tb-toggle="${h(task.id)}" role="checkbox" aria-checked="${task.completed}" aria-label="${h(task.title)}" ${data.writable?'':'disabled'}>${icon.check}</button>
            <button type="button" class="tb-agenda__title" data-tb-edit="${h(task.id)}">${h(task.title)}</button><time>${h(task.allDay?t('全天','All day'):task.time)}</time>${stepsRow(task,s)}</li>`).join('')}</ul>`:
          `<div class="tb-agenda__empty">${icon.empty}<p>${t('这一天还没有安排','Nothing planned for this day')}</p>${data.writable?`<button type="button" class="tb-soft" data-tb-new-day="${s.selected}">${t('添加任务','Add task')}</button>`:''}</div>`}
      </div>`;
    if(markup!==s.sideMarkup){s.sideMarkup=markup;side.innerHTML=markup;}
  }
  function renderHead(board,s){
    const open=data.tasks.filter(task=>!task.completed).length,done=data.tasks.length-open;
    const summary=board.querySelector('[data-tb-summary]');
    summary.textContent=!data.loaded?t('正在读取待办…','Loading tasks…'):!data.available?t('待办存储暂不可用','The to-do store is unavailable'):
      t(`${open} 项待办 · ${done} 项已完成`,`${open} open · ${done} completed`)+(data.writable?'':t(' · 只读',' · read-only'));
    const counts={all:0,today:0,week:0,overdue:0},[weekStart,weekEnd]=weekRange(today());
    for(const task of data.tasks){
      if(task.completed)continue;counts.all++;
      if(task.date&&task.date<=today())counts.today++;
      if(task.date>=weekStart&&task.date<=weekEnd)counts.week++;
      if(task.date&&task.date<today())counts.overdue++;
    }
    board.querySelectorAll('[data-tb-count]').forEach(node=>{const value=counts[node.dataset.tbCount];node.textContent=value?String(value):'';});
    board.querySelectorAll('[data-tb-filter]').forEach(node=>node.setAttribute('aria-selected',String(node.dataset.tbFilter===s.filter)));
    const active=board.querySelector('[data-tb-filter][aria-selected="true"]'),thumb=board.querySelector('.tb-filters__thumb');
    if(active&&thumb){thumb.style.width=`${active.offsetWidth}px`;thumb.style.transform=`translateX(${active.offsetLeft-4}px)`;}
    board.querySelector('[data-tb-new]').disabled=!data.writable;
    const banner=board.querySelector('[data-tb-banner]');
    const message={'read-only':t('待办当前为只读，暂时无法修改','To-dos are read-only right now'),'invalid-time':t('日期或时间无效','That date or time is not valid'),
      'not-found':t('这条待办已被删除或修改','This task was changed or removed elsewhere'),limit:t('待办数量已达上限','The task limit has been reached'),
      unavailable:t('桌面宿主未连接，修改没有保存','Desktop host disconnected; changes were not saved'),stale:t('时区已变化，请重试','The time zone changed; try again')}[lastError]||
      (lastError?t('保存失败，请重试','Could not save; please try again'):'');
    banner.textContent=message;banner.hidden=!message;
    board.toggleAttribute('data-tb-readonly',!data.writable);
  }
  function refresh(board){
    const s=stateOf(board);
    if(!s.built)shell(board);
    renderHead(board,s);renderColumns(board,s);renderSide(board,s);
  }
  function scheduleRefresh(board,columnsOnly=false){
    if(disposed)return;
    refreshPending.set(board,refreshPending.has(board)?refreshPending.get(board)&&columnsOnly:columnsOnly);
    if(refreshFrame!==null)return;
    refreshFrame=view.requestAnimationFrame(()=>{
      refreshFrame=null;
      for(const [item,onlyColumns] of refreshPending)if(root.contains(item)){
        if(onlyColumns)renderColumns(item,stateOf(item));else refresh(item);
      }
      refreshPending.clear();
    });
  }
  // --- editor sheet -------------------------------------------------------
  function openSheet(board,draft){
    const s=stateOf(board);s.sheet?.close();
    const original=draft.id?taskOf(draft.id):null;
    s.sheet=openTodoSheet(board.querySelector('[data-tb-sheet]'),{draft,today:today(),original,
      onSave:(commands,next)=>{commands.forEach(command=>send(command.action,command.fields));
        s.selected=next.date;s.month=next.date.slice(0,7);s.sheet?.close();},
      onDelete:id=>{send('delete',{id});s.sheet?.close();},
      onClose:()=>{s.sheet=null;}});
  }
  // --- interaction ---------------------------------------------------------
  const boardOf=node=>node.closest('[data-todo-board]');
  const taskOf=id=>data.tasks.find(task=>task.id===id);
  function quickAdd(board,column,text){
    const s=stateOf(board),title=text.trim();
    s.adding=null;s.addingText='';
    if(title)send('add',{title,date:column==='later'?addDays(today(),1):today()});
    refresh(board);
  }
  function moveTo(id,target,day){
    const task=taskOf(id);if(!task||!data.writable)return;
    if(target==='done'){if(!task.completed)send('complete',{id});return;}
    const date=day||(target==='later'?(task.date>today()?task.date:addDays(today(),1)):today());
    if(task.completed)send('restore',{id});
    if(task.date!==date)send('update',{id,date});
  }
  const onClick=event=>{
    const target=event.target.closest('button,[data-tb-close]');const board=target&&boardOf(target);if(!board||target.closest('[data-tb-sheet]'))return;
    const s=stateOf(board),d=target.dataset;
    if(d.tbFilter){s.filter=d.tbFilter;refresh(board);}
    else if(d.tbToggle){const task=taskOf(d.tbToggle);if(task){s.columnsMarkup=s.sideMarkup=null;target.setAttribute('aria-checked',String(!task.completed));target.closest('.tb-card,li')?.classList.toggle('is-done',!task.completed);
      setTimeout(()=>send(task.completed?'restore':'complete',{id:task.id}),180);}}
    else if(d.tbEdit){const task=taskOf(d.tbEdit);if(task)openSheet(board,draftOf(task,today()));}
    else if(d.tbDelete){send('delete',{id:d.tbDelete});}
    else if(d.tbAdd){s.adding=d.tbAdd;s.addingText='';refresh(board);}
    else if(d.tbNew!==undefined||d.tbNewDay){openSheet(board,draftOf(null,d.tbNewDay||s.selected||today()));}
    else if(d.tbMonth){const [y,m]=s.month.split('-').map(Number);s.month=toKey(new Date(y,m-1+Number(d.tbMonth),1)).slice(0,7);renderSide(board,s);
      board.querySelector('.tb-cal__grid')?.animate?.([{opacity:.2,transform:`translateX(${Number(d.tbMonth)*14}px)`},{opacity:1,transform:'none'}],{duration:280,easing:'cubic-bezier(.2,.8,.2,1)'});}
    else if(d.tbDay){s.selected=d.tbDay;if(d.tbDay.slice(0,7)!==s.month)s.month=d.tbDay.slice(0,7);renderSide(board,s);
      board.querySelector('.tb-agenda')?.animate?.([{opacity:0,transform:'translateY(4px)'},{opacity:1,transform:'none'}],{duration:220,easing:'ease-out'});}
    else if(d.tbToday!==undefined){s.selected=today();s.month=today().slice(0,7);renderSide(board,s);}
    else if(d.tbStepsToggle){const id=d.tbStepsToggle,open=!s.expanded.has(id);open?s.expanded.add(id):s.expanded.delete(id);
      const task=taskOf(id),template=doc.createElement('template');
      if(open&&task)template.innerHTML=stepsRow(task,s);
      board.querySelectorAll('[data-tb-steps-toggle]').forEach(node=>{if(node.dataset.tbStepsToggle!==id)return;
        node.setAttribute('aria-expanded',String(open));const row=node.closest('.tb-sub');
        const list=row.querySelector('.tb-sub__list');
        if(open&&task)list.innerHTML=template.content.querySelector('.tb-sub__list').innerHTML;
        row.classList.toggle('is-open',open);row.querySelector('.tb-collapse').classList.toggle('is-open',open);
      });}
    else if(d.tbStepCheck){const task=taskOf(d.tbStepCheck);if(task&&data.writable){
      s.columnsMarkup=s.sideMarkup=null;
      const subtasks=task.subtasks.map(step=>({id:step.id,title:step.title,completed:step.id===d.step?!step.completed:step.completed}));
      target.setAttribute('aria-checked',String(target.getAttribute('aria-checked')!=='true'));target.closest('li')?.classList.toggle('is-done');
      send('update',{id:task.id,subtasks});}}
  };
  const onSubmit=event=>{
    const board=boardOf(event.target);if(!board)return;event.preventDefault();
    if(event.target.matches('[data-tb-form]'))return;
    else if(event.target.matches('[data-tb-quick]'))quickAdd(board,event.target.dataset.tbQuick,event.target.querySelector('input').value);
  };
  const onInput=event=>{
    const board=boardOf(event.target);if(!board)return;const s=stateOf(board);
    if(event.target.matches('[data-tb-search]')){s.search=event.target.value;scheduleRefresh(board,true);}
    else if(event.target.matches('[data-tb-quick-input]'))s.addingText=event.target.value;
  };
  const onKey=event=>{
    const board=boardOf(event.target);if(!board)return;const s=stateOf(board);
    if(event.key!=='Escape')return;
    if(s.adding){s.adding=null;refresh(board);event.stopPropagation();}
  };
  const onFocusOut=event=>{
    const board=boardOf(event.target);if(!board||!event.target.matches('[data-tb-quick-input]'))return;
    const s=stateOf(board),form=event.target.closest('[data-tb-quick]');
    setTimeout(()=>{if(s.adding&&!form.contains(doc.activeElement))quickAdd(board,form.dataset.tbQuick,event.target.value);},0);
  };
  // Drag a card onto a column or a calendar day to reschedule or complete it.
  const onDragStart=event=>{
    const node=event.target.closest?.('[data-tb-card]');const board=node&&boardOf(node);if(!board)return;
    stateOf(board).drag=node.dataset.tbCard;event.dataTransfer.effectAllowed='move';event.dataTransfer.setData('text/plain',node.dataset.tbCard);
    requestAnimationFrame(()=>node.classList.add('is-dragging'));board.classList.add('is-dragging');
  };
  const dropTarget=event=>event.target.closest?.('[data-tb-drop],[data-tb-drop-day]');
  const onDragOver=event=>{
    const zone=dropTarget(event);const board=zone&&boardOf(zone);if(!board||!stateOf(board).drag)return;
    event.preventDefault();event.dataTransfer.dropEffect='move';
    board.querySelectorAll('.is-drop').forEach(node=>{if(node!==zone)node.classList.remove('is-drop');});zone.classList.add('is-drop');
  };
  const endDrag=board=>{const s=stateOf(board);s.drag=null;board.classList.remove('is-dragging');
    board.querySelectorAll('.is-drop,.is-dragging').forEach(node=>node.classList.remove('is-drop','is-dragging'));};
  const onDrop=event=>{
    const zone=dropTarget(event);const board=zone&&boardOf(zone);if(!board)return;const id=stateOf(board).drag;
    event.preventDefault();endDrag(board);if(!id)return;
    if(zone.dataset.tbDropDay)moveTo(id,zone.dataset.tbDropDay>today()?'later':'today',zone.dataset.tbDropDay);
    else moveTo(id,zone.dataset.tbDrop);
  };
  const onDragEnd=event=>{const board=boardOf(event.target);if(board)endDrag(board);};
  const onTransitionEnd=event=>{
    const collapse=event.target;
    if(event.propertyName!=='grid-template-rows'||!collapse.matches('.tb-sub .tb-collapse')||collapse.classList.contains('is-open'))return;
    collapse.querySelector('.tb-sub__list')?.replaceChildren();
  };
  root.addEventListener('click',onClick);root.addEventListener('submit',onSubmit);root.addEventListener('input',onInput);
  root.addEventListener('keydown',onKey,true);root.addEventListener('focusout',onFocusOut);
  root.addEventListener('dragstart',onDragStart);root.addEventListener('dragover',onDragOver);root.addEventListener('drop',onDrop);root.addEventListener('dragend',onDragEnd);
  root.addEventListener('transitionend',onTransitionEnd);
  // Boards appear whenever the page re-renders; build and request fresh data.
  const observer=new view.MutationObserver(()=>{
    const fresh=boards().filter(board=>!stateOf(board).built);
    if(!fresh.length)return;
    fresh.forEach(refresh);send('snapshot');
  });
  observer.observe(root,{childList:true,subtree:true});
  if(boards().length){boards().forEach(refresh);send('snapshot');}
  return {
    receive(message){
      if(message?.type!=='todo-board-state')return false;
      const wasToday=data.today;
      data={available:message.available,writable:message.writable,today:message.today||toKey(new Date()),tasks:message.tasks,loaded:true};
      if(message.result!=='snapshot')pending=Math.max(0,pending-1);
      if(message.result==='error'){lastError=message.error||'save';notify(lastError);}
      else if(message.result==='applied')lastError='';
      for(const board of boards()){
        const s=stateOf(board);
        if(wasToday!==data.today&&s.selected===wasToday){s.selected=data.today;s.month=data.today.slice(0,7);}
        if(!pending)board.removeAttribute('aria-busy');
        scheduleRefresh(board);
      }
      return true;
    },
    dispose(){
      disposed=true;if(refreshFrame!==null)view.cancelAnimationFrame(refreshFrame);refreshPending.clear();
      observer.disconnect();
      root.removeEventListener('click',onClick);root.removeEventListener('submit',onSubmit);root.removeEventListener('input',onInput);
      root.removeEventListener('keydown',onKey,true);root.removeEventListener('focusout',onFocusOut);
      root.removeEventListener('dragstart',onDragStart);root.removeEventListener('dragover',onDragOver);root.removeEventListener('drop',onDrop);root.removeEventListener('dragend',onDragEnd);
      root.removeEventListener('transitionend',onTransitionEnd);
    },
  };
}
