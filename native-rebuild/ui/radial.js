import {escapeHTML as h} from './dom.js';
import {settingsFrame,settingsSelect,syncSettingsSelect,syncSettingsControlBusy} from './settings-frame.js';
import {t} from './localization.js';

const materials=()=>[['liquid-glass',t('液态玻璃','Liquid glass')],['frosted-glass',t('磨砂玻璃','Frosted glass')]];
const busy=state=>['loading','saving'].includes(state.radialStatus);
const screenshotSlot=6;

export function validRadialShortcut(code) {
  if(!Number.isSafeInteger(code)||code<1||code>4095)return false;
  const vk=code%256,mods=Math.floor(code/256);
  const key=[8,9,13,19,20,32,44,45,46,144,145,226].includes(vk)||
    (vk>=33&&vk<=40)||(vk>=48&&vk<=57)||(vk>=65&&vk<=90)||
    (vk>=96&&vk<=135)||(vk>=186&&vk<=192)||(vk>=219&&vk<=222);
  return key&&!((vk===9&&(mods&9))||(vk===46&&(mods&3)===3)||(vk===76&&(mods&8)));
}

export function validRadialState(message) {
  const keys=['version','type','enabled','running','visible','material','shortcutCode',
    'materialStatus','loadStatus','error','result','saved'];
  return !!message&&typeof message==='object'&&!Array.isArray(message)&&
    keys.every(key=>Object.hasOwn(message,key))&&Object.keys(message).every(key=>keys.includes(key))&&
    message.version===1&&message.type==='radial-state'&&
    ['enabled','running','visible','saved'].every(key=>typeof message[key]==='boolean')&&
    ['liquid-glass','frosted-glass'].includes(message.material)&&validRadialShortcut(message.shortcutCode)&&
    ['inactive','native-ready','native-starting','transparent-fallback'].includes(message.materialStatus)&&
    ['fresh','loaded','recovered','failed'].includes(message.loadStatus)&&
    ['','runtime','save','rollback'].includes(message.error)&&['snapshot','applied'].includes(message.result)&&
    (!message.saved||(message.result==='applied'&&message.error===''&&message.loadStatus==='loaded'));
}

const stableKeyCodes={Backspace:8,Tab:9,Enter:13,NumpadEnter:13,Pause:19,CapsLock:20,Space:32,
  PageUp:33,PageDown:34,End:35,Home:36,ArrowLeft:37,ArrowUp:38,ArrowRight:39,ArrowDown:40,
  PrintScreen:44,Insert:45,Delete:46,NumLock:144,ScrollLock:145,
  NumpadMultiply:106,NumpadAdd:107,NumpadComma:108,NumpadSubtract:109,NumpadDecimal:110,NumpadDivide:111};
const asciiKeyCodes={' ':32,';':186,':':186,'=':187,'+':187,',':188,'<':188,'-':189,'_':189,
  '.':190,'>':190,'/':191,'?':191,'`':192,'~':192,'[':219,'{':219,'\\':220,'|':220,
  ']':221,'}':221,"'":222,'"':222};

function virtualKey(event) {
  // WebView keyCode carries the layout-resolved Windows VK; code is a physical position.
  if(Number.isInteger(event.keyCode)&&event.keyCode>0&&event.keyCode<=255)return event.keyCode;
  if(typeof event.key==='string'&&/^[a-z0-9]$/i.test(event.key))return event.key.toUpperCase().charCodeAt(0);
  if(Object.hasOwn(asciiKeyCodes,event.key))return asciiKeyCodes[event.key];
  if(Object.hasOwn(stableKeyCodes,event.key))return stableKeyCodes[event.key];
  if(Object.hasOwn(stableKeyCodes,event.code))return stableKeyCodes[event.code];
  if(/^Numpad[0-9]$/.test(event.code))return 96+Number(event.code.at(-1));
  if(/^F(?:[1-9]|1[0-9]|2[0-4])$/.test(event.code))return 111+Number(event.code.slice(1));
  return null;
}

const invalidKeyEvent=event=>event.isComposing||[229,231].includes(event.keyCode)||
  ['Escape','Process','Unidentified','Dead'].includes(event.key);
export function shortcutFromEvent(event) {
  if(invalidKeyEvent(event)||event.repeat)return null;
  const vk=virtualKey(event);
  if(vk===null)return null;
  const mods=(event.altKey?1:0)+(event.ctrlKey?2:0)+(event.shiftKey?4:0)+(event.metaKey?8:0);
  const code=vk+mods*256;
  return validRadialShortcut(code)?code:null;
}

