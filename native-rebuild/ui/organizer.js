import {settingsFrame,settingsSelect,syncSettingsSelect} from './settings-frame.js';
import {t} from './localization.js';
import {organizerDisplayedEnabled} from './state.js';
import {settingsRange,syncSettingsRange} from './settings-range.js';
const materials=()=>[['liquid-glass',t('液态玻璃','Liquid glass')],['frosted-glass',t('磨砂玻璃','Frosted glass')]];
export function organizerStatus(state){
  if(state.organizerStatus==='saving')return typeof state.organizerPending==='boolean'
    ? state.organizerPending?t('正在开启...','Starting...'):t('正在关闭...','Stopping...')
    : '';
  if(state.organizerStatus==='loading')return t('正在读取设置...','Loading settings...');
  if(state.organizerStatus==='unavailable')return t('桌面宿主未连接，图标收纳设置未保存','Desktop host disconnected. Icon organizer settings were not saved.');
  if(state.organizerStatus==='error')return {
    runtime:t('图标收纳启动失败，请检查安装文件','Could not start the icon organizer. Check the installation files.'),save:t('图标收纳设置保存失败，已恢复此前设置，请重试','Could not save icon organizer settings. Previous settings restored. Please try again.'),
    rollback:t('图标收纳恢复失败，请重新启用组件','Could not restore the icon organizer. Enable the widget again.'),editor:t('收纳标题编辑器启动失败，请重试','Could not open the organizer title editor. Please try again.'),
    open:t('无法打开收纳项目，请检查默认应用和访问权限','Could not open the item. Check the default app and access permissions.'),invalid:t('收纳项目无效，请选择有效的本地文件或文件夹','Invalid item. Select a valid local file or folder.'),
    recycle:t('部分图标未移入回收站，已保留在收纳中','Some icons could not be recycled and remain in the collection.'),
    target:t('收纳项目不存在或无法访问，请检查原文件','The item is missing or inaccessible. Check the original file.'),limit:t('收纳框数量已达上限，请合并或关闭空收纳框后重试','The collection limit has been reached. Merge collections or close empty collections and try again.'),
  }[state.organizerError]||t('图标收纳配置读取失败，当前使用默认设置；尚未保存','Could not load icon organizer settings. Using defaults; not yet saved.');
  if(state.organizerLoadStatus==='recovered')return t('已从备份恢复图标收纳配置，尚未保存到主配置','Icon organizer settings restored from backup; not yet saved to the main configuration.');
  if(state.organizerMaterialStatus==='transparent-fallback')return t('当前使用兼容材质','Using a compatible material.');
  return '';
}
export function organizerNotice(state,{previousStatus,hasInlineStatus=false}={}){
  if(hasInlineStatus)return null;
  if(['error','unavailable','recovered'].includes(state.organizerStatus))return organizerStatus(state);
  if(state.organizerStatus==='saved'&&previousStatus==='saving')return !state.organizer.enabled&&!state.organizer.running
    ?t('图标收纳已关闭','Icon organizer closed')
    :t('图标收纳设置已应用并保存','Icon organizer settings applied and saved');
  return null;
}
export function organizerPage(state,tab='settings'){
  const description=t('在桌面集中收纳快捷方式、文件与文件夹。','Organize shortcuts, files and folders on your desktop.');
  const disabled=state.organizerStatus==='saving',p=state.organizer;
  const body=tab==='settings'?
    `<div class="setting-row"><label for="organizer-material">${t('材质设置','Material')}</label>${settingsSelect('organizer-material',t('材质设置','Material'),p.material,materials(),disabled)}</div>`+
    `<div class="setting-row">${settingsRange('organizer-icon-scale',t('图标大小','Icon size'),p.iconScale??70,50,120,'%',disabled,'organizer','iconScale')}</div>`+
    `<div class="reset-row"><button class="reset" data-action="organizer-reset" ${disabled?'disabled':''}>${t('恢复默认','Restore defaults')}</button></div>`:
    `<div class="settings-details"><h3>${t('功能说明','About this widget')}</h3>
      <p>${t('默认有「桌面」和「文件」两个标签，图标大小为 70%。个人桌面和公共桌面上的应用及其快捷方式会自动同步到「桌面」，其他项目会同步到「文件」。','The organizer starts with Desktop and Files tabs and 70% icon size. Apps and app shortcuts from your personal and public desktops sync to Desktop. Other items sync to Files.')}</p>
      <p>${t('可将文件或文件夹拖入收纳，拖动图标排序，双击打开。右键选择「显示方式」，可切换折叠或展开，也可选择列表或图标布局。两项选择分别保存。','Drag files or folders into the organizer, drag icons to reorder them, and double-click to open. In the right-click menu, Display lets you collapse or expand the organizer and choose a list or icon layout. Each choice is saved separately.')}</p>
      <p>${t('右键图标打开「删除设置」。「移除图标」仅从收纳中隐藏该项，原文件保留在原处。「删除图标」会先请你确认，确认后将所选桌面项目移入回收站。','Right-click an icon and open Deletion settings. Remove icon hides the item from the organizer and keeps the original in place. Delete icon asks for confirmation before moving the selected desktop items to the Recycle Bin.')}</p>
    </div>`;
  return settingsFrame({title:t('图标收纳','Icon organizer'),description,tab,body,status:organizerStatus(state),error:state.organizerStatus==='error',scope:'organizer'});
}
export function syncOrganizerControls(root,state){
  const disabled=state.organizerStatus==='saving';
  root.querySelectorAll('[data-action="organizer-toggle"]').forEach(button=>{
    const enabled=organizerDisplayedEnabled(state);
    button.classList.toggle('checked',enabled);button.setAttribute('aria-checked',String(enabled));button.disabled=disabled;
    button.setAttribute('aria-busy',String(disabled));
    button.title=disabled?organizerStatus(state):'';
  });
  root.querySelectorAll('[data-organizer-pending]').forEach(label=>{
    label.textContent=disabled?organizerStatus(state):'';label.hidden=!disabled;
  });
  syncSettingsSelect(root,'organizer-material',state.organizer.material,materials(),disabled);
  syncSettingsRange(root,'organizer-icon-scale',state.organizer.iconScale??70,50,120,'%',disabled);
  const reset=root.querySelector('[data-action="organizer-reset"]');if(reset)reset.disabled=disabled;
  const status=root.querySelector('[data-settings-scope="organizer"] .status');
  if(status){status.textContent=organizerStatus(state);status.hidden=!status.textContent;status.setAttribute('role',state.organizerStatus==='error'?'alert':'status');status.classList.toggle('error',state.organizerStatus==='error');}
}
