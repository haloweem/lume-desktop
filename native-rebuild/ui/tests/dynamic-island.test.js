import test from 'node:test';
import assert from 'node:assert/strict';
import {createBridge,validHostMessage} from '../bridge.js';
import {initialState,reduce} from '../state.js';
import {modules} from '../catalog.js';
import {catalog,search} from '../pages.js';
import {islandPage,validIslandState} from '../dynamic-island.js';
import {setLanguage} from '../localization.js';
import {compactWidgets,compactWidgetPicker} from '../compact-widgets.js';
const snapshot=(fields={})=>({version:1,type:'dynamicIsland-state',enabled:false,running:false,locked:false,
  expanded:false,focusing:false,finished:false,remaining:0,hideFullscreen:true,topmost:true,hover:false,autoHide:false,size:'standard',material:'none',
  loadStatus:'fresh',error:'',result:'snapshot',saved:false,...fields});

test('island appears immediately after Dock in the styles catalog and search',()=>{
  const state=initialState();const index=modules.findIndex(item=>item.id==='dock');
  assert.equal(modules[index+1].id,'dynamicIsland');
  const html=catalog(state,false);
  assert.ok(html.indexOf('data-open="dynamicIsland"')>html.indexOf('data-open="dock"'));
  assert.match(html,/data-action="dynamicIsland-toggle" disabled/);
  assert.match(search({...state,query:'灵动岛'}),/data-open="dynamicIsland"/);
});
test('island bridge rejects malformed commands and snapshots',()=>{
  const sent=[];const bridge=createBridge({postMessage:m=>sent.push(m)});
  assert.ok(validIslandState(snapshot()));assert.ok(validHostMessage(snapshot()));
  for(const fields of [{enabled:'true'},{remaining:1501},{remaining:NaN},{running:true},{countdownDate:2932897},{countdownStyle:'other'},{extra:true},
    {saved:true,result:'snapshot'},{loadStatus:'bad'},{error:'unknown'},{size:'huge'},{hover:1},{material:'glass'},{hideIdle:false},{shape:'circle'},{shape:1}])assert.equal(validHostMessage(snapshot(fields)),false);
  for(const command of [['enabled',true],['locked',false],['expand'],['collapse'],['focus'],['stopFocus'],['reset'],
    ['hideFullscreen',true],['topmost',false],['hover',true],['size','large'],['material','liquid-glass'],['autoHide',true],['shape','standard'],['shape','capsule'],['resetAppearance'],
    ['leftWidget',20],['rightWidget',14],['countdownStyle','violet']])
    assert.ok(bridge.send('dynamicIsland',...command));
  for(const command of [['enabled','true'],['focus',true],['locked',1],['material','glass'],['path','C:/anything'],['size','huge'],['hover','yes'],['hideIdle',false]])
    assert.equal(bridge.send('dynamicIsland',...command),false);
  for(const command of [['customIcons',true],['overview',true],['leftWidget',-1],['rightWidget',31],['leftWidget','4'],['leftWidget',18],['rightWidget',18],
    ['leftStyle','circle'],['rightStyle','capsule'],['card',0],['cardMove',0]])
    assert.equal(bridge.send('dynamicIsland',...command),false);
  assert.equal(bridge.send('dynamicIsland','shape','circle'),false);
  assert.equal(sent.length,19);
  assert.ok(validHostMessage({version:1,type:'navigate',page:'dynamicIsland'}));
});
test('native acknowledgement settles requests while preserving confirmed preferences on failure',()=>{
  let state=reduce(initialState(),{type:'request',scope:'dynamicIsland',key:'enabled',value:true});
  assert.equal(state.dynamicIslandStatus,'saving');assert.equal(state.dynamicIsland.enabled,false);
  state=reduce(state,{type:'host',message:snapshot()});assert.equal(state.dynamicIslandStatus,'saving');
  state=reduce(state,{type:'host',message:snapshot({enabled:true,running:true,result:'applied',saved:true,loadStatus:'loaded'})});
  assert.equal(state.dynamicIslandStatus,'saved');assert.equal(state.dynamicIsland.enabled,true);
  state=reduce(state,{type:'host',message:snapshot({enabled:true,running:true,error:'save',result:'applied'})});
  assert.equal(state.dynamicIslandStatus,'error');assert.equal(state.dynamicIsland.enabled,true);
  assert.equal(reduce(state,{type:'unavailable',scope:'all'}).dynamicIslandStatus,'unavailable');
});
test('island shows expanded widgets and style choices without a live or compact preview',()=>{
  const state={...initialState(),dynamicIslandStatus:'idle'};
  for(const language of ['zh-CN','en']){
    setLanguage(language);
    const home=islandPage(state,'home');
    assert.match(home,/settings--island/);
    assert.match(home,/data-tab="home"/);
    assert.match(home,/data-tab="settings"/);
    assert.match(islandPage(state,'todo'),/data-todo-board/,'the to-do tab hosts the shared board');
    assert.match(home,/data-tab="notifications"/,'notifications have a real settings page');
    for(const key of ['music','quick-launch','clock']){
      assert.doesNotMatch(home,new RegExp(`data-tab="${key}"`),'unfinished module tabs stay out of navigation');
      assert.match(islandPage(state,key),/island-editor__empty/,'a direct route keeps its pending state');
    }
    assert.match(home,/assets\/icons\/lume-rounded-v1\/home-selected-24.png/);
    assert.match(home,/data-island-home/);
    assert.match(home,/data-island-shape="standard"/);
    assert.match(home,/data-island-shape="capsule"/);
    const settings=islandPage(state);
    assert.doesNotMatch(settings,/dynamicIsland-(?:toggle|hover|expand|collapse)|island-preview|正在播放|Now playing/);
    assert.doesNotMatch(settings,/dynamicIsland-focus|25-minute|专注/);
    assert.doesNotMatch(settings,/dynamicIsland-(?:customIcons|overview|leftStyle|rightStyle|card)|data-island-card|多组件工作台/);
    assert.doesNotMatch(islandPage(state,'details'),/workspace|customiz|自选左右|组件排序/i);
    assert.match(islandPage(state,'details'),/音乐|music/i);
  }
  setLanguage('zh-CN');
  assert.equal(reduce(state,{type:'navigate',view:'dynamicIsland'}).view,'dynamicIsland');
  const active={...state,dynamicIsland:{...state.dynamicIsland,enabled:true,expanded:true}};
  assert.doesNotMatch(islandPage(active),/dynamicIsland-(?:toggle|hover|expand|collapse)|island-preview/);
  assert.doesNotMatch(islandPage(active),/dynamicIsland-hideIdle/);
  assert.match(islandPage(active),/data-action="dynamicIsland-autoHide"/);
  assert.doesNotMatch(islandPage(active),/data-action="dynamicIsland-hideFullscreen"/);
  assert.match(islandPage(active),/id="dynamicIsland-layer"/);
  assert.match(islandPage(active),/data-select-value="above"/);
  assert.doesNotMatch(islandPage(active),/全屏时隐藏/);
  assert.match(islandPage(active),/空闲时隐藏/);
  assert.doesNotMatch(islandPage(active),/无活动时自动隐藏/);
  assert.doesNotMatch(islandPage(active),/id="dynamicIsland-(?:size|material)"|data-island-preference/);
  const home=islandPage(active,'home');
  assert.equal(compactWidgets.length,31);
  assert.equal(compactWidgets[18],null,'retired wire ID stays reserved');
  assert.doesNotMatch(home,/data-island-compact|data-island-preview|data-island-native-preview/);
  assert.doesNotMatch(home,/data-island-compact-widget="18"|文件暂存|File tray/);
  const legacy=compactWidgetPicker({...state,dynamicIsland:{...state.dynamicIsland,leftWidget:18}});
  assert.match(legacy,/data-island-selection-sample data-widget="0"/,'legacy removed selection displays None');
  const captured={...state,islandCompactCapturedAt:new Date(2026,10,5,12).getTime()};
  const before=compactWidgetPicker({...captured,islandCompactReadings:[{id:14,text:'3%',progress:.03,available:true}]});
  const after=compactWidgetPicker({...captured,islandCompactReadings:[{id:14,text:'99%',progress:.99,available:true}]});
  assert.equal(before,after,'system chooser icons are independent of the captured readings');
  assert.match(before,/data-island-compact-sample="14"><svg[^]*?<rect/);
  assert.doesNotMatch(before,/48%|99%/);
  const missing=compactWidgetPicker(captured);
  assert.equal(missing,before,'system choices remain visible without sampler readings');
  assert.match(home,/data-island-expanded-panel>/);
  assert.match(home,/data-island-expanded-picker/);
  // The five released widgets share the picker; focus remains hidden.
  assert.match(home,/data-island-expanded-toggle="6"/);
  assert.equal((home.match(/data-island-expanded-toggle="\d+"/g)||[]).length,6);
  assert.match(home,/data-island-expanded-toggle="0"/);
  assert.match(home,/data-island-expanded-toggle="1"/);
  assert.match(home,/data-island-expanded-toggle="4"/);
  assert.match(home,/data-island-expanded-toggle="5"/);
  assert.match(home,/data-island-expanded-toggle="3"/);
  assert.doesNotMatch(home,/最多同时启用 5 个组件|Use the switches to enable/);
  assert.doesNotMatch(home,/最多同时启用 4 个组件|data-island-expanded-toggle="2"/);
  assert.doesNotMatch(home,/data-island-layout|data-island-membership|data-island-components-title/);
  for(const [key,values] of [['material',['none','liquid-glass','frosted-glass']],['size',['small','standard','large']]])
    for(const value of values)assert.match(home,new RegExp(`data-island-preference="${key}" data-value="${value}"`));
  assert.match(home,/data-action="dynamicIsland-resetAppearance"/);
  assert.match(home,/data-island-style-widget="clock"/);
  assert.match(home,/data-island-clock-style="simple"/);
  assert.match(home,/data-island-clock-style="progress"/);
  assert.match(home,/data-island-clock-style="digital"/);
  assert.match(home,/data-island-clock-style="date"/);
  assert.match(home,/data-island-clock-style="flip"/);
  assert.match(home,/data-island-clock-style="pixel"/);
  assert.equal((home.match(/data-island-clock-style=/g)||[]).length,6);
  assert.equal((home.match(/data-island-style-widget=/g)||[]).length,6);
  assert.match(home,/data-island-style-widget="countdown"/);
  assert.match(home,/assets\/icons\/island-expanded\/countdown\.svg/);
  assert.doesNotMatch(home,/data-island-style-widget="other"|暂无样式|VIP专享/);
  const countdown=islandPage({...state,islandStyleWidget:'countdown'},'home');
  assert.match(countdown,/data-island-style-title[^>]*>倒数日</);
  assert.doesNotMatch(countdown,/data-island-countdown-date/);
  assert.match(countdown,/island-countdown-preview__number/);
  assert.match(countdown,/data-island-countdown-style="standard"/);
  assert.match(countdown,/data-island-countdown-style="violet"/);
  assert.equal((countdown.match(/data-island-countdown-style=/g)||[]).length,2);
  assert.match(countdown,/默认样式/);
  assert.doesNotMatch(countdown,/标准样式/);
  assert.doesNotMatch(countdown,/纪念日|Anniversary|anniversary/);
  assert.match(countdown,/纯色样式/);
  assert.doesNotMatch(countdown,/紫色样式/);
  const unavailableStyles=islandPage({...state,dynamicIslandStatus:'unavailable',islandStyleWidget:'clock'},'home');
  assert.doesNotMatch(unavailableStyles,/data-island-clock-style="progress"[^>]*disabled/);
  const customized=islandPage({...state,dynamicIsland:{...state.dynamicIsland,countdownTitle:'用户自定义标题',countdownDate:21916},islandStyleWidget:'countdown'},'home');
  assert.match(customized,/新年/);
  assert.doesNotMatch(customized,/用户自定义标题/);
  assert.match(customized,/100/);
  assert.doesNotMatch(customized,/下个周年|♥|island-countdown-preview__anniversary-footer/);
  const updated=reduce(state,{type:'host',message:snapshot({hover:true,size:'small',topmost:false})});
  assert.equal(updated.dynamicIsland.size,'small');assert.equal(updated.dynamicIsland.material,'none');assert.equal(updated.dynamicIsland.hover,true);assert.equal(updated.dynamicIsland.topmost,false);
});
test('shape waits for native confirmation, survives rollback and accepts older snapshots',()=>{
  let state=reduce(initialState(),{type:'host',message:snapshot({shape:'capsule'})});
  state=reduce(state,{type:'request',scope:'dynamicIsland',key:'shape',value:'standard'});
  assert.equal(state.dynamicIsland.shape,'capsule');
  state=reduce(state,{type:'host',message:snapshot({shape:'standard',saved:true,result:'applied',loadStatus:'loaded'})});
  assert.equal(state.dynamicIsland.shape,'standard');assert.equal(state.dynamicIslandStatus,'saved');
  state=reduce(state,{type:'host',message:snapshot({shape:'standard',error:'save',result:'applied'})});
  assert.equal(state.dynamicIsland.shape,'standard');assert.equal(state.dynamicIslandStatus,'error');
  assert.equal(reduce(state,{type:'host',message:snapshot()}).dynamicIsland.shape,'capsule');
});
test('countdown style selection supports standard and solid color styles',()=>{
  let state=reduce(initialState(),{type:'host',message:snapshot()});
  assert.equal(state.dynamicIsland.countdownStyle,'standard');
  state=reduce(state,{type:'request',scope:'dynamicIsland',key:'countdownStyle',value:'violet'});
  assert.equal(state.dynamicIsland.countdownStyle,'standard');
  state=reduce(state,{type:'host',message:snapshot({countdownStyle:'violet',saved:true,result:'applied',loadStatus:'loaded'})});
  assert.equal(state.dynamicIsland.countdownStyle,'violet');
  assert.equal(validHostMessage(snapshot({countdownStyle:'anniversary'})),false,'removed style is rejected by the renderer');
  state=reduce(state,{type:'request',scope:'dynamicIsland',key:'countdownStyle',value:'standard'});
  state=reduce(state,{type:'host',message:snapshot({countdownStyle:'violet',error:'save',result:'applied',loadStatus:'loaded'})});
  assert.equal(state.dynamicIsland.countdownStyle,'violet');
  state=reduce(state,{type:'host',message:snapshot({countdownStyle:'standard',saved:true,result:'applied',loadStatus:'loaded'})});
  assert.equal(state.dynamicIsland.countdownStyle,'standard');
});

