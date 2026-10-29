import {toggle} from './dom.js';
import {settingsFrame} from './settings-frame.js';
import {t} from './localization.js';

const description=()=>t('双击桌面空白处隐藏或显示图标，并可在系统空闲后自动隐藏、输入时自动恢复。','Double-click an empty desktop area to hide or show icons. Optionally hide them when idle and restore them on input.');
export function desktopIconsStatus(state) {
  if(state.iconsStatus==='saving')return t('正在保存...','Saving...');
  if(state.iconsStatus==='loading')return t('正在读取设置...','Loading settings...');
  if(state.iconsStatus==='unavailable')return t('桌面宿主未连接，设置未保存','Desktop host disconnected. Settings were not saved.');
  if(state.iconsError)return {
    runtime:t('桌面图标服务启动失败，请重新启用','Could not start the desktop icons service. Enable it again.'),
    'input-busy':t('输入监听已被其他模块占用，请重试','Input monitoring is in use by another module. Please try again.'),
    visibility:t('桌面暂未响应，正在等待恢复','The desktop is not responding. Waiting for recovery.'),
    save:t('设置保存失败，已保留此前设置','Could not save settings. Previous settings retained.'),
    rollback:t('桌面图标尚未恢复，请再次关闭此功能重试','Desktop icons have not been restored. Try disabling this feature again.'),
  }[state.iconsError];
  if(state.iconsLoadStatus==='failed')return t('配置读取失败，当前使用默认设置；尚未保存','Could not load settings. Using defaults; not yet saved.');
  if(state.iconsLoadStatus==='recovered')return t('已从备份恢复设置，尚未保存到主配置','Settings restored from backup; not yet saved to the main configuration.');
  if(state.icons.enabled && state.icons.running && !state.icons.desktops)return t('正在等待桌面就绪...','Waiting for the desktop...');
  if(state.icons.enabled && !state.icons.running)return t('桌面图标服务未运行，请重新启用','The desktop icons service is not running. Enable it again.');
  return '';
}
export function desktopIconsNotice(state,{previousStatus,hasInlineStatus=false}={}) {
  if(hasInlineStatus)return null;
  if(['error','unavailable','recovered'].includes(state.iconsStatus))return desktopIconsStatus(state);
  if(state.iconsStatus==='saved'&&previousStatus==='saving')return t('桌面图标设置已应用并保存','Desktop icon settings applied and saved');
  return null;
}
export function desktopIconsPage(state,tab='settings') {
  const p=state.icons,disabled=state.iconsStatus==='saving';
  const row=(label,help,control)=>`<div class="setting-row"><div><label>${label}</label><p class="setting-help">${help}</p></div>${control}</div>`;
  const body=tab==='settings'?
    row(t('自动隐藏空闲桌面图标','Auto-hide idle desktop icons'),t('系统空闲达到指定时长后自动隐藏桌面图标','Hide desktop icons after the specified idle time'),toggle(p.enableAutoHide,t('自动隐藏空闲桌面图标','Auto-hide idle desktop icons'),'icons-autoHide',disabled))+
    `<div class="setting-row"><div><label for="icons-delay">${t('自动隐藏延迟秒数','Auto-hide delay (seconds)')}</label><p class="setting-help">${t('范围为 3 到 60 秒','Range: 3 to 60 seconds')}</p></div><input class="setting-number" id="icons-delay" type="number" min="3" max="60" step="1" value="${p.autoHideDelay}" ${disabled?'disabled':''}></div>`+
    row(t('任意输入时恢复图标','Restore icons on any input'),t('仅恢复自动隐藏的图标，不影响手动隐藏的图标','Only restore automatically hidden icons, not manually hidden ones'),toggle(p.showIconsOnAnyInput,t('任意输入时恢复图标','Restore icons on any input'),'icons-restoreOnInput',disabled))+
    `<div class="reset-row"><button class="reset" data-action="icons-reset" ${disabled?'disabled':''}>${t('恢复默认','Restore defaults')}</button></div>`:
    `<p>${t('双击桌面空白处隐藏或显示图标。手动隐藏的图标不会因鼠标移动或键盘输入自动恢复。','Double-click an empty desktop area to hide or show icons. Manually hidden icons stay hidden during mouse or keyboard input.')}</p><p>${t('图标由空闲自动隐藏时，启用“任意输入时恢复图标”可在再次输入时恢复。关闭功能会恢复被此功能隐藏的图标。','Enable Restore icons on any input to restore icons hidden while idle. Disabling the feature restores the icons it hid.')}</p>`;
  return settingsFrame({title:t('桌面图标切换与自动隐藏','Desktop icons'),description:description(),tab,body,scope:'icons',
    status:desktopIconsStatus(state),error:state.iconsStatus==='error',backLabel:t('返回桌面美化','Back to desktop styles')});
}
export function syncDesktopIconsControls(root,state) {
  const disabled=state.iconsStatus==='saving',p=state.icons;
  for(const [action,value] of [['toggle',p.enabled],['autoHide',p.enableAutoHide],['restoreOnInput',p.showIconsOnAnyInput]]) {
    root.querySelectorAll(`[data-action="icons-${action}"]`).forEach(button=>{
      button.classList.toggle('checked',value);button.setAttribute('aria-checked',String(value));button.disabled=disabled;
    });
  }
  const input=root.querySelector('#icons-delay');
  if(input) {
    if(input.ownerDocument.activeElement!==input || state.iconsStatus==='saved' || state.iconsStatus==='error')input.value=String(p.autoHideDelay);
    input.disabled=disabled;
  }
  const reset=root.querySelector('[data-action="icons-reset"]');if(reset)reset.disabled=disabled;
  const status=root.querySelector('[data-settings-scope="icons"] .status');
  if(status) {
    status.textContent=desktopIconsStatus(state);status.hidden=!status.textContent;
    status.setAttribute('role',state.iconsStatus==='error'?'alert':'status');
    status.classList.toggle('error',state.iconsStatus==='error');
  }
}
