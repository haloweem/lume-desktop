import {escapeHTML as h} from './dom.js';
import {settingsFrame,settingsSelect,syncSettingsSelect} from './settings-frame.js';
import {t} from './localization.js';

const materials=()=>[['liquid-glass',t('液态玻璃','Liquid glass')],['frosted-glass',t('磨砂玻璃','Frosted glass')]];
export const weatherCities=[['auto','自动定位'],['beijing','北京'],['shanghai','上海'],['guangzhou','广州'],
  ['shenzhen','深圳'],['hangzhou','杭州'],['chengdu','成都'],['wuhan','武汉'],['nanjing','南京']];
const cityOptions=()=>weatherCities.map(([key,label])=>[key,key==='auto'?t('自动定位','Automatic location'):label]);
export function weatherStatus(state) {
  if(state.weatherStatus==='saving')return t('正在保存...','Saving...');
  if(state.weatherStatus==='loading')return t('正在读取设置...','Loading settings...');
  if(state.weatherStatus==='unavailable')return t('桌面宿主未连接，天气设置未保存','Desktop host disconnected. Weather settings were not saved.');
  if(state.weatherStatus==='error')return {
    runtime:t('天气组件启动失败，请检查安装文件','Could not start the weather widget. Check the installation files.'),
    layer:t('无法把天气组件放到桌面层，可能有更高权限的覆盖窗口；请关闭后重试','Could not place the weather widget on the desktop layer. A higher-privilege overlay may block it; close it and try again.'),
    save:t('天气设置保存失败，已恢复此前设置，请重试','Could not save weather settings. Previous settings restored. Please try again.'),
    rollback:t('天气组件恢复失败，请重新启用组件','Could not restore the weather widget. Enable it again.'),
  }[state.weatherError] || t('天气配置读取失败，当前使用默认设置；尚未保存','Could not load weather settings. Using defaults; not yet saved.');
  if(state.weatherLoadStatus==='recovered')return t('已从备份恢复天气配置，尚未保存到主配置','Weather settings restored from backup; not yet saved to the main configuration.');
  if(state.weatherMaterialStatus==='transparent-fallback')return t('材质暂不可用，当前为透明显示','Material unavailable. Using a transparent fallback.');
  if(!state.weather.enabled)return '';
  if(state.weatherData.error)return state.weatherData.hasWeather?t('天气更新失败，显示上次天气','Weather update failed. Showing the last available weather.'):t('定位或天气不可用，可选择城市或稍后刷新','Location or weather unavailable. Select a city or refresh later.');
  if(state.weatherData.loading)return t('正在获取天气...','Fetching weather...');
  if(state.weatherData.cacheWriteFailed)return t('天气已更新，本地缓存保存失败','Weather updated, but the local cache could not be saved.');
  return '';
}
export function weatherNotice(state,{previousStatus,hasInlineStatus=false}={}) {
  if(hasInlineStatus)return null;
  if(['error','unavailable','recovered'].includes(state.weatherStatus))return weatherStatus(state);
  if(state.weatherStatus==='saved'&&previousStatus==='saving')return t('天气设置已应用并保存','Weather settings applied and saved');
  return null;
}
// With automatic location and Windows location off, IP lookups are all that
// is left, and a VPN hides the real city behind them.
function locationHint(state) {
  if(state.weather.city!=='auto'||state.weatherData.systemLocation!=='off')return '';
  return `<div class="setting-row weather-location-hint"><div><label>${t('系统定位未开启','Windows location is off')}</label>`+
    `<p class="setting-help">${t('开启后可通过附近的 Wi-Fi 准确定位城市，使用 VPN 或代理时也不受影响。','Turn it on to find your city from nearby Wi-Fi, even when using a VPN or proxy.')}</p></div>`+
    `<button class="reset" data-action="weather-location-settings">${t('打开定位设置','Open location settings')}</button></div>`;
}
export function weatherPage(state,tab='settings') {
  const description=t('在桌面显示当前天气，支持自动定位或选择城市。','Display current weather on your desktop using automatic location or a selected city.');
  const disabled=state.weatherStatus==='saving',p=state.weather;
  const body=tab==='settings'?
    `<div class="setting-row"><label for="weather-material">${t('材质设置','Material')}</label>${settingsSelect('weather-material',t('材质设置','Material'),p.material,materials(),disabled)}</div>`+
    `<div class="setting-row"><label for="weather-city">${t('城市','City')}</label>${settingsSelect('weather-city',t('城市','City'),p.city,cityOptions(),disabled)}</div>`+
    locationHint(state)+
    `<div class="reset-row"><button class="reset" data-action="weather-reset" ${disabled?'disabled':''}>${t('恢复默认','Restore defaults')}</button></div>`:
    `<div class="settings-details"><h3>${t('功能说明','About this widget')}</h3><p>${h(description)}</p><p>${t('组件可拖动、调整大小或锁定，位置随拖动保存。右键菜单或 F5 可刷新天气。退出 Lume 时组件一同关闭。','Drag, resize or lock the widget. Its position is saved when dragged. Refresh weather with the context menu or F5. The widget closes when you exit Lume.')}</p>`+
    `<h3>${t('数据来源与许可','Data sources and licenses')}</h3><p>${t('优先读取和风天气公开页面的实况及预报，数据权利归原提供方所有。','Current conditions and forecasts are primarily read from public QWeather pages. Data rights belong to the original provider.')}</p>`+
    `<p>${t('备用预报来自挪威气象局（MET Norway），依据署名 4.0 国际许可（CC BY 4.0）使用，经过单位换算与逐小时汇总。当前来源和时间可在天气概览的提示中查看。','Fallback forecasts are provided by MET Norway under CC BY 4.0, with unit conversions and hourly aggregation. The current source and time appear in the weather overview tooltip.')}</p></div>`;
  return settingsFrame({title:t('天气','Weather'),description,tab,body,status:weatherStatus(state),error:state.weatherStatus==='error',scope:'weather'});
}
export function syncWeatherControls(root,state) {
  const disabled=state.weatherStatus==='saving';
  root.querySelectorAll('[data-action="weather-toggle"]').forEach(button=>{
    button.classList.toggle('checked',state.weather.enabled);
    button.setAttribute('aria-checked',String(state.weather.enabled));button.disabled=disabled;
  });
  syncSettingsSelect(root,'weather-material',state.weather.material,materials(),disabled);
  syncSettingsSelect(root,'weather-city',state.weather.city,cityOptions(),disabled);
  const reset=root.querySelector('[data-action="weather-reset"]');if(reset)reset.disabled=disabled;
  const status=root.querySelector('[data-settings-scope="weather"] .status');
  if(status) {
    status.textContent=weatherStatus(state);status.hidden=!status.textContent;
    status.setAttribute('role',state.weatherStatus==='error'?'alert':'status');
    status.classList.toggle('error',state.weatherStatus==='error');
  }
}