test('clock styles validate, wait for native acknowledgement and preserve confirmed style on failure',()=>{
  const sent=[],bridge=createBridge({postMessage:message=>sent.push(message)});
  for(const style of ['simple','progress','digital','date','flip','pixel']){
    assert.ok(bridge.send('dynamicIsland','clockStyle',style));
    assert.ok(validHostMessage(snapshot({clockStyle:style})));
  }
  for(const style of ['analog','',null,1,true]){
    assert.equal(bridge.send('dynamicIsland','clockStyle',style),false);
    assert.equal(validHostMessage(snapshot({clockStyle:style})),false);
  }
  assert.deepEqual(sent,['simple','progress','digital','date','flip','pixel'].map(style=>`v1|dynamicIsland|clockStyle|${style}`));
  let state=reduce(initialState(),{type:'host',message:snapshot()});
  assert.equal(state.dynamicIsland.clockStyle,'simple','older hosts retain the original style');
  state=reduce(state,{type:'request',scope:'dynamicIsland',key:'clockStyle',value:'progress'});
  assert.equal(state.dynamicIsland.clockStyle,'simple');
  assert.equal(state.dynamicIslandStatus,'saving');
  state=reduce(state,{type:'host',message:snapshot({clockStyle:'simple',error:'save',result:'applied'})});
  assert.equal(state.dynamicIsland.clockStyle,'simple');
  state=reduce(state,{type:'host',message:snapshot({clockStyle:'progress',saved:true,result:'applied',loadStatus:'loaded'})});
  assert.equal(state.dynamicIsland.clockStyle,'progress');
  assert.equal(state.dynamicIsland.expandedLayout,1,'style selection does not enable or reorder components');
  for(const style of ['digital','date','flip','pixel','simple','progress']){
    const previous=state.dynamicIsland.clockStyle;
    state=reduce(state,{type:'request',scope:'dynamicIsland',key:'clockStyle',value:style});
    assert.equal(state.dynamicIsland.clockStyle,previous);
    state=reduce(state,{type:'host',message:snapshot({clockStyle:previous,error:'save',result:'applied'})});
    assert.equal(state.dynamicIsland.clockStyle,previous,'each style keeps the last saved choice after failure');
    state=reduce(state,{type:'host',message:snapshot({clockStyle:style,saved:true,result:'applied',loadStatus:'loaded'})});
    assert.equal(state.dynamicIsland.clockStyle,style);
  }
  for(const language of ['zh-CN','en']){
    setLanguage(language);const home=islandPage(state,'home');
    assert.match(home,/data-island-clock-style="simple" aria-pressed="false"/);
    assert.match(home,/data-island-clock-style="progress" aria-pressed="true"/);
    assert.ok(home.includes(`assets/island-styles/clock-progress${language==='en'?'-en':''}.png`));
    for(const style of ['digital','date','flip','pixel'])assert.ok(home.includes(`assets/island-styles/clock-${style}${language==='en'?'-en':''}.png`));
    const music=islandPage({...state,islandStyleWidget:'music'},'home');
    assert.doesNotMatch(music,/data-island-clock-style=/);
    assert.equal((music.match(/data-island-style-card=/g)||[]).length,1);
  }
  setLanguage('zh-CN');
});

