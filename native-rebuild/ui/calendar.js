import {escapeHTML as h} from './dom.js';
import {settingsFrame,settingsSelect,syncSettingsSelect} from './settings-frame.js';
import {t} from './localization.js';

const themes=()=>[['spacious',t('经典','Classic')],['split',t('舒展','Spacious')],['todo',t('待办','Todo')]];
const materials=()=>[['solid',t('普通','Standard')],['liquid-glass',t('液态玻璃','Liquid glass')],['frosted-glass',t('磨砂玻璃','Frosted glass')]];
const stageNote=state=>t('日程仅保存在本机。Lume 运行时会按设置弹出提醒，关闭日历仍可提醒；从托盘退出 Lume 后不再提醒。日程编辑表单和提醒窗当前使用纯色显示。','Events are stored only on this device. Reminders work while Lume is running, even with the calendar closed. Exiting Lume from the tray stops reminders. Event forms and reminder windows currently use solid colors. ')+({
  'native-ready':t('日历原生玻璃材质已启用。','Native calendar glass is enabled.'),'native-starting':t('正在准备日历材质。','Preparing the calendar material.'),
  'transparent-fallback':t('日历材质暂不可用，当前为透明降级显示。','Calendar material is unavailable. Using a transparent fallback.'),inactive:t('启用日历后加载材质。','The material loads when the calendar is enabled.'),
}[state.calendar?.material==='solid'?'':state.calendarMaterialStatus]||'');
export function calendarStatus(state) {
  if(state.calendarStatus === 'error') return {
    invalid:t('日历设置无效，未更改此前设置','Invalid calendar settings. Previous settings were not changed.'),runtime:t('桌面日历启动失败，请检查安装文件','Could not start the desktop calendar. Check the installation files.'),
    save:t('日历保存失败，已恢复此前设置，请重试','Could not save the calendar. Previous settings restored. Please try again.'),rollback:t('日历恢复失败，请重新启用组件','Could not restore the calendar. Enable the widget again.'),
    load:t('日历配置读取失败，当前使用默认设置；尚未保存','Could not load calendar settings. Using defaults; not yet saved.'),
  }[state.calendarError] || t('日历配置读取失败，当前使用默认设置；尚未保存','Could not load calendar settings. Using defaults; not yet saved.');
  if(state.calendarStatus === 'saving') return t('正在保存...','Saving...');
  if(state.calendarStatus === 'loading') return t('正在读取设置...','Loading settings...');
  if(state.calendarStatus === 'unavailable') return t('桌面宿主未连接，日历设置未保存','Desktop host disconnected. Calendar settings were not saved.');
  if(state.calendarLoadStatus === 'recovered-backup') return t('已从备份恢复日历配置，尚未保存到主配置','Calendar settings restored from backup; not yet saved to the main configuration.');
  if(state.reminderAvailable === false) return t('提醒暂不可用，请检查本地数据目录；待处理提醒会保留并重试','Reminders are unavailable. Check the local data folder. Pending reminders will be retained and retried.');
  return '';
}
export function calendarNotice(state,{previousStatus,hasInlineStatus=false}={}) {
  if(hasInlineStatus) return null;
  if(['error','unavailable','recovered'].includes(state.calendarStatus)) return calendarStatus(state);
  if(state.calendarStatus === 'saved' && previousStatus === 'saving') return t('日历已应用并保存','Calendar applied and saved');
  return null;
}
function colorControl(key,label,color,disabled) {
  return `<div class="setting-row"><label for="calendar-${key}">${label}</label><button type="button" id="calendar-${key}" class="calendar-color-control" data-action="calendar-${key}" aria-label="${label}" ${disabled?'disabled':''}><span class="calendar-color-value">${h(color.toUpperCase())}</span><span class="calendar-color-swatch" aria-hidden="true" style="background-color:${h(color)}"></span></button></div>`;
}
export function calendarPage(state,tab='settings') {
  const description=t('在桌面显示月视图日历，并高亮今日日期。','Display a monthly calendar on your desktop with today highlighted.');
  const p=state.calendar,disabled=state.calendarStatus==='saving';
  const note=`<p class="calendar-stage-note">${h(stageNote(state))}</p>`;
  const body=tab==='settings' ?
    `<div class="setting-row"><label for="calendar-theme">${t('日历主题','Calendar theme')}</label>${settingsSelect('calendar-theme',t('日历主题','Calendar theme'),p.theme,themes(),disabled)}</div>`+
    `<div class="setting-row"><label for="calendar-material">${t('材质设置','Material')}</label>${settingsSelect('calendar-material',t('材质设置','Material'),p.material,materials(),disabled)}</div>`+
    colorControl('textColor',t('字体颜色','Text color'),p.textColor,disabled)+colorControl('highlightColor',t('高亮颜色','Highlight color'),p.highlightColor,disabled)+
    `<div class="reset-row"><button class="reset" data-action="calendar-reset" ${disabled?'disabled':''}>${t('恢复默认','Restore defaults')}</button></div>${note}` :
    `<div class="settings-details"><h3>${t('功能说明','About this widget')}</h3><p>${description}</p><p>${t('组件可独立启用，桌面位置随拖拽保存。退出 Lume 时，组件会一同关闭。','Enable the widget independently. Its position is saved when dragged. The widget closes when you exit Lume.')}</p>${note}</div>`;
  return settingsFrame({title:t('日历','Calendar'),description,tab,body,status:calendarStatus(state),error:state.calendarStatus==='error',scope:'calendar'});
}
export function syncCalendarControls(root,state) {
  root.querySelectorAll('.calendar-stage-note').forEach(note=>{note.textContent=stageNote(state);});
  const disabled=state.calendarStatus==='saving';
  root.querySelectorAll('[data-action="calendar-toggle"]').forEach(button=>{
    button.classList.toggle('checked',state.calendar.enabled);
    button.setAttribute('aria-checked',String(state.calendar.enabled));button.disabled=disabled;
  });
  syncSettingsSelect(root,'calendar-theme',state.calendar.theme,themes(),disabled);
  syncSettingsSelect(root,'calendar-material',state.calendar.material,materials(),disabled);
  for(const key of ['textColor','highlightColor']) {
    const button=root.querySelector(`[data-action="calendar-${key}"]`);
    if(!button) continue;
    button.disabled=disabled;
    button.querySelector('.calendar-color-value').textContent=state.calendar[key].toUpperCase();
    button.querySelector('.calendar-color-swatch').style.backgroundColor=state.calendar[key];
  }
  const reset=root.querySelector('[data-action="calendar-reset"]');if(reset) reset.disabled=disabled;
  const status=root.querySelector('[data-settings-scope="calendar"] .status');
  if(status) {
    status.textContent=calendarStatus(state);status.hidden=!status.textContent;
    status.setAttribute('role',state.calendarStatus==='error'?'alert':'status');
    status.classList.toggle('error',state.calendarStatus==='error');
  }
}
