import {toggle} from './dom.js';
import {settingsFrame,settingsSelect,syncSettingsSelect,syncSettingsControlBusy} from './settings-frame.js';
import {taskbarDisplayedPreferences} from './state.js';
import {t} from './localization.js';

const errors=()=>({
  load:t('任务栏配置读取失败，尚未应用设置','Could not load taskbar settings. Settings have not been applied.'),
  save:t('任务栏设置保存失败，尚未确认应用结果','Could not save taskbar settings. Application is not confirmed.'),
  busy:t('任务栏服务正在处理其他请求，请稍后重试','The taskbar service is busy. Please try again later.'),
  unsupported:t('当前系统不支持此任务栏透明效果','Taskbar transparency is not supported on this system.'),
  'missing-payload':t('任务栏透明组件缺失，请检查安装文件','The taskbar transparency component is missing. Check the installation files.'),
  'payload-integrity':t('任务栏透明组件校验失败，请修复安装文件','Taskbar component verification failed. Repair the installation files.'),
  conflict:t('其他任务栏工具正在占用任务栏，请关闭冲突工具后重试','Another tool is controlling the taskbar. Close it and try again.'),
  discover:t('尚未找到 Windows 任务栏，正在等待桌面就绪','Windows taskbar not found. Waiting for the desktop.'),
  'read-appearance':t('无法读取任务栏原始外观，尚未应用透明效果','Could not read the original taskbar appearance. Transparency has not been applied.'),
  'apply-appearance':t('任务栏透明效果应用失败，请重试','Could not apply taskbar transparency. Please try again.'),
  'read-auto-hide':t('无法读取任务栏自动隐藏状态，请重试','Could not read the taskbar auto-hide state. Please try again.'),
  'apply-auto-hide':t('任务栏自动隐藏设置应用失败，请重试','Could not apply taskbar auto-hide. Please try again.'),
  'read-visibility':t('无法读取任务栏原始显示状态，尚未完全隐藏','Could not read the original taskbar visibility. Complete hiding has not been applied.'),
  'apply-visibility':t('任务栏完全隐藏失败，请重试','Could not completely hide the taskbar. Please try again.'),
  'core-start':t('任务栏透明服务启动失败，请检查安装文件','Could not start the taskbar transparency service. Check the installation files.'),
  'core-exited':t('任务栏透明服务意外退出，效果尚未恢复','The taskbar transparency service exited unexpectedly. Appearance has not been restored.'),
  'core-stop':t('任务栏透明服务尚未停止，请再次关闭功能重试','The taskbar transparency service has not stopped. Try disabling it again.'),
  cancelled:t('任务栏操作已取消，尚未确认应用结果','Taskbar operation cancelled. Application is not confirmed.'),
  recovery:t('原任务栏设置尚未完全恢复，请再次关闭功能重试','Original taskbar settings are not fully restored. Try disabling the feature again.'),
  watch:t('任务栏状态监听失败，请重新启用功能','Could not monitor taskbar state. Enable the feature again.'),
  'system-effects-disabled':t('当前运行模式未允许修改系统任务栏','The current mode does not allow changes to the system taskbar.'),
  runtime:t('任务栏服务启动失败，请重新启用功能','Could not start the taskbar service. Enable the feature again.'),
});
export function taskbarStatus(state) {
  if(state.taskbarStatus==='loading')return t('正在读取任务栏设置...','Loading taskbar settings...');
  if(state.taskbarStatus==='unavailable')return t('桌面宿主未连接，任务栏设置未应用','Desktop host disconnected. Taskbar settings have not been applied.');
  if(state.taskbarStatus==='saving')return t('正在应用任务栏设置...','Applying taskbar settings...');
  const messages=errors();
  let message=messages[state.taskbarError] || '';
  if(!message && state.taskbarLoadStatus==='failed')message=messages.load;
  // "Not fully restored" only applies when the feature is off. While it is on,
  // an unfinished apply is retried automatically; show just its reason.
  if(state.taskbar.pendingRecovery && !state.taskbar.enabled) {
    const recovery=messages.recovery;
    return message && message!==recovery ? `${message}${t('；','; ')}${recovery}` : recovery;
  }
  if(message)return message;
  if(state.taskbar.enabled && !state.taskbar.running)return t('任务栏服务未运行，尚未应用设置，请重新启用','The taskbar service is not running. Settings have not been applied. Enable it again.');
  if(!state.taskbar.enabled && state.taskbar.running)return t('任务栏服务尚未停止，请再次关闭功能重试','The taskbar service has not stopped. Try disabling it again.');
  if(state.taskbarStatus==='applied')return state.taskbar.enabled?t('任务栏设置已应用','Taskbar settings applied'):t('已关闭功能，原任务栏设置已恢复','Feature disabled. Original taskbar settings restored.');
  return '';
}
export function taskbarNotice(state,{previousStatus,hasInlineStatus=false}={}) {
  if(hasInlineStatus)return null;
  if(['error','unavailable'].includes(state.taskbarStatus))return taskbarStatus(state);
  if(state.taskbarStatus==='applied' && previousStatus==='saving')return taskbarStatus(state);
  return null;
}
export function taskbarPage(state,tab='settings') {
  const p=taskbarDisplayedPreferences(state),busy=['saving','loading'].includes(state.taskbarStatus);
  const row=(label,help,action,value)=>`<div class="setting-row"><div><label>${label}</label><p class="setting-help">${help}</p></div>${toggle(value,label,`taskbar-${action}`,busy)}</div>`;
  const body=tab==='settings'?
    row(t('任务栏透明化','Taskbar transparency'),t('控制 Windows 任务栏背景是否透明','Make the Windows taskbar background transparent'),'transparent',p.transparentTaskbar)+
    row(t('自动隐藏任务栏','Automatically hide taskbar'),t('控制 Lume 启用期间 Windows 任务栏是否自动隐藏','Automatically hide the Windows taskbar while Lume controls it'),'autoHide',p.autoHideTaskbar)+
    `<div class="setting-row" data-taskbar-hide-behavior ${p.autoHideTaskbar?'':'hidden'}><div><label>${t('隐藏行为','Hiding behavior')}</label><p class="setting-help">${hideBehaviorHelp(p.hideBehavior)}</p></div>${settingsSelect('taskbar-hide-behavior',t('隐藏行为','Hiding behavior'),p.hideBehavior,hideBehaviorOptions(),busy || !p.autoHideTaskbar)}</div>`+
    `<div class="reset-row"><button class="reset" data-action="taskbar-reset" ${busy?'disabled':''}>${t('恢复默认','Restore defaults')}</button></div>`:
    `<p>${t('自动隐藏：鼠标移到任务栏所在的屏幕边缘时显示。完全隐藏：鼠标移到边缘也不显示任务栏。','Auto-hide reveals the taskbar when the pointer reaches its screen edge. Completely hidden keeps it hidden at the edge as well.')}</p><p>${t('关闭此功能或退出 Lume 时，会恢复此功能修改前的任务栏设置；若恢复未完成，页面会显示实际错误。','Disabling the feature or exiting Lume restores the previous taskbar settings. Incomplete recovery is reported on this page.')}</p><p>${t('恢复默认会开启任务栏透明化、关闭自动隐藏，隐藏行为恢复为自动隐藏，并保留当前功能启用状态。','Restoring defaults turns transparency on and auto-hide off, resets hiding behavior to auto-hide, and keeps the current feature enable state.')}</p>`;
  return settingsFrame({title:t('任务栏透明化','Taskbar transparency'),description:t('让 Windows 10 和 Windows 11 的任务栏保持透明。','Keep the Windows 10 and Windows 11 taskbar transparent.'),
    tab,body,scope:'taskbar',status:taskbarStatus(state),error:state.taskbarStatus==='error',backLabel:t('返回桌面美化','Back to desktop styles')});
}
const hideBehaviorOptions=()=>[['fully-hidden',t('完全隐藏','Completely hidden')],['auto-hide',t('自动隐藏','Auto-hide')]];
const hideBehaviorHelp=behavior=>behavior==='fully-hidden'
  ?t('隐藏后，鼠标移到屏幕边缘也不显示任务栏','Keep the taskbar hidden even when the pointer reaches the screen edge')
  :t('隐藏后，鼠标移到任务栏所在的屏幕边缘时显示','Show the taskbar when the pointer reaches its screen edge');
