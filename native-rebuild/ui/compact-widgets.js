import {t} from './localization.js';
import {compactWidgetThumbnail,compactTimeReadings} from './compact-widget-thumbnails.js';
import {syncSettingsControlBusy} from './settings-frame.js';

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
  null, // Retired file tray: reserve wire ID 18 so later choices keep their IDs.
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
const selectedWidget=(state,side)=>{
  const id=state.dynamicIsland[side+'Widget']??0;
  return compactWidgets[id]?id:0;
};
export function compactSelectionLabel(state){
  const side=state.islandCompactSide==='right'?'right':'left';
  const item=compactWidgets[selectedWidget(state,side)];
  return `${side==='left'?t('左侧','Left'):t('右侧','Right')} · ${t(item[1],item[2])}`;
}
function selectionHint(side){return side==='left'?t('点上方预览的右半边可切到右侧','Click the right half of the preview to select the right side'):
  t('点上方预览的左半边可切到左侧','Click the left half of the preview to select the left side');}
const snapshotReadings=state=>new Map([
  ...compactTimeReadings(state.islandCompactCapturedAt||Date.now()),...(state.islandCompactReadings||[]),
].map(row=>[row.id,row]));
export function compactWidgetPicker(state){
  const side=state.islandCompactSide==='right'?'right':'left';
  const selected=selectedWidget(state,side);
  const item=compactWidgets[selected]||compactWidgets[0];
  const readings=snapshotReadings(state);
  const groups=[['time',t('时间日期','Time and date')],['system',t('系统','System')],['weather',t('天气','Weather')]];
  return `<section class="island-compact-picker" aria-label="${t('收缩小组件','Compact widgets')}">
    <div class="island-compact-selection">
      <span data-island-selection-side>${side==='left'?t('左侧图标','Left widget'):t('右侧图标','Right widget')}</span>
      <div class="island-compact-selection__value">
        <span class="island-compact-selection__sample" data-island-selection-sample data-widget="${selected}" aria-hidden="true">${compactWidgetThumbnail(selected,readings.get(selected))}</span>
        <strong data-island-selection-name>${t(item[1],item[2])}</strong>
      </div>
      <span class="island-compact-selection__hint" data-island-selection-hint>${selectionHint(side)}</span>
    </div>
    ${groups.map(([group,label])=>`<div class="island-compact-picker__group"><h3>${label}</h3><div class="island-compact-picker__grid">${compactWidgets.map((item,id)=>{
      if(!item||item[3]!==group)return '';
      return `<button type="button" data-island-compact-widget="${id}" aria-pressed="${selected===id}" aria-label="${t(item[1],item[2])}">
        <span class="island-compact-picker__chip"><span class="island-compact-picker__sample" data-island-compact-sample="${id}">${compactWidgetThumbnail(id,readings.get(id))}</span><span class="island-widget-enable" data-island-compact-enable aria-hidden="true"><span></span></span></span>
        <span class="island-compact-picker__name">${t(item[1],item[2])}</span></button>`;
    }).join('')}</div></div>`).join('')}
  </section>`;
}
export function syncCompactWidgetPicker(root,state){
  const picker=root.querySelector('.island-compact-picker');if(!picker)return;
  const side=state.islandCompactSide==='right'?'right':'left',selected=selectedWidget(state,side);
  const readings=snapshotReadings(state);
  const updateSample=(sample,id)=>{
    const row=readings.get(id),key=JSON.stringify([id,row]);
    if(sample.dataset.reading===key)return;
    sample.innerHTML=compactWidgetThumbnail(id,row);sample.dataset.reading=key;
  };
  root.querySelectorAll('[data-island-compact-side]').forEach(button=>button.setAttribute('aria-pressed',String(button.dataset.islandCompactSide===side)));
  picker.querySelectorAll('[data-island-compact-widget]').forEach(button=>{
    const id=Number(button.dataset.islandCompactWidget);
    button.setAttribute('aria-pressed',String(selected===id));
    syncSettingsControlBusy(button,['loading','saving','unavailable'].includes(state.dynamicIslandStatus));
    button.toggleAttribute('data-island-pending',state.dynamicIslandStatus==='saving');
    updateSample(button.querySelector('[data-island-compact-sample]'),id);
  });
  const id=selected,item=compactWidgets[id];
  picker.querySelector('[data-island-selection-side]').textContent=side==='left'?t('左侧图标','Left widget'):t('右侧图标','Right widget');
  picker.querySelector('[data-island-selection-name]').textContent=t(item[1],item[2]);
  picker.querySelector('[data-island-selection-hint]').textContent=selectionHint(side);
  const sample=picker.querySelector('[data-island-selection-sample]');
  updateSample(sample,id);sample.dataset.widget=String(id);
}