export function shortcutLabel(code) {
  const vk=code%256,mods=Math.floor(code/256);
  const labels={8:'Backspace',9:'Tab',13:'Enter',19:'Pause',20:'Caps Lock',32:t('空格','Space'),
    33:'Page Up',34:'Page Down',35:'End',36:'Home',37:'Left',38:'Up',39:'Right',40:'Down',
    44:'Print Screen',45:'Insert',46:'Delete',144:'Num Lock',145:'Scroll Lock',
    106:'Num *',107:'Num +',108:'Num ,',109:'Num -',110:'Num .',111:'Num /',
    186:';',187:'=',188:',',189:'-',190:'.',191:'/',192:'`',219:'[',220:'\\',221:']',222:"'",226:'\\'};
  const key=labels[vk]||((vk>=48&&vk<=57)||(vk>=65&&vk<=90)?String.fromCharCode(vk):
    vk>=96&&vk<=105?`Num ${vk-96}`:vk>=112&&vk<=135?`F${vk-111}`:'');
  return [mods&2?'Ctrl':'',mods&1?'Alt':'',mods&4?'Shift':'',mods&8?'Win':'',key].filter(Boolean).join(' + ');
}

export function radialStatus(state) {
  if(state.radialStatus==='loading')return t('正在读取设置...','Loading settings...');
  if(state.radialStatus==='saving')return t('正在保存...','Saving...');
  if(state.radialStatus==='unavailable')return t('桌面宿主未连接，轮盘设置未保存','Desktop host disconnected. Radial settings were not saved.');
  if(state.radialStatus==='error')return {
    runtime:t('轮盘菜单启动失败，请检查安装文件','Could not start the radial menu. Check the installation files.'),
    save:t('轮盘设置保存失败，请重试','Could not save radial settings. Please try again.'),
    rollback:t('轮盘菜单恢复失败，请重新启用','Could not restore the radial menu. Enable it again.'),
  }[state.radialError]||t('轮盘配置读取失败，当前使用默认设置；尚未保存','Could not load radial settings. Using defaults; not yet saved.');
  if(state.radialLoadStatus==='recovered')return t('已从备份恢复轮盘配置，尚未保存到主配置','Radial settings restored from backup; not yet saved to the main configuration.');
  if(state.radialMaterialStatus==='transparent-fallback')return t('材质暂不可用，当前为透明显示','Material unavailable. Using a transparent fallback.');
  if(state.radialMaterialStatus==='native-starting')return t('正在准备轮盘材质...','Preparing the radial material...');
  return '';
}

export function radialNotice(state,{previousStatus,hasInlineStatus=false}={}) {
  if(hasInlineStatus)return null;
  if(['error','unavailable','recovered'].includes(state.radialStatus))return radialStatus(state);
  if(state.radialStatus==='saved'&&previousStatus==='saving')return t('轮盘设置已应用并保存','Radial settings applied and saved');
  return null;
}

export function radialPage(state,tab='settings') {
  const title=t('轮盘菜单','Radial menu');
  const description=t('快速使用截图等桌面工具','Quick access to screenshot and desktop tools');
  const slots=Array.from({length:10},(_,index)=>{
    const available=index===screenshotSlot,placeholder=!available&&index!==1;
    const label=available?t('截图','Screenshot'):index===1?t('翻译','Translation'):t('待添加','Empty');
    const status=available?t('可用','Available'):index===1?t('暂不可用','Unavailable'):'--';
    return `<li class="radial-slot${available?' active':''}" data-radial-slot="${index}"${available?' data-radial-available="true"':' aria-disabled="true"'}${placeholder?' data-radial-placeholder':''}><span class="radial-slot-index" aria-hidden="true">${index+1}</span><span class="radial-slot-label">${h(label)}</span><span class="radial-slot-status">${h(status)}</span></li>`;
  }).join('');
  const disabled=busy(state),recordLabel=t('录制快捷键','Record shortcut'),resetLabel=t('恢复默认','Restore defaults');
  const body=tab==='settings'?`<div class="setting-row"><label>${t('菜单项目','Menu slots')}</label><ol class="radial-slots" aria-label="${t('轮盘菜单项目','Radial menu slots')}">${slots}</ol></div>`+
    `<div class="setting-row"><label for="radial-material">${t('材质设置','Material')}</label>${settingsSelect('radial-material',t('材质设置','Material'),state.radial.material,materials(),disabled)}</div>`+
    `<div class="setting-row"><label for="radial-shortcut">${t('快捷键','Shortcut')}</label><button type="button" id="radial-shortcut" class="radial-shortcut" data-radial-shortcut title="${h(recordLabel)}" aria-label="${h(recordLabel)}" aria-pressed="false" aria-disabled="${disabled}" ${disabled?'disabled':''}>${h(shortcutLabel(state.radial.shortcutCode))}</button><span class="radial-recorder-status" role="status" data-radial-recorder-status hidden></span></div>`+
    `<div class="reset-row"><button type="button" class="reset" data-action="radial-reset" title="${h(resetLabel)}" ${disabled?'disabled':''}>${resetLabel}</button></div>`:
    `<div class="settings-details"><h3>${t('功能说明','About this menu')}</h3><p>${description}</p></div>`;
  return settingsFrame({title,description,tab,body,status:radialStatus(state),error:state.radialStatus==='error',
    scope:'radial',backLabel:t('返回桌面美化','Back to desktop styles')});
}