export function syncTaskbarControls(root,state) {
  const busy=['saving','loading'].includes(state.taskbarStatus),p=taskbarDisplayedPreferences(state);
  for(const [action,value] of [['toggle',p.enabled],['transparent',p.transparentTaskbar],['autoHide',p.autoHideTaskbar]]) {
    root.querySelectorAll(`[data-action="taskbar-${action}"]`).forEach(button=>{
      button.classList.toggle('checked',value);button.setAttribute('aria-checked',String(value));
      button.setAttribute('aria-busy',String(state.taskbarStatus==='saving'));
      syncSettingsControlBusy(button,busy);
    });
  }
  const behaviorRow=root.querySelector('[data-taskbar-hide-behavior]');
  if(behaviorRow) {
    behaviorRow.hidden=!p.autoHideTaskbar;
    behaviorRow.querySelector('.setting-help').textContent=hideBehaviorHelp(p.hideBehavior);
    syncSettingsSelect(root,'taskbar-hide-behavior',p.hideBehavior,hideBehaviorOptions(),busy || !p.autoHideTaskbar,true);
  }
  const reset=root.querySelector('[data-action="taskbar-reset"]');if(reset)syncSettingsControlBusy(reset,busy);
  const status=root.querySelector('[data-settings-scope="taskbar"] .status');
  if(status) {
    status.textContent=taskbarStatus(state);status.hidden=!status.textContent;
    status.setAttribute('role',state.taskbarStatus==='error'?'alert':'status');
    status.classList.toggle('error',state.taskbarStatus==='error');
  }
}
