import test,{afterEach} from 'node:test';
import assert from 'node:assert/strict';
import {createSettingsPageController} from '../settings-page-controller.js';
import {createComponentSettingsController} from '../component-settings.js';
import {initialState} from '../state.js';
import {setLanguage} from '../localization.js';

const emptyRoot=()=>({querySelector:()=>null,querySelectorAll:()=>[]});
afterEach(()=>setLanguage('zh-CN'));

test('settings registry routes only its messages and keeps view rendering separate from updates',()=>{
  const calls=[],root=emptyRoot(),before={status:'saving',weatherStatus:'idle'},state={status:'saved'};
  const controller=createSettingsPageController({root,showNotice:(...args)=>calls.push(['notice',...args]),
    pages:[{scope:'clock',messages:['settings'],render:(value,tab)=>`clock:${value.status}:${tab}`,
      sync:(element,value)=>calls.push(['sync',element===root,value===state]),
      notice:(value,options)=>{calls.push(['feedback',options]);return 'saved';}}],
    afterSync:scope=>calls.push(['after',scope])});
  for(const event of [{type:'navigate',view:'clock'},{type:'host',message:{type:'future-state'}}])
    assert.equal(controller.receive(event,before,state),false);
  assert.deepEqual(calls,[],'unowned events cannot refresh controls or show feedback');
  assert.equal(controller.render('clock',state,'details'),'clock:saved:details');
  assert.equal(controller.render('future',state,'details'),undefined);
  assert.equal(controller.receive({type:'host',message:{type:'settings'}},before,state),true);
  assert.deepEqual(calls,[['sync',true,true],['after','clock'],
    ['feedback',{previousStatus:'saving',hasInlineStatus:false}],['notice','saved',{autoDismiss:true}]]);
});

test('settings registry rejects ambiguous scope and host message ownership',()=>{
  const make=pages=>createSettingsPageController({root:emptyRoot(),pages,showNotice:()=>{}});
  assert.throws(()=>make([{scope:'clock'},{scope:'clock'}]),/Duplicate settings scope/);
  assert.throws(()=>make([{scope:'clock',messages:['same']},{scope:'weather',messages:['same']}]),/Duplicate settings message/);
});

test('settings controller checks the current DOM for inline feedback on every update',()=>{
  let inline=false;const notices=[],root={querySelector:()=>inline?{}:null};
  const controller=createSettingsPageController({root,showNotice:text=>notices.push(text),
    pages:[{scope:'weather',messages:['weather-state'],notice:(_state,options)=>options.hasInlineStatus?null:'error'}]});
  const event={type:'host',message:{type:'weather-state'}},state={weatherStatus:'error'};
  controller.receive(event,{},state);
  inline=true;controller.receive(event,{},state);
  inline=false;controller.receive(event,{},state);
  assert.deepEqual(notices,['error','error'],'navigation can replace the root contents without remounting the controller');
});

test('all-scope refresh preserves registration order and runs the owner hook after controls',()=>{
  const calls=[],pages=['clock','calendar','dynamicIsland','radial'].map(scope=>({scope,
    sync:()=>calls.push(scope),notice:()=>null}));
  const controller=createSettingsPageController({root:emptyRoot(),pages,showNotice:()=>{},
    afterSync:scope=>calls.push(`after:${scope}`)});
  assert.equal(controller.receive({type:'unavailable',scope:'all'},{},{}),true);
  assert.deepEqual(calls,['clock','calendar','dynamicIsland','radial','after:all']);
  calls.length=0;controller.syncScopes(['radial','dynamicIsland'],{});
  assert.deepEqual(calls,['radial','dynamicIsland'],'explicit render synchronization keeps caller order');
});

test('taskbar success feedback waits for a completed, matching host result',()=>{
  const notices=[],controller=createSettingsPageController({root:emptyRoot(),showNotice:(_text,options)=>notices.push(options),
    pages:[{scope:'taskbar',messages:['taskbar-state'],notice:()=> 'taskbar feedback'}]});
  const previous={taskbarStatus:'saving'},event={type:'host',message:{type:'taskbar-state'}};
  for(const [pendingRecovery,running,error,expected] of [[false,true,'',true],[true,true,'',false],[false,false,'',false],[false,true,'save',false]]) {
    controller.receive(event,previous,{taskbarStatus:'applied',taskbarError:error,
      taskbar:{enabled:true,running,pendingRecovery}});
    assert.equal(notices.at(-1).autoDismiss,expected);
  }
});

test('real component registrations retain localized clock feedback, aliases and page contracts',()=>{
  const notices=[],controller=createComponentSettingsController({root:emptyRoot(),showNotice:(...args)=>notices.push(args)});
  const previous=initialState(),state={...previous,status:'saved',error:''};previous.status='saving';
  setLanguage('en');
  controller.receive({type:'host',message:{type:'settings'}},previous,state);
  assert.deepEqual(notices,[['Applied and saved',{autoDismiss:true}]]);
  assert.equal(controller.receive({type:'host',message:{type:'reminder-status'}},previous,state),true);
  assert.equal(controller.receive({type:'host',message:{type:'dynamicIsland-state'}},previous,state),false,
    'island runtime feedback remains owned by the dedicated preview controller');
  for(const scope of ['clock','calendar','performance','spectrum','todayTodo','almanac','weather','organizer','icons','taskbar','minimize','dock','radial','dynamicIsland']) {
    assert.match(controller.render(scope,state,'details'),new RegExp(`data-settings-scope="${scope}"`));
  }
  assert.equal(controller.render('wallpaperDetail',state,'details'),undefined);
});
