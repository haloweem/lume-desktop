import test from 'node:test';
import assert from 'node:assert/strict';
import {defaultIslandNotificationPreferences,normalizeIslandNotificationPreferences,validIslandNotificationCommand,validIslandNotificationState} from '../island-notifications-protocol.js';
import {createBridge,validHostMessage} from '../bridge.js';
import {initialState,reduce} from '../state.js';
import {islandPage} from '../dynamic-island.js';
import {mountIslandNotifications} from '../island-notifications.js';
import {setLanguage} from '../localization.js';

const snapshot=(preferences={},fields={})=>({version:1,type:'island-notifications-state',
  preferences:{...defaultIslandNotificationPreferences(),...preferences},revision:0,loaded:'fresh',
  result:'snapshot',saved:false,error:'',availability:'not-connected',receiving:false,...fields});
const legacyPreferences=(values={},offWork=true)=>{
  const p={...defaultIslandNotificationPreferences(),careEnabled:false,weatherEnabled:false,messagesEnabled:false,
    waterEnabled:true,standEnabled:true,weatherSevere:true,weatherRain:true,weatherTemperatureDrop:true,...values};
  delete p.offWorkStyle;
  if(!offWork){delete p.offWorkEnabled;delete p.offWorkTime;}
  return p;
};
const runtime=(values={})=>({islandEnabled:true,care:'off',weather:'off',qq:'disabled',wechat:'disabled',qqRealtime:false,...values});
const connected=(preferences={},fields={})=>snapshot({messagesQQ:true,...preferences},
  {runtime:runtime({qq:'connected',qqRealtime:true}),receiving:true,availability:'ready',...fields});

