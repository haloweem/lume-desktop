import {escapeHTML as h} from './dom.js';
import {settingsFrame,settingsSelect,syncSettingsSelect} from './settings-frame.js';
import {t} from './localization.js';

const materials=()=>[['solid',t('普通','Standard')],['liquid-glass',t('液态玻璃','Liquid glass')],['frosted-glass',t('磨砂玻璃','Frosted glass')]];
export function todayTodoStatus(state) {
  if(state.todayTodoStatus==='saving')return t('正在保存...','Saving...');
  if(state.todayTodoStatus==='loading')return t('正在读取设置...','Loading settings...');
  if(state.todayTodoStatus==='unavailable')return t('桌面宿主未连接，待办清单设置未保存','Desktop host disconnected. To-do list settings were not saved.');
  if(state.todayTodoStatus==='error')return {
    runtime:t('待办清单组件启动失败，请检查安装文件','Could not start the to-do list widget. Check the installation files.'),
    layer:t('无法把待办清单放到桌面层，可能有更高权限的覆盖窗口；请关闭后重试','Could not place the to-do list on the desktop layer. A higher-privilege overlay may block it; close it and try again.'),
    save:t('待办清单设置保存失败，已恢复此前设置，请重试','Could not save to-do list settings. Previous settings restored. Please try again.'),
    rollback:t('待办清单组件恢复失败，请重新启用组件','Could not restore the to-do list widget. Enable it again.'),
  }[state.todayTodoError] || t('待办清单配置读取失败，当前使用默认设置；尚未保存','Could not load to-do list settings. Using defaults; not yet saved.');
  if(state.todayTodoLoadStatus==='recovered')return t('已从备份恢复待办清单配置，尚未保存到主配置','To-do list settings restored from backup; not yet saved to the main configuration.');
  if(state.todayTodoMaterialStatus==='transparent-fallback')return t('材质暂不可用，当前为透明显示','Material unavailable. Using a transparent fallback.');
  return '';
}
export function todayTodoNotice(state,{previousStatus,hasInlineStatus=false}={}) {
  if(hasInlineStatus)return null;
  if(['error','unavailable','recovered'].includes(state.todayTodoStatus))return todayTodoStatus(state);
  if(state.todayTodoStatus==='saved'&&previousStatus==='saving')return t('待办清单设置已应用并保存','To-do list settings applied and saved');
  return null;
}
export function todayTodoPage(state,tab='settings') {
  const description=t('在桌面管理今日待办，支持勾选、编辑和删除。','Manage today’s tasks on your desktop: complete, edit or delete them.');
  const disabled=state.todayTodoStatus==='saving',p=state.todayTodo;
  const body=tab==='settings'?
    `<div class="setting-row"><label for="todayTodo-material">${t('材质设置','Material')}</label>${settingsSelect('todayTodo-material',t('材质设置','Material'),p.material,materials(),disabled)}</div>`+
    `<div class="reset-row"><button class="reset" data-open="calendar">${t('打开日历设置','Open calendar settings')}</button><button class="reset" data-action="todayTodo-reset" ${disabled?'disabled':''}>${t('恢复默认','Restore defaults')}</button></div>`:
    `<div class="settings-details"><h3>${t('功能说明','About this widget')}</h3><p>${h(description)}</p><p>${t('清单仅显示今天的待办，修改与日历同步。点击圆圈勾选完成，右键某条待办可编辑或删除。右键菜单中“新建待办”添加今天的事项，“新建清单”添加另一份独立桌面组件。各清单分别保存位置、大小和材质，关闭一份不影响其他清单。更多内容可用鼠标滚轮直接上下滑动，不显示翻页箭头和滚动条；也可拖拽边框放大，支持锁定位置。重复日程的预览项沿用日历的系列操作。','Only today’s tasks are shown and changes sync with the calendar. Check a circle to complete or right-click a task to edit or delete. New task adds an item for today. New checklist adds an independent desktop widget. Each checklist keeps its own position, size and material; closing one leaves the others open. Use the mouse wheel to scroll directly without paging arrows or a scrollbar. Drag the borders to enlarge, or lock the position. Projected repeating tasks use calendar series actions.')}</p></div>`;
  return settingsFrame({title:t('待办清单','To-do list'),description,tab,body,status:todayTodoStatus(state),error:state.todayTodoStatus==='error',scope:'todayTodo'});
}
export function syncTodayTodoControls(root,state) {
  const disabled=state.todayTodoStatus==='saving';
  root.querySelectorAll('[data-action="todayTodo-toggle"]').forEach(button=>{
    button.classList.toggle('checked',state.todayTodo.enabled);
    button.setAttribute('aria-checked',String(state.todayTodo.enabled));button.disabled=disabled;
  });
  syncSettingsSelect(root,'todayTodo-material',state.todayTodo.material,materials(),disabled);
  const reset=root.querySelector('[data-action="todayTodo-reset"]');if(reset)reset.disabled=disabled;
  const status=root.querySelector('[data-settings-scope="todayTodo"] .status');
  if(status) {
    status.textContent=todayTodoStatus(state);status.hidden=!status.textContent;
    status.setAttribute('role',state.todayTodoStatus==='error'?'alert':'status');
    status.classList.toggle('error',state.todayTodoStatus==='error');
  }
}
