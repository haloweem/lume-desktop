import {escapeHTML as h} from './dom.js';
import {t} from './localization.js';
import {lumeIcon} from './lume-icons.js';

// All component settings use the island's sidebar frame; services and commands
// remain owned by each component. Unfinished island pages stay out of navigation.
export function settingsFrame({title,description,tab,body,status='',error=false,scope='',backLabel=t('返回组件列表','Back to widgets'),variant='',extraPages=[],navigationPages,pageTitle}) {
  const island=variant==='island';
  // Music, quick launch and clock are hidden for now;
  // their routes still render a placeholder if reached directly.
  const pages=navigationPages??(island?[
    ['home',t('首页','Home')],['todo',t('待办','To-do')],['notifications',t('通知','Notifications')],['settings',t('设置','Settings')],['details',t('详情','Details')]
  ]:[...extraPages,['settings',t('设置','Settings')],['details',t('详情','Details')]]);
  const navigation=`<aside class="settings-side-nav" aria-label="${h(title)}">
    <button type="button" class="settings-side-nav__back lume-icon-button" data-action="back" aria-label="${h(backLabel)}" title="${h(backLabel)}">${lumeIcon('back')}</button>
    <nav class="settings-side-nav__links" role="tablist" aria-label="${h(title)}">
      ${pages.map(([key,label,icon=key])=>
        `<button type="button" role="tab" aria-label="${h(label)}" title="${h(label)}" aria-selected="${tab===key}" data-tab="${key}">${lumeIcon(icon,{selected:tab===key})}</button>`).join('')}
    </nav>
  </aside>`;
  const regionLabel=navigationPages?` ${pages.find(([key])=>key===tab)?.[1]||''}`:tab==='settings'?t('设置',' settings'):tab==='home'?t('主页',' home'):tab==='todo'?t('待办',' to-dos'):tab==='notifications'?t('通知',' notifications'):t('详情',' details');
  const heading=pageTitle!==undefined||['settings','details'].includes(tab)?`<h1 class="settings-component-title">${h(pageTitle??title)}</h1>`:'';
  return `<div class="settings settings--sidebar${island?' settings--island':''}" data-settings-scope="${h(scope)}" aria-label="${h(title)}">${navigation}<div class="settings-card"><div class="settings-body" role="region" aria-label="${h(title)}${regionLabel}">${heading}${body}</div><p role="${error?'alert':'status'}" class="status ${error?'error':''}" ${status?'':'hidden'}>${h(status)}</p></div></div>`;
}

export function settingsGroup(body,label='',{collapsible=false}={}) {
  const group=collapsible?'details':'div',head=collapsible?'summary':'div';
  return `<${group} class="settings-group">${label?`<${head} class="settings-group__head"><h2>${h(label)}</h2></${head}>`:''}<div class="settings-group__body">${body}</div></${group}>`;
}

export function settingsSelect(id,label,value,options,disabled=false) {
  const selected=options.find(option=>option[0]===value);
  return `<div class="setting-select"><button class="setting-select-trigger" id="${id}" type="button" role="combobox" aria-label="${h(label)}" aria-haspopup="listbox" aria-expanded="false" aria-controls="${id}-options" data-select="${id}" ${disabled?'disabled':''}><span>${h(selected?.[1]||'')}</span><svg viewBox="0 0 12 12" width="12" height="12"><path d="m2 4 4 4 4-4" fill="none" stroke="currentColor"/></svg></button><div class="setting-select-options" id="${id}-options" role="listbox" aria-label="${h(label)}" hidden>${options.map(([key,text])=>`<button type="button" role="option" aria-selected="${key===value}" data-select-value="${key}" data-select-owner="${id}">${h(text)}<span aria-hidden="true">${key===value?'✓':''}</span></button>`).join('')}</div></div>`;
}

// A pending native request must block activation without discarding keyboard focus.
export function syncSettingsControlBusy(control,busy) {
  control.setAttribute('aria-disabled',String(busy));
  control.disabled=busy && control.ownerDocument.activeElement!==control;
  if(control.tagName==='INPUT')control.readOnly=busy;
}

export function syncSettingsSelect(root,id,value,options,disabled,preserveFocus=false) {
  const trigger=root.querySelector(`#${id}`);
  if(!trigger) return;
  trigger.querySelector('span').textContent=options.find(([key])=>key===value)?.[1] || '';
  if(preserveFocus)syncSettingsControlBusy(trigger,disabled);
  else trigger.disabled=disabled;
  const list=trigger.nextElementSibling;
  if(disabled) {list.hidden=true;trigger.setAttribute('aria-expanded','false');}
  list.querySelectorAll('[role="option"]').forEach(option=>{
    const selected=option.dataset.selectValue===value;
    option.disabled=disabled;
    option.setAttribute('aria-selected',String(selected));
    option.querySelector('span').textContent=selected?'✓':'';
  });
}

export function mountSettingsSelect(root,onChange) {
  const close=()=>root.querySelectorAll('.setting-select').forEach(select=>{
    select.querySelector('[role="listbox"]').hidden=true;
    select.querySelector('[role="combobox"]').setAttribute('aria-expanded','false');
  });
  const click=event=>{
    const trigger=event.target.closest('[data-select]');
    if(trigger && !trigger.disabled && trigger.getAttribute('aria-disabled')!=='true') {
      const list=trigger.nextElementSibling,opening=list.hidden;
      close(); list.hidden=!opening; trigger.setAttribute('aria-expanded',String(opening));
      if(opening) (list.querySelector('[aria-selected="true"]')||list.querySelector('button'))?.focus();
    }
    const option=event.target.closest('[data-select-value]');
    if(option) {
      const owner=root.querySelector(`#${option.dataset.selectOwner}`);
      if(!owner || owner.disabled || owner.getAttribute('aria-disabled')==='true' || option.disabled || option.closest('[role="listbox"]').hidden) return;
      close(); owner.focus(); onChange(option.dataset.selectOwner,option.dataset.selectValue);
    }
  };
  const key=event=>{
    const select=event.target.closest('.setting-select'); if(!select) return;
    const trigger=select.querySelector('[role="combobox"]');
    if(trigger.disabled || trigger.getAttribute('aria-disabled')==='true') {
      close();
      if(['ArrowDown','ArrowUp','Home','End','Enter',' '].includes(event.key)) event.preventDefault();
      return;
    }
    if(event.key==='Escape') {close();select.querySelector('[role="combobox"]').focus();event.preventDefault();}
    if(['ArrowDown','ArrowUp','Home','End'].includes(event.key)) {
      event.preventDefault(); const list=select.querySelector('[role="listbox"]'); list.hidden=false;
      select.querySelector('[role="combobox"]').setAttribute('aria-expanded','true');
      const options=[...list.querySelectorAll('button')]; const current=options.indexOf(document.activeElement);
      const index=event.key==='Home'?0:event.key==='End'?options.length-1:(current+(event.key==='ArrowDown'?1:-1)+options.length)%options.length;
      options[index]?.focus();
    }
    if(event.key==='Tab') close();
  };
  const outside=event=>{if(!event.target.closest('.setting-select'))close();};
  root.addEventListener('click',click);root.addEventListener('keydown',key);document.addEventListener('pointerdown',outside);
  return ()=>{root.removeEventListener('click',click);root.removeEventListener('keydown',key);document.removeEventListener('pointerdown',outside);};
}
