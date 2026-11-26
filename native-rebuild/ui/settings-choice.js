// Native controls retain their value/change contract; only the chooser is shared.
// Existing music/wallpaper proxies and settings-frame comboboxes own their UI.
const selector='select,input[type="time"]';
const excluded='.MusicModeProxy,.wallpaper-select-native,[aria-hidden="true"]';
const two=value=>String(value).padStart(2,'0');
let nextId=0;

export function mountSettingsChoice(root) {
  const doc=root.ownerDocument,view=doc.defaultView,entries=new Map();
  let active=null,disposed=false,queued=false;
  const english=source=>(source.closest('[lang]')?.lang||doc.documentElement.lang||'zh-CN').startsWith('en');
  const words=source=>english(source)?{hour:'Hour',minute:'Minute',apply:'Done',cancel:'Cancel',time:'Choose time'}:
    {hour:'小时',minute:'分钟',apply:'完成',cancel:'取消',time:'选择时间'};
  const labelOf=source=>source.getAttribute('aria-label')||source.labels?.[0]?.querySelector('.application-settings__label,.tb-label,span')?.textContent?.trim()||source.title||'';
  const unavailable=source=>!!(source.disabled||source.getAttribute('aria-disabled')==='true'||source.readOnly);
  const visible=source=>source.isConnected&&root.contains(source)&&!source.closest('[hidden],[inert]');
  const setText=(node,value)=>{if(node.textContent!==value)node.textContent=value;};
  function close(restore=false) {
    if(!active)return;
    const {entry,popup}=active;active=null;
    entry.trigger.setAttribute('aria-expanded','false');popup.remove();
    if(restore&&visible(entry.source))entry.trigger.focus({preventScroll:true});
  }
  function sync(entry) {
    const {source,trigger,value}=entry;
    const text=entry.time?(source.value||'--:--'):(source.selectedOptions[0]?.label||'');
    setText(value,text);trigger.setAttribute('aria-label',labelOf(source)||text);
    const busy=unavailable(source);trigger.setAttribute('aria-disabled',String(busy));
    // A request may start synchronously during change. Preserve current focus.
    trigger.disabled=busy&&doc.activeElement!==trigger;
    entry.wrapper.classList.toggle('is-disabled',!!busy);
    if(active?.entry===entry&&(busy||!visible(source)||active.value!==source.value))close();
  }
  function enhance(source) {
    if(entries.has(source)||source.matches(excluded)||source.tagName==='SELECT'&&(source.multiple||source.size>1))return;
    const wrapper=doc.createElement('span'),trigger=doc.createElement('button'),value=doc.createElement('span');
    const id='settings-choice-'+(++nextId),time=source.type==='time';
    wrapper.className='settings-choice';wrapper.dataset.settingsChoice=time?'time':'select';
    trigger.type='button';trigger.className='settings-choice__trigger';trigger.dataset.settingsChoiceTrigger='';
    trigger.id=id;trigger.setAttribute('role','combobox');trigger.setAttribute('aria-haspopup',time?'dialog':'listbox');
    trigger.setAttribute('aria-expanded','false');trigger.setAttribute('aria-controls',id+'-popup');
    value.className='settings-choice__value';
    trigger.append(value);trigger.insertAdjacentHTML('beforeend','<svg viewBox="0 0 16 16" aria-hidden="true"><path d="m4 6 4 4 4-4" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/></svg>');
    const entry={source,wrapper,trigger,value,time,tabindex:source.getAttribute('tabindex'),ariaHidden:source.getAttribute('aria-hidden')};
    entries.set(source,entry);source.before(wrapper);wrapper.append(source,trigger);
    source.classList.add('settings-choice-source');source.dataset.settingsChoiceSource='';
    source.setAttribute('aria-hidden','true');source.setAttribute('tabindex','-1');sync(entry);
  }
  function restore(entry) {
    const {source,wrapper,tabindex,ariaHidden}=entry;
    source.classList.remove('settings-choice-source');delete source.dataset.settingsChoiceSource;
    if(tabindex===null)source.removeAttribute('tabindex');else source.setAttribute('tabindex',tabindex);
    if(ariaHidden===null)source.removeAttribute('aria-hidden');else source.setAttribute('aria-hidden',ariaHidden);
    if(source.parentElement===wrapper){wrapper.before(source);wrapper.remove();}
  }
  function refresh() {
    if(disposed)return;
    for(const [source,entry] of entries) {
      if(!root.contains(source)){if(active?.entry===entry)close();restore(entry);entries.delete(source);}
    }
    root.querySelectorAll(selector).forEach(enhance);
    entries.forEach(sync);
  }
  function schedule() {
    if(queued||disposed)return;queued=true;
    queueMicrotask(()=>{queued=false;refresh();});
  }
  function optionButton(value,label,selected,attribute) {
    const button=doc.createElement('button');button.type='button';button.className='settings-choice__option';
    button.setAttribute('role','option');button.setAttribute('aria-selected',String(selected));
    button.setAttribute(attribute,value);button.tabIndex=selected?0:-1;
    const text=doc.createElement('span'),mark=doc.createElement('span');text.textContent=label;
    mark.className='settings-choice__check';mark.setAttribute('aria-hidden','true');mark.textContent='✓';
    button.append(text,mark);return button;
  }
  function fit(popup,trigger) {
    const rect=trigger.getBoundingClientRect(),viewport=view.visualViewport;
    const left=viewport?.offsetLeft||0,top=viewport?.offsetTop||0;
    const width=viewport?.width||view.innerWidth,height=viewport?.height||view.innerHeight,edge=8;
    // getBoundingClientRect includes CSS zoom; fixed style coordinates are in
    // the body's layout units. Native WebView zoom already uses CSS pixels.
    popup.style.width='200px';
    const scale=popup.getBoundingClientRect().width/popup.offsetWidth||1,gap=6*scale;
    popup.style.maxWidth=Math.max(0,width-edge*2)/scale+'px';
    popup.style.width=Math.min(Math.max(rect.width,(popup.classList.contains('settings-choice-popup--time')?248:180)*scale),width-edge*2)/scale+'px';
    const below=top+height-rect.bottom-gap-edge,above=rect.top-top-gap-edge;
    const useAbove=below<Math.min(popup.scrollHeight*scale,240*scale)&&above>below;
    popup.style.maxHeight=Math.max(0,useAbove?above:below)/scale+'px';
    const size=popup.getBoundingClientRect();
    popup.style.left=Math.max(left+edge,Math.min(rect.left,left+width-size.width-edge))/scale+'px';
    popup.style.top=(useAbove?Math.max(top+edge,rect.top-gap-size.height):rect.bottom+gap)/scale+'px';
    if(popup.classList.contains('settings-choice-popup--time'))
      popup.style.setProperty('--choice-list-height',Math.max(34,Math.min(196,(size.height/scale)-91))+'px');
  }
  function chooseFocus(button) {
    if(!button)return;
    button.parentElement.querySelectorAll('[role="option"]').forEach(option=>{option.tabIndex=option===button?0:-1;});
    button.focus({preventScroll:true});
    button.scrollIntoView({block:'nearest',inline:'nearest'});
  }
  function open(entry,last=false) {
    refresh();if(unavailable(entry.source)||!visible(entry.source))return;
    close();
    const {source,trigger,time}=entry,popup=doc.createElement('div'),labels=words(source);
    popup.className='settings-choice-popup'+(time?' settings-choice-popup--time':'')+
      (trigger.closest('.settings--sidebar, .DesktopBeautyLayout')?' settings-choice-popup--desktop-beauty':'');
    popup.id=trigger.getAttribute('aria-controls');popup.dataset.settingsChoicePopup=time?'time':'select';
    popup.setAttribute('role',time?'dialog':'listbox');popup.setAttribute('aria-label',labelOf(source)||labels.time);
    active={entry,popup,value:source.value,draft:/^\d{2}:\d{2}$/.test(source.value)?source.value:'00:00'};
    if(time) {
      const columns=doc.createElement('div');columns.className='settings-choice__columns';
      for(const [key,count,label,index] of [['hour',24,labels.hour,0],['minute',60,labels.minute,1]]) {
        const column=doc.createElement('div'),heading=doc.createElement('div'),list=doc.createElement('div');
        column.className='settings-choice__column';heading.className='settings-choice__heading';heading.textContent=label;
        list.className='settings-choice__time-list';list.setAttribute('role','listbox');list.setAttribute('aria-label',label);
        list.dataset.settingsChoiceColumn=key;
        for(let i=0;i<count;i++)list.append(optionButton(two(i),two(i),two(i)===active.draft.split(':')[index],'data-settings-choice-'+key));
        column.append(heading,list);columns.append(column);
      }
      const actions=doc.createElement('div');actions.className='settings-choice__actions';
      for(const key of ['cancel','apply']){const button=doc.createElement('button');button.type='button';button.setAttribute('data-settings-choice-'+key,'');button.textContent=labels[key];actions.append(button);}
      popup.append(columns,actions);
    } else {
      for(const option of source.options) {
        const button=optionButton(option.value,option.label,option.selected,'data-settings-choice-option');
        button.dataset.settingsChoiceValue=option.value;button.disabled=option.disabled||option.parentElement?.disabled;
        if(option.hidden)button.hidden=true;popup.append(button);
      }
    }
    doc.body.append(popup);trigger.setAttribute('aria-expanded','true');fit(popup,trigger);
    popup.querySelectorAll('.settings-choice__time-list').forEach(list=>list.querySelector('[aria-selected="true"]')?.scrollIntoView({block:'center'}));
    const buttons=[...popup.querySelectorAll('[role="option"]:not(:disabled):not([hidden])')];
    chooseFocus(last?buttons.at(-1):(buttons.find(button=>button.getAttribute('aria-selected')==='true')||buttons[0]));
  }
  function commit(entry,value) {
    if(unavailable(entry.source)){close();return;}
    close(true);if(entry.source.value===value)return;
    entry.source.value=value;
    entry.source.dispatchEvent(new view.Event('input',{bubbles:true}));
    entry.source.dispatchEvent(new view.Event('change',{bubbles:true}));
    refresh();
  }
  function click(event) {
    const trigger=event.target.closest('[data-settings-choice-trigger]');
    if(trigger&&root.contains(trigger)) {
      event.preventDefault();const entry=[...entries.values()].find(item=>item.trigger===trigger);
      if(entry){if(active?.entry===entry)close(true);else open(entry);}return;
    }
    if(!active||!active.popup.contains(event.target))return;
    const option=event.target.closest('[data-settings-choice-option]');
    if(option&&!option.disabled){commit(active.entry,option.dataset.settingsChoiceOption);return;}
    const part=event.target.closest('[data-settings-choice-hour],[data-settings-choice-minute]');
    if(part) {
      const hour=part.hasAttribute('data-settings-choice-hour'),pieces=active.draft.split(':');
      pieces[hour?0:1]=hour?part.dataset.settingsChoiceHour:part.dataset.settingsChoiceMinute;active.draft=pieces.join(':');
      part.parentElement.querySelectorAll('[role="option"]').forEach(button=>{
        const selected=button===part;button.setAttribute('aria-selected',String(selected));button.tabIndex=selected?0:-1;
      });return;
    }
    if(event.target.closest('[data-settings-choice-apply]'))commit(active.entry,active.draft);
    else if(event.target.closest('[data-settings-choice-cancel]'))close(true);
  }
  function keydown(event) {
    const trigger=event.target.closest('[data-settings-choice-trigger]');
    if(trigger&&root.contains(trigger)&&['ArrowDown','ArrowUp','Home','End'].includes(event.key)) {
      event.preventDefault();open([...entries.values()].find(item=>item.trigger===trigger),event.key==='End');return;
    }
    if(!active||(!active.popup.contains(event.target)&&trigger!==active.entry.trigger))return;
    if(event.key==='Escape'){event.preventDefault();event.stopPropagation();close(true);return;}
    if(event.key==='Tab') {
      if(active.entry.time&&active.popup.contains(event.target)) {
        const stops=[...active.popup.querySelectorAll('button')].filter(button=>button.tabIndex>=0&&!button.disabled);
        const index=stops.indexOf(doc.activeElement);
        if((event.shiftKey&&index>0)||(!event.shiftKey&&index<stops.length-1))return;
      }
      // At the popup boundary, continue from the page's original trigger.
      close(true);return;
    }
    const list=event.target.closest('[role="listbox"]');
    if(list&&['ArrowDown','ArrowUp','Home','End'].includes(event.key)) {
      event.preventDefault();const options=[...list.querySelectorAll('[role="option"]:not(:disabled):not([hidden])')];
      const index=options.indexOf(doc.activeElement),next=event.key==='Home'?0:event.key==='End'?options.length-1:
        (index+(event.key==='ArrowDown'?1:-1)+options.length)%options.length;
      chooseFocus(options[next]);
    } else if(active.entry.time&&['ArrowLeft','ArrowRight'].includes(event.key)) {
      event.preventDefault();const key=event.key==='ArrowLeft'?'hour':'minute';
      chooseFocus(active.popup.querySelector('[data-settings-choice-column="'+key+'"] [aria-selected="true"]'));
    }
  }
  const outside=event=>{if(active&&!active.popup.contains(event.target)&&!active.entry.wrapper.contains(event.target))close();};
  const scrolling=event=>{if(active&&!active.popup.contains(event.target))close();};
  const focus=event=>{const entry=entries.get(event.target);if(entry)entry.trigger.focus({preventScroll:true});};
  const changed=event=>{if(entries.has(event.target))schedule();};
  const resize=()=>close();
  refresh();
  const observer=new view.MutationObserver(records=>{
    if(records.some(record=>{
      const target=record.target.nodeType===1?record.target:record.target.parentElement;
      if(target?.closest('[data-settings-choice-trigger]'))return false;
      if(record.type==='attributes')return entries.has(target)||target?.matches('option,optgroup')||
        (record.attributeName==='hidden'&&active&&target?.contains(active.entry.source));
      if(target?.matches('select,option,optgroup'))return true;
      return [...record.addedNodes,...record.removedNodes].some(node=>node.nodeType===1&&
        (node.matches(selector)||node.querySelector(selector)));
    }))schedule();
  });
  observer.observe(root,{subtree:true,childList:true,characterData:true,attributes:true,
    attributeFilter:['disabled','aria-disabled','readonly','value','selected','label','aria-label','hidden','lang']});
  doc.addEventListener('click',click);doc.addEventListener('keydown',keydown,true);doc.addEventListener('pointerdown',outside);
  root.addEventListener('focusin',focus);root.addEventListener('input',changed);root.addEventListener('change',changed);
  doc.addEventListener('scroll',scrolling,true);view.addEventListener('resize',resize);
  view.visualViewport?.addEventListener('resize',resize);view.visualViewport?.addEventListener('scroll',resize);
  const unmount=()=>{
    if(disposed)return;disposed=true;close();observer.disconnect();entries.forEach(restore);entries.clear();
    doc.removeEventListener('click',click);doc.removeEventListener('keydown',keydown,true);doc.removeEventListener('pointerdown',outside);
    root.removeEventListener('focusin',focus);root.removeEventListener('input',changed);root.removeEventListener('change',changed);
    doc.removeEventListener('scroll',scrolling,true);view.removeEventListener('resize',resize);
    view.visualViewport?.removeEventListener('resize',resize);view.visualViewport?.removeEventListener('scroll',resize);
  };
  unmount.refresh=refresh;return unmount;
}
