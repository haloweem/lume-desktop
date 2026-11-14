export const defaultIslandNotificationPreferences=()=>({
  waterEnabled:false,waterIntervalMinutes:60,waterGoal:8,
  standEnabled:false,standIntervalMinutes:60,sleepEnabled:false,sleepTime:'23:30',sleepLeadMinutes:30,
  offWorkEnabled:false,offWorkTime:'18:00',offWorkStyle:'default',
  weatherSevere:false,weatherRain:false,weatherTemperatureDrop:false,temperatureDropDegrees:5,
  messagesQQ:false,messagesWeChat:false,messagePreview:false,
});
const defaults=defaultIslandNotificationPreferences();
const ranges={waterIntervalMinutes:[15,240],waterGoal:[1,20],standIntervalMinutes:[15,240],sleepLeadMinutes:[0,120],temperatureDropDegrees:[1,15]};
const exact=(value,keys)=>!!value&&typeof value==='object'&&!Array.isArray(value)&&Object.keys(value).length===keys.length&&keys.every(key=>Object.hasOwn(value,key));
export function validIslandNotificationValue(key,value){
  if(!Object.hasOwn(defaults,key))return false;
  if(typeof defaults[key]==='boolean')return typeof value==='boolean';
  if(key==='sleepTime'||key==='offWorkTime')return typeof value==='string'&&/^(?:[01]\d|2[0-3]):[0-5]\d$/.test(value);
  if(key==='offWorkStyle')return value==='default'||value==='squidward';
  const range=ranges[key];return !!range&&Number.isInteger(value)&&value>=range[0]&&value<=range[1];
}
export function validIslandNotificationCommand(action,fields={}){
  if(['snapshot','reset','authorize','open-system-settings'].includes(action))return exact(fields,[]);
  return action==='set'&&exact(fields,['key','value'])&&validIslandNotificationValue(fields.key,fields.value);
}
export function normalizeIslandNotificationPreferences(value){
  const keys=Object.keys(defaults),masters=['careEnabled','weatherEnabled','messagesEnabled'];
  const legacy=[...keys.filter(key=>key!=='offWorkStyle'),...masters];
  const older=legacy.filter(key=>key!=='offWorkEnabled'&&key!=='offWorkTime');
  const current=exact(value,keys);
  if(!current&&!exact(value,legacy)&&!exact(value,older))return null;
  if(!Object.entries(value).every(([key,entry])=>masters.includes(key)?typeof entry==='boolean':validIslandNotificationValue(key,entry)))return null;
  const normalized={...defaults};
  for(const key of keys)if(Object.hasOwn(value,key))normalized[key]=value[key];
  if(!current){
    if(!value.careEnabled)for(const key of ['waterEnabled','standEnabled','sleepEnabled','offWorkEnabled'])normalized[key]=false;
    if(!value.weatherEnabled)for(const key of ['weatherSevere','weatherRain','weatherTemperatureDrop'])normalized[key]=false;
    if(!value.messagesEnabled)normalized.messagesQQ=normalized.messagesWeChat=false;
  }
  return normalized;
}
export function validIslandNotificationState(m){
  const keys=['version','type','preferences','revision','loaded','result','saved','error','availability','receiving'];
  const runtime=m?.runtime;
  const preferences=normalizeIslandNotificationPreferences(m?.preferences);
  // Current native hosts report the QQ subscription mode. Older hosts omit it.
  // Keep both exact schemas, so an added unknown field is still rejected.
  const realtime=!!runtime&&Object.hasOwn(runtime,'qqRealtime');
  const runtimeKeys=['islandEnabled','care','weather','qq','wechat'];
  const runtimeValid=runtime===undefined||exact(runtime,realtime?[...runtimeKeys,'qqRealtime']:runtimeKeys)&&
    typeof runtime.islandEnabled==='boolean'&&['off','active','island-off'].includes(runtime.care)&&
    ['off','active','waiting','island-off'].includes(runtime.weather)&&
    ['disabled','package-required','access-required','denied','requesting','connected','attention','error'].includes(runtime.qq)&&
    ['disabled','starting','listening','error'].includes(runtime.wechat)&&
    (!realtime||typeof runtime.qqRealtime==='boolean'&&(!runtime.qqRealtime||
      runtime.islandEnabled&&preferences?.messagesQQ))&&
    (!['connected','attention'].includes(runtime.qq)||runtime.islandEnabled&&preferences?.messagesQQ)&&
    (runtime.wechat!=='listening'||runtime.islandEnabled&&preferences?.messagesWeChat);
  return !!m&&exact(m,runtime===undefined?keys:[...keys,'runtime'])&&runtimeValid&&
    m.version===1&&m.type==='island-notifications-state'&&
    !!preferences&&
    Number.isSafeInteger(m.revision)&&m.revision>=0&&['fresh','loaded','failed'].includes(m.loaded)&&
    ['snapshot','applied'].includes(m.result)&&typeof m.saved==='boolean'&&['','load','save','invalid'].includes(m.error)&&
    ['not-connected','ready'].includes(m.availability)&&typeof m.receiving==='boolean'&&
    (runtime===undefined?m.availability==='not-connected'&&!m.receiving:
      m.receiving===(runtime.islandEnabled&&(['connected','attention'].includes(runtime.qq)&&preferences.messagesQQ||runtime.wechat==='listening'&&preferences.messagesWeChat))&&
      m.availability===(m.receiving?'ready':'not-connected'))&&
    (!m.saved||(m.result==='applied'&&!m.error&&m.loaded==='loaded'));
}