test('every reminder is opt-in and the current eighteen-field schema has no master switches',()=>{
  const p=initialState().islandNotifications.preferences;
  for(const [key,value] of Object.entries(p))if(typeof value==='boolean')assert.equal(value,false,key);
  assert.equal(p.offWorkTime,'18:00');assert.equal(p.offWorkStyle,'default');
  assert.equal(Object.keys(p).length,18);
  for(const key of ['careEnabled','weatherEnabled','messagesEnabled'])assert.equal(Object.hasOwn(p,key),false);
  assert.ok(validHostMessage(snapshot()));
  assert.equal(validIslandNotificationState(snapshot({}, {receiving:true})),false);
  assert.equal(validIslandNotificationState(snapshot({}, {unexpected:true})),false);
});
test('legacy eighteen and twenty-field snapshots migrate only effective enabled children',()=>{
  for(const offWork of [false,true])for(const careEnabled of [false,true])for(const weatherEnabled of [false,true])for(const messagesEnabled of [false,true]){
    const p=legacyPreferences({careEnabled,weatherEnabled,messagesEnabled,sleepEnabled:true,offWorkEnabled:true,
      offWorkTime:'19:15',messagesQQ:true,messagesWeChat:true,messagePreview:true},offWork);
    const source=snapshot({}, {preferences:p});
    assert.equal(Object.keys(p).length,offWork?20:18);assert.ok(validHostMessage(source));
    const state=reduce(initialState(),{type:'host',message:source}),migrated=state.islandNotifications.preferences;
    assert.equal(state.islandNotificationsStatus,'ready');assert.equal(Object.keys(migrated).length,18);
    for(const key of ['waterEnabled','standEnabled','sleepEnabled'])assert.equal(migrated[key],careEnabled,key);
    assert.equal(migrated.offWorkEnabled,offWork&&careEnabled);
    assert.equal(migrated.offWorkTime,offWork?'19:15':'18:00');assert.equal(migrated.offWorkStyle,'default');
    for(const key of ['weatherSevere','weatherRain','weatherTemperatureDrop'])assert.equal(migrated[key],weatherEnabled,key);
    for(const key of ['messagesQQ','messagesWeChat'])assert.equal(migrated[key],messagesEnabled,key);
    assert.equal(migrated.messagePreview,true,'preview preference is retained even when its old master was off');
    assert.equal(Object.hasOwn(p,'careEnabled'),true,'normalization does not mutate the source');
  }
});
test('migration preserves connected QQ and independent child opt-outs',()=>{
  const old=legacyPreferences({careEnabled:true,waterEnabled:false,messagesEnabled:true,messagesQQ:true,messagePreview:true});
  const message=connected({}, {preferences:old});assert.ok(validHostMessage(message));
  const state=reduce(initialState(),{type:'host',message});
  assert.equal(state.islandNotifications.preferences.messagesQQ,true);assert.equal(state.islandNotifications.preferences.waterEnabled,false);
  assert.equal(state.islandNotifications.runtime.qqRealtime,true);assert.equal(state.islandNotifications.receiving,true);
  const disabled=structuredClone(message);disabled.preferences.messagesEnabled=false;
  assert.equal(validHostMessage(disabled),false,'an old disabled master cannot claim an active receiver');
});
test('partial, hybrid and invalid preference schemas are rejected',()=>{
  for(const key of ['offWorkEnabled','offWorkTime','offWorkStyle','messagesQQ']){
    const partial=snapshot();delete partial.preferences[key];
    assert.equal(validHostMessage(partial),false,key);assert.equal(normalizeIslandNotificationPreferences(partial.preferences),null);
  }
  assert.equal(validHostMessage(snapshot({careEnabled:true})),false);
  for(const offWorkTime of ['24:00','18:60','18:0','8:00','18:00:00',' 18:00',null,1800])
    assert.equal(validHostMessage(snapshot({offWorkTime})),false,String(offWorkTime));
  for(const offWorkStyle of ['other','Squidward',true,null,1])assert.equal(validHostMessage(snapshot({offWorkStyle})),false);
  assert.equal(validHostMessage(snapshot({offWorkEnabled:'false'})),false);assert.equal(validHostMessage(snapshot({unknown:false})),false);
  for(const offWorkTime of ['00:00','18:00','23:59'])assert.ok(validHostMessage(snapshot({offWorkTime})));
  assert.ok(validHostMessage(snapshot({offWorkStyle:'squidward'})));
  const partialLegacy=legacyPreferences();delete partialLegacy.offWorkTime;
  assert.equal(validHostMessage(snapshot({}, {preferences:partialLegacy})),false);
});
test('direct child settings and style validate before crossing the bridge; master commands are retired',()=>{
  const sent=[],bridge=createBridge({postMessage:m=>sent.push(m)});assert.ok(bridge.islandNotifications('snapshot'));
  for(const [key,value] of [['messagesQQ',true],['waterEnabled',true],['sleepTime','23:30'],['offWorkEnabled',true],['offWorkTime','18:45'],['offWorkStyle','squidward']])
    assert.ok(bridge.islandNotifications('set',{key,value}));
  for(const [key,value] of [['careEnabled',true],['weatherEnabled',false],['messagesEnabled',true],['messagesQQ','true'],['waterIntervalMinutes',0],['waterGoal',21],['sleepTime','24:00'],['offWorkEnabled',1],['offWorkTime','24:00'],['offWorkStyle','other'],['temperatureDropDegrees',16],['permission','allowed']])
    assert.equal(bridge.islandNotifications('set',{key,value}),false,key);
  assert.equal(validIslandNotificationCommand('reset',{messagesQQ:true}),false);assert.equal(bridge.islandNotifications('authorize',{permission:'allowed'}),false);
  assert.equal(sent.length,7);assert.deepEqual(sent[1],{version:1,scope:'island-notifications',action:'set',key:'messagesQQ',value:true});
});
test('off-work enable, time and style wait for confirmation and restore confirmed values after failures',()=>{
  let state=reduce(initialState(),{type:'host',message:snapshot({messagesQQ:true})});
  state=reduce(state,{type:'request',scope:'island-notifications',key:'set'});assert.equal(state.islandNotifications.preferences.offWorkEnabled,false);
  state=reduce(state,{type:'host',message:snapshot({messagesQQ:true},{result:'applied',error:'save',revision:1})});
  assert.equal(state.islandNotificationsStatus,'error');assert.equal(state.islandNotifications.preferences.offWorkEnabled,false);
  state=reduce(state,{type:'host',message:snapshot({messagesQQ:true,offWorkEnabled:true},{result:'applied',saved:true,loaded:'loaded',revision:2})});
  state=reduce(state,{type:'request',scope:'island-notifications',key:'set'});
  state=reduce(state,{type:'host',message:snapshot({messagesQQ:true,offWorkEnabled:true},{result:'applied',error:'save',revision:3})});
  assert.equal(state.islandNotifications.preferences.offWorkTime,'18:00');assert.equal(state.islandNotifications.preferences.offWorkStyle,'default');
  const saved=snapshot({messagesQQ:true,offWorkEnabled:true,offWorkTime:'18:45',offWorkStyle:'squidward'},{result:'applied',saved:true,loaded:'loaded',revision:4});
  state=reduce(initialState(),{type:'host',message:saved});
  assert.equal(state.islandNotifications.preferences.offWorkStyle,'squidward');assert.equal(state.islandNotifications.preferences.offWorkTime,'18:45');
  assert.equal(state.islandNotifications.preferences.messagesQQ,true);assert.equal(reduce(state,{type:'host',message:snapshot({}, {revision:3})}),state);
});
test('real island route keeps three sections with no title master switches',()=>{
  const page=islandPage(reduce(initialState(),{type:'host',message:snapshot()}),'notifications');
  for(const text of ['日常关怀','天气','消息通知','喝水','久坐','睡眠','下班','下班时间','QQ','微信'])assert.ok(page.includes(text),text);
  assert.match(page,/notifications-selected-24.png/);assert.doesNotMatch(page,/data-island-notification-key="(?:careEnabled|weatherEnabled|messagesEnabled)"/);
  assert.doesNotMatch(page,/in-module is-disabled|island-editor__empty|data-island-notifications-reset|in-module-note|in-footnote/);
});
test('off-work follows sleep and exposes default and Squidward styles in both languages',()=>{
  const state=reduce(initialState(),{type:'host',message:snapshot({offWorkEnabled:true,offWorkStyle:'squidward'})});
  try{
    for(const [language,label,timeLabel,styleLabel] of [['zh-CN','下班','下班时间','章鱼哥'],['en','End of work','End-of-work time','Squidward']]){
      setLanguage(language);const page=islandPage(state,'notifications');
      assert.ok(page.indexOf('data-island-notification-row="offWorkEnabled"')>page.indexOf('data-island-notification-row="sleepEnabled"'));
      for(const text of [label,timeLabel,styleLabel])assert.ok(page.includes(text),text);
      assert.match(page,/<option value="default">/);assert.match(page,/<option value="squidward" selected>/);
      assert.match(page,/data-island-notification-summary="off-work">18:00/);
      assert.doesNotMatch(page,/in-off-work-style-info|data-island-off-work-avatar|data-island-off-work-style-description|off-work-squidward\.png|<audio|试听|Preview voice/);
    }
  }finally{setLanguage('zh-CN');}
});
test('current QQ realtime snapshots reach the bridge and reducer with only the direct QQ switch',()=>{
  let listener,received=0,state=initialState();
  const bridge=createBridge({addEventListener(type,callback){assert.equal(type,'message');listener=callback;},removeEventListener(){}});
  const unsubscribe=bridge.subscribe(message=>{++received;state=reduce(state,{type:'host',message});});
  listener({data:snapshot({}, {runtime:runtime({islandEnabled:false})})});assert.equal(state.islandNotificationsStatus,'ready');
  listener({data:connected({}, {revision:1})});assert.equal(state.islandNotifications.runtime.qqRealtime,true);assert.equal(state.islandNotifications.receiving,true);
  state=reduce(state,{type:'request',scope:'island-notifications',key:'set'});
  listener({data:connected({waterEnabled:true},{runtime:runtime({care:'active',qq:'connected',qqRealtime:true}),revision:2,result:'applied',loaded:'loaded',saved:true})});
  assert.equal(received,3);assert.equal(state.islandNotifications.preferences.waterEnabled,true);unsubscribe();
});
test('QQ realtime metadata stays optional for older hosts and exact for current hosts',()=>{
  const oldRuntime=runtime({qq:'connected'});delete oldRuntime.qqRealtime;
  assert.ok(validHostMessage(connected({}, {runtime:oldRuntime})));
  for(const qqRealtime of [false,true])assert.ok(validHostMessage(connected({}, {runtime:{...oldRuntime,qqRealtime}})));
  for(const qqRealtime of [undefined,null,0,1,'true',{},[]])assert.equal(validHostMessage(connected({}, {runtime:{...oldRuntime,qqRealtime}})),false);
  assert.equal(validHostMessage(connected({}, {runtime:{...oldRuntime,qqRealtime:true,unexpected:true}})),false);
  assert.equal(validHostMessage(snapshot({}, {runtime:runtime({qqRealtime:true})})),false);
});
test('runtime receiving requires the selected application and island, never invented authorization',()=>{
  assert.ok(validIslandNotificationState(connected()));
  assert.ok(validIslandNotificationState(snapshot({messagesWeChat:true},{runtime:runtime({wechat:'listening'}),receiving:true,availability:'ready'})));
  for(const value of [connected({messagesQQ:false}),connected({}, {runtime:runtime({islandEnabled:false,qq:'connected'})}),connected({}, {receiving:false}),connected({}, {availability:'not-connected'}),connected({}, {runtime:runtime({qq:'authorized'})})])
    assert.equal(validIslandNotificationState(value),false);
  let state=reduce(initialState(),{type:'host',message:connected({}, {revision:3})});assert.match(islandPage(state,'notifications'),/接收 QQ 发送到 Windows 的通知。/);
  state=reduce(state,{type:'host',message:snapshot({messagesQQ:true},{runtime:runtime({qq:'access-required'}),revision:4})});
  assert.equal(state.islandNotifications.receiving,false);assert.equal(state.islandNotifications.availability,'not-connected');
});
test('direct opt-in failure cannot claim reception and unrelated children stay usable',()=>{
  let state=reduce(initialState(),{type:'host',message:snapshot({}, {runtime:runtime()})});
  state=reduce(state,{type:'request',scope:'island-notifications',key:'set'});assert.equal(state.islandNotifications.preferences.messagesQQ,false);
  state=reduce(state,{type:'host',message:snapshot({}, {runtime:runtime(),result:'applied',error:'save',revision:1})});
  assert.equal(state.islandNotificationsStatus,'error');assert.equal(state.islandNotifications.receiving,false);
  state=reduce(state,{type:'host',message:snapshot({messagesQQ:true},{runtime:runtime({qq:'access-required'}),result:'applied',saved:true,loaded:'loaded',revision:2})});
  assert.equal(state.islandNotifications.preferences.messagesQQ,true);assert.equal(state.islandNotifications.receiving,false);
  assert.match(islandPage(state,'notifications'),/data-island-notification-key="waterEnabled"><span/);
});
test('notification pages omit permission controls and ignore stale access clicks',()=>{
  let state=reduce(initialState(),{type:'host',message:snapshot({messagesQQ:true},{runtime:runtime({qq:'access-required'})})});
  const handlers=new Map(),sent=[];
  const root={querySelector:()=>null,contains:()=>true,addEventListener:(name,handler)=>handlers.set(name,handler),removeEventListener:name=>handlers.delete(name)};
  const unmount=mountIslandNotifications(root,{getState:()=>state,send:command=>sent.push(command)});assert.deepEqual(sent,[]);
  const access={hidden:false,disabled:false,getAttribute:()=>null},event={target:{closest:selector=>selector==='[data-island-notification-access]'?access:null}};
  handlers.get('click')(event);assert.deepEqual(sent,[]);
  for(const qq of ['access-required','requesting','denied','attention']) {
    const active=qq==='attention';
    const value=reduce(initialState(),{type:'host',message:snapshot({messagesQQ:true},{runtime:runtime({qq}),receiving:active,availability:active?'ready':'not-connected'})});
    assert.doesNotMatch(islandPage(value,'notifications'),/data-island-notification-access|等待授权|未授权|授权窗口|允许通知访问|打开系统设置/);
  }
  state=reduce(state,{type:'host',message:connected({}, {revision:1})});handlers.get('click')(event);
  state=reduce(state,{type:'host',message:snapshot({}, {runtime:runtime(),revision:2})});handlers.get('click')(event);
  assert.equal(sent.length,0);unmount();assert.equal(handlers.size,0);
});
