import {toggle} from './dom.js';
import {settingsFrame,settingsSelect,syncSettingsSelect,syncSettingsControlBusy} from './settings-frame.js';
import {t} from './localization.js';

const engines=()=>[['modern',t('现代样式','Modern')],['classic',t('经典样式','Classic')]];
const errors=()=>({
  save:t('动画设置保存失败，已保留此前设置','Could not save animation settings. Previous settings retained.'),
  rollback:t('动画设置恢复失败，请关闭功能后重新启用','Could not restore animation settings. Disable and re-enable the feature.'),
  'invalid-settings':t('动画设置无效，尚未应用','Invalid animation settings. Settings have not been applied.'),
  'missing-host':t('动画组件不完整，请检查 32 位和 64 位动画宿主及钩子文件','Animation components are incomplete. Check the 32-bit and 64-bit animation hosts and hook files.'),
  'already-running':t('动画服务已被其他实例占用，请关闭冲突实例后重试','The animation service is in use by another instance. Close it and try again.'),
  ipc:t('动画服务通信失败，请重新启用功能','Could not communicate with the animation service. Enable the feature again.'),
  'host-start':t('动画宿主启动失败，请检查安装文件','Could not start the animation host. Check the installation files.'),
  'hook-install':t('窗口动画钩子安装失败，动画未能启动','Could not install the window animation hooks. Animation did not start.'),
  'stop-timeout':t('动画服务停止超时，请再次关闭功能重试','Timed out stopping the animation service. Try disabling it again.'),
  'host-exited':t('动画宿主意外退出，动画已停止','The animation host exited unexpectedly. Animation has stopped.'),
  'unsupported-architecture':t('当前处理器架构不支持此动画服务','The animation service does not support this processor architecture.'),
  runtime:t('动画服务启动失败，请重新启用功能','Could not start the animation service. Enable the feature again.'),
});
export function minimizeStatus(state) {
  if(state.minimizeStatus==='loading')return t('正在读取动画设置...','Loading animation settings...');
  if(state.minimizeStatus==='unavailable')return t('桌面宿主未连接，动画设置未保存','Desktop host disconnected. Animation settings were not saved.');
  if(state.minimizeStatus==='saving')return t('正在应用动画设置...','Applying animation settings...');
  if(state.minimizeError) {
    const message=errors()[state.minimizeError] || t('动画服务出现错误，尚未确认应用结果','The animation service encountered an error. Application is not confirmed.');
    return state.minimize.systemError ? `${message}${t(`（系统错误：${state.minimize.systemError}）`,` (system error: ${state.minimize.systemError})`)}` : message;
  }
  if(state.minimizeLoadStatus==='failed')return t('动画配置读取失败，当前使用默认设置；尚未保存','Could not load animation settings. Using defaults; not yet saved.');
  if(state.minimizeLoadStatus==='recovered')return t('已从备份恢复动画设置，尚未保存到主配置','Animation settings restored from backup; not yet saved to the main configuration.');
  if(state.minimize.enabled && !state.minimize.running)return t('动画服务未运行，请重新启用功能','The animation service is not running. Enable the feature again.');
  if(!state.minimize.enabled && state.minimize.running)return t('动画服务尚未停止，请再次关闭功能重试','The animation service has not stopped. Try disabling it again.');
  if(state.minimizeStatus==='saved')return state.minimize.enabled?t('动画设置已应用并保存','Animation settings applied and saved'):t('动画设置已保存，功能已关闭','Animation settings saved. Feature disabled.');
  return '';
}
export function minimizeNotice(state,{previousStatus,hasInlineStatus=false}={}) {
  if(hasInlineStatus)return null;
  if(['error','unavailable','recovered'].includes(state.minimizeStatus))return minimizeStatus(state);
  if(state.minimizeStatus==='saved' && previousStatus==='saving')return minimizeStatus(state);
  return null;
}
export function minimizePage(state,tab='settings') {
  const p=state.minimize,busy=['saving','loading'].includes(state.minimizeStatus);
  const row=(label,help,control)=>`<div class="setting-row"><div><label>${label}</label><p class="setting-help">${help}</p></div>${control}</div>`;
  const toggleRow=(label,help,key)=>row(label,help,toggle(p[key],label,`minimize-${key}`,busy));
  const numberRow=(label,help,key,min,max)=>`<div class="setting-row"><div><label for="minimize-${key}">${label}</label><p class="setting-help">${help}</p></div><input class="setting-number" id="minimize-${key}" type="number" min="${min}" max="${max}" step="1" value="${p[key]}" ${busy?'disabled':''}></div>`;
  const body=tab==='settings'?
    numberRow(t('动画时长（毫秒）','Animation duration (ms)'),t('神奇效果动画的持续时间，范围为 50 至 2000 毫秒','Genie effect duration, from 50 to 2000 ms'),'durationMs',50,2000)+
    toggleRow(t('还原窗口时播放动画','Animate window restoration'),t('从任务栏还原窗口时，反向播放神奇效果动画','Reverse the Genie effect when restoring windows from the taskbar'),'restore')+
    toggleRow(t('展开动画','Open animation'),t('软件窗口打开时从 Dock 图标（或任务栏）展开；个别软件的启动画面、对话框也可能播放','New windows open out of their Dock icon (or the taskbar); some splash screens and dialogs may animate too'),'launch')+
    toggleRow(t('多显示器支持（实验性）','Multiple displays (experimental)'),t('在窗口所在显示器上播放动画，并以该显示器的任务栏边缘为目标','Animate on the window display, toward its taskbar edge'),'multiMonitor')+
    toggleRow(t('显示自动隐藏的任务栏','Show auto-hidden taskbar'),t('最小化时短暂显示自动隐藏的任务栏，动画结束后再允许任务栏收回','Briefly reveal the taskbar while minimizing; allow it to hide after the animation'),'unhideTaskbar')+
    numberRow(t('任务栏显示时长（毫秒）','Taskbar reveal duration (ms)'),t('自动隐藏模式下延迟最小化前的任务栏显示时间，范围为 0 至 5000 毫秒','Show the auto-hidden taskbar before minimizing, from 0 to 5000 ms'),'unhideDurationMs',0,5000)+
    row(t('动画样式','Animation style'),t('现代样式使用平滑网格变形；经典样式呈现传统神奇效果，资源占用更低','Modern uses smooth mesh deformation; Classic uses the traditional Genie effect with fewer resources'),settingsSelect('minimize-engine',t('动画样式','Animation style'),p.engine,engines(),busy))+
    `<div class="reset-row"><button class="reset" data-action="minimize-reset" ${busy?'disabled':''}>${t('恢复默认','Restore defaults')}</button></div>`:
    `<p>${t('为顶层窗口提供 macOS 神奇效果的最小化、还原与打开动画；子窗口、尺寸过小的窗口和隐藏窗口会被跳过。','Use macOS Genie effects to minimize, restore and open top-level windows. Child, very small and hidden windows are skipped.')}</p><p>${t('展开动画默认开启，可能短暂闪烁，也可能无法捕获无边框应用；多显示器支持为实验性选项，默认关闭。','The open animation is on by default; it may briefly flicker or fail to capture frameless apps. Multiple-display support is experimental and off by default.')}</p><p>${t('动画期间临时关闭目标窗口的系统过渡效果，结束后恢复。使用亚克力或半透明效果的窗口，在动画快照中可能显示为不透明。','System transitions for the target window are temporarily disabled during animation and restored afterward. Acrylic or translucent windows may appear opaque in animation snapshots.')}</p><p>${t('恢复默认会恢复原版动画参数，并保留当前功能启用状态。关闭功能或退出 Lume 会停止动画服务。','Restoring defaults resets animation parameters while keeping the current feature enable state. Disabling the feature or exiting Lume stops the animation service.')}</p>`;
  return settingsFrame({title:t('macOS 最小化动画','macOS minimize animation'),description:t('为窗口提供流畅的 macOS 神奇效果最小化、还原与打开动画。','Smooth macOS Genie effects for minimizing, restoring and opening windows.'),
    tab,body,scope:'minimize',status:minimizeStatus(state),error:state.minimizeStatus==='error',backLabel:t('返回桌面美化','Back to desktop styles')});
}
export function syncMinimizeControls(root,state) {
  const busy=['saving','loading'].includes(state.minimizeStatus),p=state.minimize;
  for(const key of ['enabled','restore','launch','multiMonitor','unhideTaskbar']) {
    root.querySelectorAll(`[data-action="minimize-${key==='enabled'?'toggle':key}"]`).forEach(button=>{
      button.classList.toggle('checked',p[key]);button.setAttribute('aria-checked',String(p[key]));
      syncSettingsControlBusy(button,busy);
    });
  }
  for(const key of ['durationMs','unhideDurationMs']) {
    const input=root.querySelector(`#minimize-${key}`);if(!input)continue;
    if(input.ownerDocument.activeElement!==input || ['saved','error','unavailable'].includes(state.minimizeStatus))input.value=String(p[key]);
    syncSettingsControlBusy(input,busy);
  }
  syncSettingsSelect(root,'minimize-engine',p.engine,engines(),busy,true);
  const reset=root.querySelector('[data-action="minimize-reset"]');if(reset)syncSettingsControlBusy(reset,busy);
  const status=root.querySelector('[data-settings-scope="minimize"] .status');
  if(status) {
    status.textContent=minimizeStatus(state);status.hidden=!status.textContent;
    status.setAttribute('role',state.minimizeStatus==='error'?'alert':'status');
    status.classList.toggle('error',state.minimizeStatus==='error');
  }
}