test('expansion mode defaults to click and follows confirmed hover preferences in both languages',()=>{
  for(const language of ['zh-CN','en']){
    setLanguage(language);
    for(const hover of [false,true]){
      const state=reduce(initialState(),{type:'host',message:snapshot({hover})});
      const html=islandPage(state,'home');
      assert.ok(html.indexOf('class="island-expansion"')>html.indexOf('class="island-display"'));
      assert.match(html,new RegExp(`data-island-expansion="${hover?'hover':'click'}" aria-pressed="true"`));
      assert.match(html,new RegExp(`data-island-expansion="${hover?'click':'hover'}" aria-pressed="false"`));
      for(const label of language==='en'?['Expansion mode','Click to expand','Hover to expand']:['展开模式','点击展开','悬停展开'])assert.ok(html.includes(label));
    }
  }
  setLanguage('zh-CN');
  let state=reduce(initialState(),{type:'host',message:snapshot()});
  state=reduce(state,{type:'request',scope:'dynamicIsland',key:'hover',value:true});
  assert.equal(state.dynamicIsland.hover,false,'pending mode waits for native confirmation');
  assert.match(islandPage(state,'home'),/data-island-expansion="hover" aria-pressed="false" disabled/);
  state=reduce(state,{type:'host',message:snapshot({error:'save',result:'applied'})});
  assert.equal(state.dynamicIsland.hover,false,'failed saves retain click mode');
  state=reduce(state,{type:'host',message:snapshot({hover:true,result:'applied',saved:true,loadStatus:'loaded'})});
  assert.equal(state.dynamicIsland.hover,true);
  assert.match(islandPage(state,'home'),/data-island-expansion="hover" aria-pressed="true"/);
});

