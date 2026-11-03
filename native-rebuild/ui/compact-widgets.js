import {t} from './localization.js';
import {compactWidgetThumbnail} from './compact-widget-thumbnails.js';

// Numeric IDs match core/dynamic_island/compact_widgets.h and the native wire.
export const compactWidgets=[
  ['none','无','None','time','CircleOff'],
  ['lunar','农历','Lunar date','time','Moon'],
  ['calendar','日历','Calendar','time','CalendarDays'],
  ['date','日期','Date','time','CalendarRange'],
  ['weekday','星期','Weekday','time','CalendarCheck'],
  ['clock','时钟','Clock','time','Clock3'],
  ['dayProgress','日进度','Day progress','time','Gauge'],
  ['weekProgress','周进度','Week progress','time','ChartNoAxesCombined'],
  ['monthProgress','月进度','Month progress','time','ChartNoAxesCombined'],
  ['quarterProgress','季进度','Quarter progress','time','ChartNoAxesCombined'],
  ['yearProgress','年进度','Year progress','time','ChartNoAxesCombined'],
  ['network','网速','Network speed','system','Network'],
  ['power','电源','Power','system','Power'],
  ['battery','电量','Battery','system','Battery'],
  ['cpu','处理器','Processor','system','Cpu'],
  ['memory','内存','Memory','system','MemoryStick'],
  ['disk','磁盘','Disk','system','HardDrive'],
  ['cpuMemory','处理器与内存','CPU and memory','system','Activity'],
  ['files','文件暂存','File tray','system','FolderOpen'],
  ['weather','天气情况','Conditions','weather','CloudSun'],
  ['temperatureRange','温度范围','Temperature range','weather','Thermometer'],
  ['humidity','湿度','Humidity','weather','Droplets'],
  ['windSpeed','风速','Wind speed','weather','Wind'],
  ['windDirection','风向','Wind direction','weather','Compass'],
  ['uv','紫外线','UV index','weather','Sun'],
  ['sunriseSunset','日出日落','Sunrise and sunset','weather','Sunrise'],
  ['pressure','气压','Pressure','weather','Gauge'],
  ['temperature','实时温度','Temperature','weather','Thermometer'],
  ['visibility','能见度','Visibility','weather','Eye'],
  ['feelsLike','体感温度','Feels like','weather','Thermometer'],
  ['precipitation','降水概率','Precipitation','weather','CloudRain'],
];
export function compactSelectionLabel(state){
  const side=state.islandCompactSide==='right'?'right':'left';
  const item=compactWidgets[state.dynamicIsland[side+'Widget']??0]||compactWidgets[0];
  return `${side==='left'?t('左侧','Left'):t('右侧','Right')} · ${t(item[1],item[2])}`;
}
function selectionHint(side){return side==='left'?t('点上方预览的右半边可切到右侧','Click the right half of the preview to select the right side'):
  t('点上方预览的左半边可切到左侧','Click the left half of the preview to select the left side');}
export function compactWidgetPicker(state){
  const island=state.dynamicIsland,side=state.islandCompactSide==='right'?'right':'left';
  const selected=island[side+'Widget']??0;
  const item=compactWidgets[selected]||compactWidgets[0];
  const groups=[['time',t('时间日期','Time and date')],['system',t('系统','System')],['weather',t('天气','Weather')]];
  return `<section class="island-compact-picker" aria-label="${t('收缩小组件','Compact widgets')}">
    <div class="island-compact-selection">
      <span data-island-selection-side>${side==='left'?t('左侧图标','Left widget'):t('右侧图标','Right widget')}</span>
      <div class="island-compact-selection__value">
        <span class="island-compact-selection__sample" data-island-selection-sample data-widget="${selected}" aria-hidden="true">${compactWidgetThumbnail(selected)}</span>
        <strong data-island-selection-name>${t(item[1],item[2])}</strong>
        <button type="button" data-island-compact-clear aria-label="${t('清除当前侧组件','Clear selected side')}" ${selected===0?'disabled':''}>×</button>
      </div>
      <span class="island-compact-selection__hint" data-island-selection-hint>${selectionHint(side)}</span>
    </div>
    ${groups.map(([group,label])=>`<div class="island-compact-picker__group"><h3>${label}</h3><div class="island-compact-picker__grid">${compactWidgets.map((item,id)=>{
      if(item[3]!==group)return '';
      return `<button type="button" data-island-compact-widget="${id}" aria-pressed="${selected===id}" aria-label="${t(item[1],item[2])}">
        <span class="island-compact-picker__chip"><span class="island-compact-picker__sample">${compactWidgetThumbnail(id)}</span></span>
        <span class="island-compact-picker__name">${t(item[1],item[2])}</span></button>`;
    }).join('')}</div></div>`).join('')}
  </section>`;
}
export function syncCompactWidgetPicker(root,state){
  const picker=root.querySelector('.island-compact-picker');if(!picker)return;
  const side=state.islandCompactSide==='right'?'right':'left',current=state.dynamicIsland;
  root.querySelectorAll('[data-island-compact-side]').forEach(button=>button.setAttribute('aria-pressed',String(button.dataset.islandCompactSide===side)));
  picker.querySelectorAll('[data-island-compact-widget]').forEach(button=>{
    const id=Number(button.dataset.islandCompactWidget);
    button.setAttribute('aria-pressed',String((current[side+'Widget']??0)===id));
    button.disabled=state.dynamicIslandStatus==='saving';
  });
  const id=current[side+'Widget']??0,item=compactWidgets[id]||compactWidgets[0];
  picker.querySelector('[data-island-selection-side]').textContent=side==='left'?t('左侧图标','Left widget'):t('右侧图标','Right widget');
  picker.querySelector('[data-island-selection-name]').textContent=t(item[1],item[2]);
  picker.querySelector('[data-island-selection-hint]').textContent=selectionHint(side);
  picker.querySelector('[data-island-compact-clear]').disabled=id===0||state.dynamicIslandStatus==='saving';
  const sample=picker.querySelector('[data-island-selection-sample]');
  if(sample.dataset.widget!==String(id)){sample.innerHTML=compactWidgetThumbnail(id);sample.dataset.widget=String(id);}
}
