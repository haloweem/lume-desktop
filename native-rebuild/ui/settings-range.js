import {escapeHTML as h} from './dom.js';

const integer=(value,min,max)=>Number.isInteger(value)&&value>=min&&value<=max;
export function settingsRange(id,label,value,min,max,unit,disabled,scope,key){
  const current=integer(value,min,max)?value:Math.min(max,Math.max(min,100));
  const marker=scope==='dock'?`data-dock-range="${id}"`:scope==='organizer'?'data-organizer-range':'';
  return `<div class="setting-range" data-setting-range="${id}"><div class="setting-range-head"><label for="${id}">${h(label)}</label><output id="${id}-value" for="${id} ${id}-number">${current}${h(unit)}</output></div><div class="setting-range-controls"><input id="${id}" type="range" ${marker} data-setting-slider data-numeric-scope="${scope}" data-numeric-key="${key}" data-range-unit="${h(unit)}" data-confirmed="${current}" min="${min}" max="${max}" step="1" value="${current}" aria-label="${h(label)}" aria-valuetext="${current}${h(unit)}" style="--range-progress:${(current-min)/(max-min)*100}%" ${disabled?'disabled':''}><div class="setting-range-entry"><input class="setting-number" id="${id}-number" type="number" data-setting-number="${id}" min="${min}" max="${max}" step="1" value="${current}" aria-label="${h(label)}${h(unit)}" ${disabled?'disabled':''}><span aria-hidden="true">${h(unit.trim())}</span></div></div><div class="setting-range-scale" aria-hidden="true"><span>${min}${h(unit)}</span><span>${max}${h(unit)}</span></div></div>`;
}
function preview(slider,value,includeNumber=true){
  const group=slider.closest('[data-setting-range]'),unit=slider.dataset.rangeUnit||'';
  slider.value=String(value);slider.setAttribute('aria-valuetext',`${value}${unit}`);
  slider.style.setProperty('--range-progress',`${(value-Number(slider.min))/(Number(slider.max)-Number(slider.min))*100}%`);
  const output=group.querySelector('output');if(output)output.textContent=`${value}${unit}`;
  const number=group.querySelector('[data-setting-number]');if(number&&includeNumber)number.value=String(value);
}
export function syncSettingsRange(root,id,value,min,max,unit,disabled){
  const slider=root.querySelector(`#${id}`);if(!slider)return;
  const current=integer(value,min,max)?value:100;slider.dataset.confirmed=String(current);
  const group=slider.closest('[data-setting-range]'),number=group.querySelector('[data-setting-number]');
  const submitted=Number(slider.dataset.submitted);
  const shown=disabled&&slider.dataset.submitted!==undefined&&integer(submitted,min,max)?submitted:current;
  if(!disabled)delete slider.dataset.submitted;
  const editing=number&&number.ownerDocument.activeElement===number&&number.dataset.editing==='true';
  preview(slider,editing&&integer(number.valueAsNumber,min,max)?number.valueAsNumber:shown,!editing);
  for(const control of [slider,number])if(control){
    control.setAttribute('aria-disabled',String(disabled));control.disabled=disabled&&control.ownerDocument.activeElement!==control;
    if(control.type==='number')control.readOnly=disabled;
  }
}
// One delegated controller covers every settings slider and its numeric input.
export function mountSettingsRanges(root,onChange){
  const sliderFor=target=>target.matches('[data-setting-slider]')?target:
    target.matches('[data-setting-number]')?root.querySelector(`#${target.dataset.settingNumber}`):null;
  const busy=slider=>slider.disabled||slider.getAttribute('aria-disabled')==='true';
  const valid=(slider,value)=>integer(value,Number(slider.min),Number(slider.max));
  const restore=slider=>preview(slider,Number(slider.dataset.confirmed));
  const input=event=>{
    const slider=sliderFor(event.target);if(!slider||busy(slider))return;
    if(event.target!==slider)event.target.dataset.editing='true';
    const value=event.target.valueAsNumber;if(valid(slider,value))preview(slider,value,event.target===slider);
  };
  const commit=target=>{
    const slider=sliderFor(target);if(!slider||busy(slider))return;
    delete target.dataset.editing;
    const value=target.valueAsNumber;
    if(!valid(slider,value)){restore(slider);return;}
    preview(slider,value);
    if(value===Number(slider.dataset.confirmed))return;
    slider.dataset.submitted=String(value);onChange(slider.dataset.numericScope,slider.dataset.numericKey,value);
  };
  const change=event=>commit(event.target);
  const leave=event=>{if(event.target.matches('[data-setting-number]')&&event.target.dataset.editing==='true')commit(event.target);};
  const key=event=>{
    if(!event.target.matches('[data-setting-number]'))return;
    if(event.key==='Enter'){event.preventDefault();commit(event.target);}
    if(event.key==='Escape'){event.preventDefault();const slider=sliderFor(event.target);if(slider)restore(slider);delete event.target.dataset.editing;}
  };
  root.addEventListener('input',input);root.addEventListener('change',change);root.addEventListener('keydown',key);root.addEventListener('focusout',leave);
  return ()=>{root.removeEventListener('input',input);root.removeEventListener('change',change);root.removeEventListener('keydown',key);root.removeEventListener('focusout',leave);};
}