test('display policy lists connected screens and preserves confirmed selection across save failures',()=>{
  const sent=[],bridge=createBridge({postMessage:message=>sent.push(message)});
  for(const screen of [0,1,64])assert.ok(bridge.send('dynamicIsland','screen',screen));
  for(const screen of [-1,65,1.5,'1',null,NaN])assert.equal(bridge.send('dynamicIsland','screen',screen),false);
  assert.deepEqual(sent,['v1|dynamicIsland|screen|0','v1|dynamicIsland|screen|1','v1|dynamicIsland|screen|64']);
  for(const fields of [{screen:0,screens:[1]},{screen:2,screens:[1,2]},{screen:2,screens:[1]}])assert.ok(validHostMessage(snapshot(fields)));
  for(const fields of [{screen:-1,screens:[1]},{screen:65,screens:[1]},{screen:'1',screens:[1]},
    {screen:1,screens:[]},{screen:1,screens:[1,1]},{screen:1,screens:[2]},{screen:1},{screens:[1]}])
    assert.equal(validHostMessage(snapshot(fields)),false);
  for(const language of ['zh-CN','en']){
    setLanguage(language);
    for(const screens of [[1],[1,2],[1,2,3,4]]){
      const state=reduce(initialState(),{type:'host',message:snapshot({screen:0,screens})});
      const html=islandPage(state,'home');
      assert.match(html,/class="island-display"/);
      assert.ok(html.indexOf('class="island-display"')>html.indexOf('id="island-size-label"'));
      assert.equal((html.match(/data-island-screen="\d+"/g)||[]).length,screens.length+1);
      for(const id of screens)assert.match(html,new RegExp(`data-island-screen="${id}"`));
      assert.match(html,/data-island-screen="0" aria-pressed="true"/);
      assert.match(html,/显示策略|Display policy/);
      assert.doesNotMatch(html,new RegExp(`data-island-screen="${screens.length+1}"`));
    }
  }
  setLanguage('zh-CN');
  let state=reduce(initialState(),{type:'host',message:snapshot({screen:2,screens:[1,2]})});
  state=reduce(state,{type:'request',scope:'dynamicIsland',key:'screen',value:0});
  assert.equal(state.dynamicIsland.screen,2,'selection waits for native confirmation');
  state=reduce(state,{type:'host',message:snapshot({screen:2,screens:[1,2],result:'applied',error:'save'})});
  assert.equal(state.dynamicIsland.screen,2);assert.equal(state.dynamicIslandStatus,'error');
  state=reduce(state,{type:'host',message:snapshot({screen:0,screens:[1,2],result:'applied',saved:true,loadStatus:'loaded'})});
  assert.equal(state.dynamicIsland.screen,0);assert.equal(state.dynamicIslandStatus,'saved');
  state=reduce(state,{type:'host',message:snapshot({screen:2,screens:[1]})});
  assert.match(islandPage(state,'home'),/data-island-screen-unavailable >所选屏幕未连接/);
  assert.equal(reduce(state,{type:'host',message:snapshot()}).dynamicIsland.screen,1,'older hosts retain the primary-screen default');
});

