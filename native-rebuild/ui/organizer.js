import {escapeHTML as h} from './dom.js';
import {settingsFrame,settingsSelect,syncSettingsSelect} from './settings-frame.js';
import {t} from './localization.js';
import {organizerDisplayedEnabled} from './state.js';
import {settingsRange,syncSettingsRange} from './settings-range.js';
const materials=()=>[['liquid-glass',t('液态玻璃','Liquid glass')],['frosted-glass',t('磨砂玻璃','Frosted glass')]];
export function organizerStatus(state){
  if(state.organizerStatus==='saving')return typeof state.organizerPending==='boolean'
    ? state.organizerPending?t('正在开启...','Starting...'):t('正在关闭...','Stopping...')
    : t('正在保存...','Saving...');
  if(state.organizerStatus==='loading')return t('正在读取设置...','Loading settings...');
  if(state.organizerStatus==='unavailable')return t('桌面宿主未连接，图标收纳设置未保存','Desktop host disconnected. Icon organizer settings were not saved.');
  if(state.organizerStatus==='error')return {
    runtime:t('图标收纳启动失败，请检查安装文件','Could not start the icon organizer. Check the installation files.'),save:t('图标收纳设置保存失败，已恢复此前设置，请重试','Could not save icon organizer settings. Previous settings restored. Please try again.'),
    rollback:t('图标收纳恢复失败，请重新启用组件','Could not restore the icon organizer. Enable the widget again.'),editor:t('收纳标题编辑器启动失败，请重试','Could not open the organizer title editor. Please try again.'),
    open:t('无法打开收纳项目，请检查默认应用和访问权限','Could not open the item. Check the default app and access permissions.'),invalid:t('收纳项目无效，请选择有效的本地文件或文件夹','Invalid item. Select a valid local file or folder.'),
    target:t('收纳项目不存在或无法访问，请检查原文件','The item is missing or inaccessible. Check the original file.'),limit:t('收纳框或项目数量已达上限，请移除部分引用后重试','The collection or item limit has been reached. Remove some references and try again.'),
  }[state.organizerError]||t('图标收纳配置读取失败，当前使用默认设置；尚未保存','Could not load icon organizer settings. Using defaults; not yet saved.');
  if(state.organizerLoadStatus==='recovered')return t('已从备份恢复图标收纳配置，尚未保存到主配置','Icon organizer settings restored from backup; not yet saved to the main configuration.');
  if(state.organizerMaterialStatus==='transparent-fallback')return t('材质暂不可用，当前为透明显示','Material unavailable. Using a transparent fallback.');
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
    `<div class="setting-row"><label>${t('已收纳项目','Collected items')}</label><span>${p.count}${t(' 个项目',p.count===1?' item':' items')}</span></div>`+
    `<div class="reset-row"><button class="reset" data-action="organizer-reset" ${disabled?'disabled':''}>${t('恢复默认','Restore defaults')}</button></div>`:
    `<div class="settings-details"><h3>${t('功能说明','About this widget')}</h3><p>${h(description)}</p><p>${t('默认在同一收纳内提供“桌面”和“文件”两个标签，图标大小为 70%。自动读取用户桌面及公共桌面上的项目：应用和应用快捷方式放在“桌面”，文件夹、文档、图片、视频等放在“文件”。桌面上的新增、删除和重命名会自动同步，原文件位置不变。','The default organizer has Desktop and Files tabs with 70% icons. Apps and app shortcuts from your user and public desktops appear in Desktop; folders, documents, images and videos appear in Files. Desktop additions, deletions and renames sync automatically. Original files stay in place.')}</p><p>${t('可将本地文件和文件夹拖入收纳框，点击项目用系统默认程序打开；移除项目只移除引用，不删除原文件。项目可拖动重排，设置会独立保存。','Drag local files and folders into a collection. Click an item to open it in the default app. Removing an item removes only its reference, not the original file. Drag to reorder items. Settings are saved independently.')}</p></div>`;
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