export function syncRadialControls(root,state) {
  const disabled=busy(state);
  root.querySelectorAll('[data-action="radial-toggle"]').forEach(button=>{
    button.classList.toggle('checked',state.radial.enabled);
    button.setAttribute('aria-checked',String(state.radial.enabled));button.disabled=disabled;
    button.title=t('轮盘菜单','Radial menu');
  });
  syncSettingsSelect(root,'radial-material',state.radial.material,materials(),disabled,true);
  const material=root.querySelector('#radial-material');
  if(material)material.title=t('材质设置','Material');
  const recorder=root.querySelector('[data-radial-shortcut]');
  if(recorder) {
    syncSettingsControlBusy(recorder,disabled);
    if(recorder.getAttribute('aria-pressed')!=='true')recorder.textContent=shortcutLabel(state.radial.shortcutCode);
  }
  const reset=root.querySelector('[data-action="radial-reset"]');if(reset)syncSettingsControlBusy(reset,disabled);
  const status=root.querySelector('[data-settings-scope="radial"] .status');
  if(status) {
    status.textContent=radialStatus(state);status.hidden=!status.textContent;
    status.setAttribute('role',state.radialStatus==='error'?'alert':'status');
    status.classList.toggle('error',state.radialStatus==='error');
  }
  root.querySelectorAll('[data-settings-scope="radial"] button').forEach(button=>{
    if(!button.title)button.title=button.getAttribute('aria-label')||button.textContent.trim();
  });
}

// Capture is confined to the focused recorder, never the WebView/document key path.
export function mountRadialShortcut(root,{getState,onShortcut,onCaptureChange}) {
  let active=null,candidate=null,pendingStart=false;
  const feedback=text=>{
    const status=root.querySelector('[data-radial-recorder-status]');
    if(status){status.textContent=text;status.hidden=!text;}
  };
  const cancel=()=>{
    const wasActive=active!==null;
    if(active) {
      active.setAttribute('aria-pressed','false');
      active.textContent=shortcutLabel(getState().radial.shortcutCode);
    }
    active=null;candidate=null;pendingStart=false;feedback('');
    if(wasActive)onCaptureChange?.(false);
  };
  const start=({waitForState=false}={})=>{
    cancel();const recorder=root.querySelector('[data-radial-shortcut]');
    if(!recorder)return false;
    if(busy(getState())){pendingStart=waitForState;return false;}
    recorder.focus();
    if(root.ownerDocument.activeElement!==recorder)return false;
    active=recorder;recorder.setAttribute('aria-pressed','true');
    recorder.textContent=t('录制中...','Recording...');
    feedback('');onCaptureChange?.(true);return true;
  };
  const click=event=>{
    if(!event.target.closest('[data-radial-shortcut]'))return;
    if(active)cancel();else start();
  };
  const keydown=event=>{
    if(!active||event.target!==active||root.ownerDocument.activeElement!==active)return;
    if(busy(getState())){cancel();return;}
    event.preventDefault();event.stopPropagation();
    if(event.key==='Escape'){cancel();return;}
    if(event.repeat)return;
    if(['Alt','Control','Shift','Meta'].includes(event.key))return;
    const code=shortcutFromEvent(event);
    candidate=code===null?null:{key:event.code,code};
    feedback(code===null?t('此快捷键不可用','This shortcut is unavailable'): '');
    active.textContent=code===null?t('录制中...','Recording...'):shortcutLabel(code);
  };
  const keyup=event=>{
    if(!active||event.target!==active||root.ownerDocument.activeElement!==active)return;
    event.preventDefault();event.stopPropagation();
    if(invalidKeyEvent(event)){cancel();return;}
    if(!candidate||candidate.key!==event.code||busy(getState()))return;
    const code=candidate.code;cancel();onShortcut(code);
  };
  const blur=event=>{if(event.target===active)cancel();};
  const composition=event=>{if(event.target===active)cancel();};
  root.addEventListener('click',click);root.addEventListener('keydown',keydown);
  root.addEventListener('keyup',keyup);root.addEventListener('blur',blur,true);
  root.addEventListener('compositionstart',composition);
  root.ownerDocument.defaultView.addEventListener('blur',cancel);
  return {start,cancel,sync(){if(pendingStart&&!busy(getState()))start();},
    dispose(){cancel();root.removeEventListener('click',click);
      root.removeEventListener('keydown',keydown);root.removeEventListener('keyup',keyup);
      root.removeEventListener('blur',blur,true);root.removeEventListener('compositionstart',composition);
      root.ownerDocument.defaultView.removeEventListener('blur',cancel);}};
}