test('animation commands and snapshots validate their bounds and preserve older hosts',()=>{
  const sent=[],bridge=createBridge({postMessage:message=>sent.push(message)});
  for(const value of [100,320,1000])assert.ok(bridge.send('dynamicIsland','hoverDelayMs',value));
  for(const value of [99,1001,NaN,Infinity,300.5,'320',null])assert.equal(bridge.send('dynamicIsland','hoverDelayMs',value),false);
  for(const value of ['slow','normal','fast'])assert.ok(bridge.send('dynamicIsland','animationSpeed',value));
  for(const value of ['subtle','normal','bouncy'])assert.ok(bridge.send('dynamicIsland','bounce',value));
  for(const [key,value] of [['animationSpeed','Slow'],['animationSpeed',1],['bounce','large'],['bounce',true]])assert.equal(bridge.send('dynamicIsland',key,value),false);
  assert.equal(sent.length,9);
  const animation={hoverDelayMs:320,animationSpeed:'normal',bounce:'normal'};
  assert.ok(validHostMessage(snapshot(animation)));
  assert.ok(validHostMessage(snapshot({...animation,hoverDelayMs:100,animationSpeed:'slow',bounce:'bouncy'})));
  assert.ok(validHostMessage(snapshot({...animation,hoverDelayMs:1000,animationSpeed:'fast',bounce:'subtle'})));
  for(const patch of [{hoverDelayMs:99},{hoverDelayMs:1001},{hoverDelayMs:100.1},{hoverDelayMs:'320'},
    {animationSpeed:'quick'},{animationSpeed:1},{bounce:'large'},{bounce:false}])assert.equal(validHostMessage(snapshot({...animation,...patch})),false);
  for(const field of Object.keys(animation)){
    const partial={...animation};delete partial[field];assert.equal(validHostMessage(snapshot(partial)),false);
  }
  const older=reduce(initialState(),{type:'host',message:snapshot()}).dynamicIsland;
  assert.equal(older.hoverDelayMs,300);assert.equal(older.animationSpeed,'normal');assert.equal(older.bounce,'normal');
});

