import {escapeHTML as h} from './dom.js';
import {t} from './localization.js';
import {syncSettingsControlBusy} from './settings-frame.js';
import {defaultIslandNotificationPreferences,validIslandNotificationValue} from './island-notifications-protocol.js';

const careEnabled=p=>p.waterEnabled||p.standEnabled||p.sleepEnabled||p.offWorkEnabled;
const weatherEnabled=p=>p.weatherSevere||p.weatherRain||p.weatherTemperatureDrop;
const limits={waterIntervalMinutes:[15,240],waterGoal:[1,20],standIntervalMinutes:[15,240],sleepLeadMinutes:[0,120],temperatureDropDegrees:[1,15]};
const preferenceOf=state=>({...defaultIslandNotificationPreferences(),...state.islandNotifications?.preferences});
const busy=state=>['loading','saving','unavailable'].includes(state.islandNotificationsStatus);
const svg=body=>'<svg viewBox="0 0 24 24" fill="none" aria-hidden="true">'+body+'</svg>';
const icons={
  lock:svg('<rect x="6" y="10" width="12" height="10" rx="2.5"/><path d="M8.5 10V7a3.5 3.5 0 0 1 7 0v3m-3.5 4v2"/>'),
  bell:svg('<path d="M5 17h14l-2-3V9a5 5 0 0 0-10 0v5l-2 3Zm5 3h4"/>'),
};
const chevron='<svg viewBox="0 0 16 16" aria-hidden="true"><path d="m6 3 5 5-5 5"/></svg>';
const icon=name=>'<span class="in-icon in-icon--'+name+'" aria-hidden="true"><img src="assets/notifications/'+name+'.png" alt="" width="40" height="40"></span>';
const disabledAttr=value=>value?' disabled':'';
function switchControl(key,label,p,disabled=false,small=false){
  return '<button type="button" class="switch in-switch'+(small?' in-switch--small':'')+(p[key]?' checked':'')+'" role="switch" aria-label="'+h(label)+'" aria-checked="'+!!p[key]+'" data-island-notification-key="'+key+'"'+disabledAttr(disabled)+'><span></span></button>';
}
function moduleHead(title,subtitle){
  return '<header class="in-module__head"><div><h2>'+title+'</h2><p>'+subtitle+'</p></div></header>';
}
function numberField(key,label,unit,p){
  const [min,max]=limits[key];
  return '<label class="in-field"><span>'+label+'</span><span class="in-field__input"><input type="number" min="'+min+'" max="'+max+'" step="1" value="'+h(p[key])+'" inputmode="numeric" data-island-notification-key="'+key+'" aria-label="'+h(label)+'"><span>'+unit+'</span></span></label>';
}
function timeField(key,label,p){
  return '<label class="in-field"><span>'+h(label)+'</span><input class="in-time" type="time" value="'+h(p[key])+'" data-island-notification-key="'+key+'" aria-label="'+h(label)+'"></label>';
}
function offWorkStyleField(p){
  const label=t('提醒样式','Reminder style');
  return '<label class="in-field"><span>'+label+'</span><select class="in-time" data-island-notification-key="offWorkStyle" aria-label="'+label+'">'+
    [['default',t('默认样式','Default style')],['squidward',t('章鱼哥','Squidward')]].map(([value,name])=>'<option value="'+value+'"'+(p.offWorkStyle===value?' selected':'')+'>'+name+'</option>').join('')+'</select></label>';
}
const editPanel=(id,body)=>'<div class="in-editor" id="island-notification-editor-'+id+'" data-island-notification-panel="'+id+'" hidden>'+body+'</div>';
function summary(p,id){
  if(id==='water')return p.waterIntervalMinutes+' '+t('分钟','min')+' · '+p.waterGoal+' '+t('杯 / 天','cups / day');
  if(id==='stand')return p.standIntervalMinutes+' '+t('分钟','min');
  if(id==='sleep')return p.sleepTime+' · '+t('提前 ','')+p.sleepLeadMinutes+' '+t('分钟','min before');
  if(id==='off-work')return p.offWorkTime;
  return p.temperatureDropDegrees+'°C';
}
function inactiveKey(key,p){
  if(['waterIntervalMinutes','waterGoal'].includes(key))return !p.waterEnabled;
  if(key==='standIntervalMinutes')return !p.standEnabled;
  if(['sleepTime','sleepLeadMinutes'].includes(key))return !p.sleepEnabled;
  if(key==='offWorkTime'||key==='offWorkStyle')return !p.offWorkEnabled;
  if(key==='temperatureDropDegrees')return !p.weatherTemperatureDrop;
  return false;
}
function row({id,key,label,hint,p,disabled,editor=''}){
  const inactive=disabled;
  const value=editor?'<button type="button" class="in-value" data-island-notification-editor="'+id+'" aria-label="'+h(t('设置','Configure')+' '+label)+'" aria-expanded="false" aria-controls="island-notification-editor-'+id+'"'+disabledAttr(inactive||!p[key])+'><span data-island-notification-summary="'+id+'">'+h(summary(p,id))+'</span>'+chevron+'</button>':'';
  return '<div class="in-row-shell" data-island-notification-row="'+key+'"><div class="in-row">'+icon(id)+'<div class="in-row__text"><span class="in-row__label">'+label+'</span></div><div class="in-row__controls">'+value+switchControl(key,label,p,inactive,true)+'</div></div>'+editor+'</div>';
}
function noticeStatus(state){
  const status=state.islandNotificationsStatus;
  if(status==='loading')return t('正在读取通知设置…','Loading notification settings…');
  if(status==='saving')return '';
  if(status==='unavailable')return t('桌面宿主未连接，设置暂不可保存','Desktop host is disconnected. Settings cannot be saved.');
  if(state.islandNotifications?.error==='load'||state.islandNotifications?.loaded==='failed')return t('通知设置读取失败，请重试；不会自动开启提醒','Notification settings could not be loaded. Reminders will not be enabled automatically.');
  if(status==='error'||state.islandNotifications?.error)return t('设置未保存，请重试；原有配置已保留','Settings could not be saved. Your previous preferences were kept.');
  return '';
}
function connection(state){
  const p=preferenceOf(state),r=state.islandNotifications?.runtime;
  if(!r)return {tag:t('待接入','Not connected'),text:t('等待桌面宿主提供通知状态。','Waiting for notification status from the desktop host.')};
  if(!r.islandEnabled)return {tag:t('已暂停','Paused'),text:t('开启灵动岛后，再接收已选择的消息提醒。','Turn on the island to receive alerts from the selected apps.')};
  if(!p.messagesQQ&&!p.messagesWeChat)return {tag:t('选择应用','Choose an app'),text:t('选择 QQ 或微信后开始接入。','Select QQ or WeChat to connect alerts.')};
  if(p.messagesQQ&&r.qq==='attention'&&p.messagesWeChat&&r.wechat==='error')return {tag:t('部分已开启','Partly connected'),text:t('QQ 注意提醒已开启；微信暂时连接失败，稍后会自动重试。','QQ attention alerts are on. WeChat could not connect and will retry automatically.')};
  if(!p.messagesQQ&&r.wechat==='starting')return {tag:t('正在连接','Connecting'),text:t('正在连接微信任务栏注意提示。','Connecting to WeChat taskbar attention alerts.')};
  if(p.messagesQQ){
    if(r.qq==='package-required')return {tag:t('QQ 通知暂不可用','QQ unavailable'),text:t('Windows 通知与应用提醒暂不可用，请稍后重试。','Windows notifications and app attention alerts are unavailable. Please try again later.')};
    if(r.qq==='access-required'||r.qq==='requesting')return {tag:t('正在连接','Connecting'),text:t('正在连接 QQ 应用提醒。','Connecting to QQ app alerts.')};
    if(r.qq==='denied')return {tag:t('QQ 暂不可用','QQ unavailable'),text:t('QQ 消息提醒暂不可用。','QQ message alerts are unavailable.')};
    if(r.qq==='error')return {tag:t('QQ 暂不可用','QQ unavailable'),text:t('Windows 通知连接失败，稍后会自动重试。','Could not connect to Windows notifications. Retrying automatically.')};
    if(r.qq==='attention')return {tag:t('仅应用提醒','App alerts only'),text:p.messagesWeChat&&r.wechat==='listening'?t('QQ 或微信有新提醒时显示在灵动岛上，不显示消息摘要。','QQ and WeChat alerts appear on the island without message previews.'):t('QQ 有新提醒时显示在灵动岛上，不显示消息摘要。','QQ alerts appear on the island without message previews.')};
  }
  if(p.messagesWeChat&&r.wechat==='error')return {tag:t('微信暂不可用','WeChat unavailable'),text:t('暂时无法订阅微信的任务栏注意提示。','WeChat taskbar attention alerts could not be subscribed to.')};
  return {tag:t('接收已开启','Listening'),text:p.messagesQQ&&p.messagesWeChat?t('QQ 接收系统通知；微信仅提示应用有新提醒。','QQ uses Windows notifications; WeChat signals that the app needs attention.'):p.messagesQQ?t('接收 QQ 发送到 Windows 的通知。','Receiving notifications that QQ submits to Windows.'):t('接收微信任务栏注意提示，不读取聊天正文。','Listening for WeChat taskbar attention alerts without reading chat content.')};
}
function syncConnection(page,state){
  const info=connection(state),r=state.islandNotifications?.runtime,p=preferenceOf(state);
  page.querySelector('[data-island-notification-connection-tag]').textContent=info.tag;
  page.querySelector('[data-island-notification-connection-text]').textContent=info.text;
  const subtitles=page.querySelectorAll('.in-module__head p');
  if(r){
    subtitles[0].textContent=careEnabled(p)&&r.care==='island-off'?t('开启灵动岛后开始提醒','Turn on the island to start reminders'):t('让专注的日常，也有适时的休息','Small pauses for a more comfortable day');
    subtitles[1].textContent=weatherEnabled(p)?(r.weather==='island-off'?t('开启灵动岛后开始提醒','Turn on the island to start reminders'):r.weather==='waiting'?t('等待所选城市的最新天气','Waiting for fresh weather for your selected city'):t('根据所选城市的天气变化提醒','Alerts for weather changes in your selected city')):t('天气有变化，出门更从容','Stay a step ahead of changing weather');
  }
}
export function islandNotificationsPage(state){
  const p=preferenceOf(state),disabled=busy(state);
  const waterEditor=editPanel('water',numberField('waterIntervalMinutes',t('提醒间隔','Reminder interval'),t('分钟','min'),p)+numberField('waterGoal',t('每日目标','Daily goal'),t('杯','cups'),p));
  const standEditor=editPanel('stand',numberField('standIntervalMinutes',t('提醒间隔','Reminder interval'),t('分钟','min'),p));
  const sleepEditor=editPanel('sleep',timeField('sleepTime',t('就寝时间','Bedtime'),p)+numberField('sleepLeadMinutes',t('提前提醒','Remind before'),t('分钟','min'),p));
  const offWorkEditor=editPanel('off-work',timeField('offWorkTime',t('下班时间','End-of-work time'),p)+offWorkStyleField(p));
  const temperatureEditor=editPanel('temperature',numberField('temperatureDropDegrees',t('降温幅度','Temperature drop'),t('°C','°C'),p));
  const status=noticeStatus(state);
  return [
    '<div class="island-notifications" data-island-notifications>',
    '<header class="in-page-head"><div><h1 class="settings-component-title">'+t('实时通知','Notifications')+'</h1></div></header>',
    '<div class="in-layout"><div class="in-column">',
    '<section class="in-module" data-island-notification-group="care" aria-label="'+t('日常关怀','Daily care')+'">',
    moduleHead(t('日常关怀','Daily care'),''),
    '<div class="in-card">',
    row({id:'water',key:'waterEnabled',label:t('喝水','Drink water'),hint:t('忙碌时，也记得补充水分','Make time to stay hydrated'),group:'care',p,disabled,editor:waterEditor}),
    row({id:'stand',key:'standEnabled',label:t('久坐','Take a break'),hint:t('起身走走，舒展一下','Stand up and stretch for a moment'),group:'care',p,disabled,editor:standEditor}),
    row({id:'sleep',key:'sleepEnabled',label:t('睡眠','Bedtime'),hint:t('留一点时间，慢慢结束一天','A gentle reminder to wind down'),group:'care',p,disabled,editor:sleepEditor}),
    row({id:'off-work',key:'offWorkEnabled',label:t('下班','End of work'),hint:t('到点收尾，安心下班','Wrap up and enjoy your evening'),group:'care',p,disabled,editor:offWorkEditor}),
    '</div></section>',
    '<section class="in-module" data-island-notification-group="weather" aria-label="'+t('天气','Weather')+'">',
    moduleHead(t('天气','Weather'),''),
    '<div class="in-card">',
    row({id:'severe',key:'weatherSevere',label:t('恶劣天气','Severe weather'),hint:t('留意较强降水、降雪和雷雨','Watch for heavy rain, snow and thunderstorms'),group:'weather',p,disabled}),
    row({id:'rain',key:'weatherRain',label:t('降雨','Rain'),hint:t('下雨前，提醒带伞','Remember an umbrella before it rains'),group:'weather',p,disabled}),
    row({id:'temperature',key:'weatherTemperatureDrop',label:t('降温','Temperature drop'),hint:t('气温下降，记得添衣','A little notice when the weather cools'),group:'weather',p,disabled,editor:temperatureEditor}),
    '</div></section></div>',
    '<section class="in-module in-module--messages" data-island-notification-group="messages" aria-label="'+t('消息通知','Messages')+'">',
    moduleHead(t('消息通知','Messages'),''),
    '<div class="in-card">',
    row({id:'qq',key:'messagesQQ',label:t('QQ 通知','QQ notifications'),hint:t('接收 QQ 的系统通知或注意提示','QQ system notifications or attention alerts'),group:'messages',p,disabled}),
    row({id:'wechat',key:'messagesWeChat',label:t('微信提醒','WeChat alerts'),hint:t('仅提示应用有新提醒，不读取聊天正文','App attention alerts without reading chat content'),group:'messages',p,disabled}),
    row({id:'preview',key:'messagePreview',label:t('显示消息摘要','Show message preview'),hint:t('仅对支持内容预览的通知生效，默认关闭','For notifications that support previews; off by default'),group:'messages',p,disabled}),
    '<div class="in-connection"><span class="in-connection__symbol">'+icons.bell+'</span><div><div class="in-connection__heading"><span>'+t('消息状态','Message status')+'</span><span class="in-state-tag" data-island-notification-connection-tag>'+h(connection(state).tag)+'</span></div><p data-island-notification-connection-text>'+h(connection(state).text)+'</p></div></div></div>',
    '<div class="in-preview" aria-label="'+t('通知样式示例','Notification appearance example')+'"><div class="in-preview__label"><span>'+t('在灵动岛上','On your island')+'</span><span>'+t('样式预览','Appearance preview')+'</span></div><div class="in-preview__stage"><div class="in-preview__pill"><span class="in-preview__app">'+icons.bell+'</span><div><strong>'+t('有一条新消息','You have a new message')+'</strong><span>'+t('轻点打开对应应用','Open the app with a click')+'</span></div><span class="in-preview__dot"></span></div></div></div></section>',
    '</div>',
    '<p class="in-status'+(state.islandNotificationsStatus==='error'?' is-error':'')+'" data-island-notification-status role="status" aria-live="polite"'+(status?'':' hidden')+'>'+h(status)+'</p></div>',
  ].join('');
}
export function syncIslandNotifications(root,state){
  const page=root.querySelector('[data-island-notifications]');if(!page)return;
  const p=preferenceOf(state),saving=busy(state);
  page.querySelectorAll('[data-island-notification-key]').forEach(control=>{
    const key=control.dataset.islandNotificationKey,inactive=saving||inactiveKey(key,p);
    if(control.getAttribute('role')==='switch'){control.setAttribute('aria-checked',String(!!p[key]));control.classList.toggle('checked',!!p[key]);}
    else {
      const pending=control.hasAttribute('data-notification-submitted');
      const editing=control.ownerDocument.activeElement===control&&control.hasAttribute('data-notification-editing');
      if(pending&&state.islandNotificationsStatus!=='saving'){
        control.removeAttribute('data-notification-submitted');control.removeAttribute('data-notification-editing');control.value=String(p[key]);
      } else if(!editing&&!pending)control.value=String(p[key]);
    }
    syncSettingsControlBusy(control,inactive);
  });
  page.querySelectorAll('[data-island-notification-summary]').forEach(label=>{label.textContent=summary(p,label.dataset.islandNotificationSummary);});
  page.querySelectorAll('[data-island-notification-editor]').forEach(button=>{
    const key={water:'waterIntervalMinutes',stand:'standIntervalMinutes',sleep:'sleepTime','off-work':'offWorkTime',temperature:'temperatureDropDegrees'}[button.dataset.islandNotificationEditor];
    syncSettingsControlBusy(button,saving||inactiveKey(key,p));
    if(inactiveKey(key,p)){button.setAttribute('aria-expanded','false');const panel=page.querySelector('[data-island-notification-panel="'+button.dataset.islandNotificationEditor+'"]');if(panel)panel.hidden=true;}
  });
  const status=page.querySelector('[data-island-notification-status]'),text=noticeStatus(state);
  status.textContent=text;status.hidden=!text;status.classList.toggle('is-error',state.islandNotificationsStatus==='error');
  syncConnection(page,state);
}
export function mountIslandNotifications(root,{getState,send}){
  const enabledControl=control=>control&&!control.disabled&&control.getAttribute('aria-disabled')!=='true'&&!busy(getState());
  const submit=(key,value)=>{
    const state=getState(),p=preferenceOf(state);
    if(busy(state)||!validIslandNotificationValue(key,value)||inactiveKey(key,p)||p[key]===value)return false;
    send({version:1,scope:'island-notifications',action:'set',key,value});
    return true;
  };
  const click=event=>{
    const editor=event.target.closest('[data-island-notification-editor]');
    if(editor&&root.contains(editor)&&enabledControl(editor)){
      const panel=root.querySelector('[data-island-notification-panel="'+editor.dataset.islandNotificationEditor+'"]');
      const opening=panel.hidden;panel.hidden=!opening;editor.setAttribute('aria-expanded',String(opening));if(opening)(panel.querySelector('[data-settings-choice-trigger]')||panel.querySelector('input'))?.focus();return;
    }
    const control=event.target.closest('[data-island-notification-key][role="switch"]');
    if(control&&root.contains(control)&&enabledControl(control)){const p=preferenceOf(getState());submit(control.dataset.islandNotificationKey,!p[control.dataset.islandNotificationKey]);}
  };
  const change=event=>{
    const control=event.target.closest('input[data-island-notification-key],select[data-island-notification-key]');
    if(!control||!root.contains(control)||!enabledControl(control))return;
    const key=control.dataset.islandNotificationKey,value=limits[key]?Number(control.value):control.value;
    control.removeAttribute('data-notification-editing');
    if(!control.value.trim()||!validIslandNotificationValue(key,value)){control.value=String(preferenceOf(getState())[key]);return;}
    control.setAttribute('data-notification-submitted','');
    if(!submit(key,value))control.removeAttribute('data-notification-submitted');
  };
  const input=event=>{
    const control=event.target.closest('input[data-island-notification-key]');
    if(control&&root.contains(control)&&enabledControl(control))control.setAttribute('data-notification-editing','');
  };
  const keydown=event=>{
    if(event.key!=='Escape')return;
    const panel=event.target.closest('[data-island-notification-panel]');if(!panel||!root.contains(panel))return;
    panel.hidden=true;const button=root.querySelector('[data-island-notification-editor="'+panel.dataset.islandNotificationPanel+'"]');button?.setAttribute('aria-expanded','false');button?.focus();event.preventDefault();
  };
  root.addEventListener('click',click);root.addEventListener('input',input);root.addEventListener('change',change);root.addEventListener('keydown',keydown);
  syncIslandNotifications(root,getState());
  return ()=>{root.removeEventListener('click',click);root.removeEventListener('input',input);root.removeEventListener('change',change);root.removeEventListener('keydown',keydown);};
}
