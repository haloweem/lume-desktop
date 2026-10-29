import {escapeHTML as h} from './dom.js';
import {settingsFrame,settingsSelect,syncSettingsSelect} from './settings-frame.js';
import {t} from './localization.js';

const materials=()=>[['liquid-glass',t('液态玻璃','Liquid glass')],['frosted-glass',t('磨砂玻璃','Frosted glass')]];
const resourceBytes = value => value === null || value === undefined
  ? '--'
  : `${(value / (1024 * 1024)).toFixed(value >= 1024 * 1024 * 10 ? 0 : 1)} MiB`;
const resourceCount = value => value === null || value === undefined ? '--' : String(value);
const resourceProcessLabels = {
  Shell: ['Lume 主程序', 'Lume core'],
  WebViewBrowser: ['Lume 页面浏览器', 'Lume page browser'],
  WebViewRenderer: ['Lume 页面渲染', 'Lume page renderer'],
  WebViewGpu: ['Lume 图形组件', 'Lume graphics'],
  WebViewUtility: ['Lume 后台服务', 'Lume background service'],
  WebViewCrashpad: ['Lume 崩溃辅助', 'Lume crash helper'],
  MusicHost: ['Lume 音乐', 'Lume music'],
  Wallpaper: ['壁纸', 'Wallpaper'],
  Other: ['其他 Lume 进程', 'Other Lume process'],
};
function resourceProcessLabel(process) {
  const labels = resourceProcessLabels[process?.role] || resourceProcessLabels.Other;
  return t(labels[0], labels[1]);
}
function resourceOverview(snapshot) {
  const unavailable = !snapshot;
  const hasUnavailableGroup = snapshot && Object.values(snapshot)
    .filter(value => value && typeof value === 'object' && !Array.isArray(value))
    .some(value => value.coverage === 'unavailable');
  const group = (key, label, metric = 'workingSetBytes') => {
    const value = snapshot?.[key];
    return `<div class="resource-row"><span class="resource-label">${h(label)}</span>` +
      `<strong><span data-resource-value="${key}-${metric === 'workingSetBytes' ? 'workingSet' : metric}">${resourceBytes(value?.[metric])}</span></strong>` +
      `<span class="resource-process-count">${t('进程','processes')} ${resourceCount(value?.processCount)}</span></div>`;
  };
  const gpu = snapshot?.gpu;
  const gpuMetric = gpu && gpu.gpuDedicatedBytes !== null && gpu.gpuDedicatedBytes !== undefined
    ? resourceBytes(gpu.gpuDedicatedBytes)
    : '--';
  const processes = (snapshot?.processes || []).filter(process => process.role !== 'Wallpaper');
  const nonWallpaper = snapshot?.nonWallpaper;
  return `<section class="resource-overview" aria-label="${t('资源总览','Resource overview')}">` +
    `<div class="resource-overview-heading"><h3>${t('资源总览','Resource overview')}</h3>` +
    `<span class="resource-coverage">${unavailable ? '--' :
      hasUnavailableGroup ? t('部分数据不可用','Some data unavailable') : t('实时采样','Live sample')}</span></div>` +
    `<details class="resource-lume-group">` +
    `<summary class="resource-lume-summary"><span class="resource-lume-title">${t('Lume','Lume')}</span>` +
    `<span class="resource-lume-total"><strong><span data-resource-value="nonWallpaper-workingSet">${resourceBytes(nonWallpaper?.workingSetBytes)}</span></strong>` +
    `<span>${t('非壁纸','non-wallpaper')} · ${t('进程','processes')} ${resourceCount(nonWallpaper?.processCount)}</span></span></summary>` +
    `<div class="resource-lume-children resource-grid">` +
    group('shell', t('Lume 主程序','Lume core')) +
    group('webview', t('Lume 组件','Lume components')) +
    group('music', t('音乐','Music')) +
    `<div class="resource-row"><span class="resource-label">${t('GPU','GPU')}</span><strong>` +
    `<span data-resource-value="gpu-gpuDedicated">${gpuMetric}</span></strong>` +
    `<span class="resource-process-count">${t('进程','processes')} ${resourceCount(gpu?.processCount)}</span></div>` +
    `</div>` +
    `<details class="resource-processes"><summary>${t('查看 Lume 进程明细','View Lume process details')}</summary>` +
    `<div class="resource-process-list">${processes.length ? processes.map(process =>
      `<div class="resource-process"><span>${h(resourceProcessLabel(process))}</span>` +
      `<span>${resourceBytes(process.workingSetBytes)}</span><span>PID ${process.pid}</span></div>`).join('') :
      `<p>${t('暂无 Lume 进程采样','No Lume process samples')}</p>`}</div></details>` +
    `</details>` +
    `<div class="resource-row resource-wallpaper-row"><span class="resource-label">${t('壁纸（独立统计）','Wallpaper (separate)')}</span><strong>` +
    `<span data-resource-value="wallpaper-workingSet">${resourceBytes(snapshot?.wallpaper?.workingSetBytes)}</span></strong>` +
    `<span class="resource-process-count">${t('不计入 Lume 总内存','Excluded from Lume total')} · ${t('进程','processes')} ${resourceCount(snapshot?.wallpaper?.processCount)}</span></div>` +
    `</section>`;
}
export function performanceStatus(state) {
  if(state.performanceStatus==='saving')return t('正在保存...','Saving...');
  if(state.performanceStatus==='loading')return t('正在读取设置...','Loading settings...');
  if(state.performanceStatus==='unavailable')return t('桌面宿主未连接，性能设置未保存','Desktop host disconnected. Performance settings were not saved.');
  if(state.performanceStatus==='error')return {
    runtime:t('性能组件启动失败，请检查安装文件','Could not start the performance widget. Check the installation files.'),
    layer:t('无法把性能组件放到桌面层，可能有更高权限的覆盖窗口；请关闭后重试','Could not place the performance widget on the desktop layer. A higher-privilege overlay may block it; close it and try again.'),
    save:t('性能设置保存失败，已恢复此前设置，请重试','Could not save performance settings. Previous settings restored. Please try again.'),
    rollback:t('性能组件恢复失败，请重新启用组件','Could not restore the performance widget. Enable it again.'),
  }[state.performanceError] || t('性能配置读取失败，当前使用默认设置；尚未保存','Could not load performance settings. Using defaults; not yet saved.');
  if(state.performanceLoadStatus==='recovered')return t('已从备份恢复性能配置，尚未保存到主配置','Performance settings restored from backup; not yet saved to the main configuration.');
  if(state.performanceMaterialStatus==='transparent-fallback')return t('材质暂不可用，当前为透明显示','Material unavailable. Using a transparent fallback.');
  return '';
}
export function performanceNotice(state,{previousStatus,hasInlineStatus=false}={}) {
  if(hasInlineStatus)return null;
  if(['error','unavailable','recovered'].includes(state.performanceStatus))return performanceStatus(state);
  if(state.performanceStatus==='saved'&&previousStatus==='saving')return t('性能设置已应用并保存','Performance settings applied and saved');
  return null;
}
export function performancePage(state,tab='settings') {
  const description=t('在桌面查看 CPU、GPU、内存与网络状态。','View CPU, GPU, memory and network status on your desktop.');
  const disabled=state.performanceStatus==='saving',p=state.performance;
  const body=tab==='settings'?
    `<div class="setting-row"><label for="performance-material">${t('材质设置','Material')}</label>${settingsSelect('performance-material',t('材质设置','Material'),p.material,materials(),disabled)}</div>`+
    `<div class="reset-row"><button class="reset" data-action="performance-reset" ${disabled?'disabled':''}>${t('恢复默认','Restore defaults')}</button></div>`:
    `<div class="settings-details"><h3>${t('功能说明','About this widget')}</h3><p>${h(description)}</p><p>${t('启用后每秒更新。组件可拖动、调整大小或锁定；关闭后停止采样。暂时无法获取的数据以 -- 显示。','Updates once per second while enabled. Drag, resize or lock the widget. Sampling stops when disabled. Unavailable readings are shown as --.')}</p></div>`;
  return settingsFrame({title:t('性能','Performance'),description,tab,
    body:resourceOverview(state.resourceSnapshot)+body,status:performanceStatus(state),
    error:state.performanceStatus==='error',scope:'performance'});
}
export function syncPerformanceResourceOverview(root,state) {
  const overview=root.querySelector('.resource-overview');
  if (overview) overview.outerHTML=resourceOverview(state.resourceSnapshot);
}
export function syncPerformanceControls(root,state) {
  const disabled=state.performanceStatus==='saving';
  root.querySelectorAll('[data-action="performance-toggle"]').forEach(button=>{
    button.classList.toggle('checked',state.performance.enabled);
    button.setAttribute('aria-checked',String(state.performance.enabled));button.disabled=disabled;
  });
  syncSettingsSelect(root,'performance-material',state.performance.material,materials(),disabled);
  const reset=root.querySelector('[data-action="performance-reset"]');if(reset)reset.disabled=disabled;
  const status=root.querySelector('[data-settings-scope="performance"] .status');
  if(status) {
    status.textContent=performanceStatus(state);status.hidden=!status.textContent;
    status.setAttribute('role',state.performanceStatus==='error'?'alert':'status');
    status.classList.toggle('error',state.performanceStatus==='error');
  }
}