test('animation settings follow confirmed saves, retain preferences on failure and render in both languages',()=>{
  const animation={hoverDelayMs:1000,animationSpeed:'slow',bounce:'bouncy'};
  let state=reduce(initialState(),{type:'host',message:snapshot()});
  for(const [key,value] of Object.entries(animation)){
    state=reduce(state,{type:'request',scope:'dynamicIsland',key,value});
    assert.equal(state.dynamicIsland[key],initialState().dynamicIsland[key]);
    state=reduce(state,{type:'host',message:snapshot({result:'applied',error:'save'})});
    assert.equal(state.dynamicIslandStatus,'error');assert.equal(state.dynamicIsland[key],initialState().dynamicIsland[key]);
  }
  state=reduce(state,{type:'host',message:snapshot({...animation,result:'applied',saved:true,loadStatus:'loaded'})});
  for(const [key,value] of Object.entries(animation))assert.equal(state.dynamicIsland[key],value);
  for(const language of ['zh-CN','en']){
    setLanguage(language);const html=islandPage(state,'home');
    assert.ok(html.indexOf('class="island-animation"')>html.indexOf('class="island-expansion"'));
    assert.match(html,/data-island-hover-delay type="range" min="100" max="1000" step="50" value="1000"/);
    assert.match(html,/output for="island-hover-delay">1s/);
    assert.match(html,/data-island-preference="animationSpeed" data-value="slow" aria-pressed="true"/);
    assert.match(html,/data-island-preference="bounce" data-value="bouncy" aria-pressed="true"/);
    for(const label of language==='en'?['Animation settings','Hover delay','Expand / collapse speed','Bounce amount','Very slow','Very fast','Subtle','Bouncy']:['动画设置','悬停延迟','展开 / 收缩速度','弹动幅度','非常慢','非常快','轻微','常规','弹动'])assert.ok(html.includes(label));
  }
  setLanguage('zh-CN');
});
