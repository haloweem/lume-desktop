import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createBrowserHarness} from './component-settings-browser.mjs';

// Stand-in for the native ControlCenterWindow (platform/windows/control_center).
function host({language,state}){
  const listeners=new Set(),sent=[];
  window.ccHost={sent,emit:state=>listeners.forEach(l=>l({data:{version:1,type:'control-center-state',state}}))};
  window.chrome={webview:{
    addEventListener(name,listener){if(name==='message')listeners.add(listener);},
    removeEventListener(name,listener){listeners.delete(listener);},
    postMessage(message){sent.push(message);
      const emit=data=>queueMicrotask(()=>listeners.forEach(l=>l({data})));
      // List pages: the host adds networks / devices while one is open, and reports a join.
      if(message.action==='set'&&message.key==='view'){
        const next={...state};
        if(message.value==='wifi')next.networks=[{ssid:'Home-5G',signal:92,secured:true,connected:true,known:true},
          {ssid:'Office',signal:60,secured:true,connected:false,known:false},{ssid:'Cafe Free',signal:30,secured:false,connected:false,known:false}];
        if(message.value==='bluetooth')next.devices=[{name:'WH-1000XM5',connected:true},{name:'MX Keys',connected:false}];
        window.ccHost.view=next;emit({version:1,type:'control-center-state',state:next});
      }
      if(message.action==='set'&&message.key==='connect')
        emit({version:1,type:'control-center-state',state:{...window.ccHost.view,join:{ssid:message.value.ssid,status:'failed'}}});
      if(message.action==='ready')queueMicrotask(()=>{
        listeners.forEach(l=>l({data:{version:1,type:'control-center-init',english:language==='en',material:'solid'}}));
        listeners.forEach(l=>l({data:{version:1,type:'control-center-state',state}}));
        listeners.forEach(l=>l({data:{version:1,type:'control-center-shown'}}));
      });
    }}};
}
const desktop={wifi:{available:true,on:true,ssid:'Home-5G',signal:90},ethernet:false,bluetooth:{available:true,on:true},
  hotspot:{available:true,on:false,reason:''},airplane:{available:true,on:false},
  audio:{available:true,volume:40,muted:false,output:'a',outputs:[{id:'a',name:'Speakers (Realtek Audio)'},{id:'b',name:'DELL G2725D (NVIDIA High Definition Audio)'}]},
  brightness:{available:true,level:54,display:'\\\\.\\DISPLAY1',displays:[{id:'\\\\.\\DISPLAY1',name:'BenQ PD2706QN'},{id:'\\\\.\\DISPLAY2',name:'DELL G2725D'}]},
  battery:{available:false,percent:0,charging:false},tray:true,accent:'#0067c0'};
// A wired desktop with no wireless hardware and a monitor without DDC/CI.
const wired={...desktop,wifi:{available:false,on:false,ssid:'',signal:0},ethernet:true,bluetooth:{available:false,on:false},
  hotspot:{available:false,on:false,reason:'hardware'},airplane:{available:false,on:false},
  brightness:{available:false,level:0,display:'',displays:[]},audio:{...desktop.audio,outputs:[desktop.audio.outputs[0]]},tray:false};
// A laptop: internal panel, battery, Wi-Fi off, hotspot without a network.
const laptop={...desktop,wifi:{available:true,on:false,ssid:'',signal:0},hotspot:{available:false,on:false,reason:'no-network'},
  battery:{available:true,percent:76,charging:true},brightness:{available:true,level:80,display:'\\\\.\\DISPLAY1',displays:[{id:'\\\\.\\DISPLAY1',name:'内置显示器'}]}};

test('control center renders every machine profile and sends commands',{skip:process.env.LUME_UI_BROWSER_TESTS!=='1'},async()=>{
  const output=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../build-artifacts/control-center-browser');
  const harness=await createBrowserHarness(output);
  try{
    for(const [name,state,language] of [['desktop',desktop,'zh'],['wired',wired,'zh'],['laptop',laptop,'zh'],['desktop-en',desktop,'en']]){
      const tab=await harness.browser.newPage({viewport:{width:372,height:560}});
      const errors=[];tab.on('pageerror',error=>errors.push(error.message));
      await tab.addInitScript(host,{language,state});await tab.goto(harness.origin+'/control-center.html');
      await tab.waitForSelector('.cc.is-shown .cc-grid');await tab.waitForTimeout(300);
      await tab.screenshot({path:path.join(output,`${name}.png`)});
      const overflow=await tab.evaluate(()=>[...document.querySelectorAll('.cc-tile__text b')].some(n=>n.scrollWidth>n.clientWidth+1||n.scrollHeight>n.clientHeight+1));
      assert.equal(overflow,false,`${name}: tile titles fit`);
      const size=await tab.evaluate(()=>window.ccHost.sent.find(m=>m.action==='size'));
      assert.ok(size&&size.height>200&&size.height<620,`${name}: reports its height`);
      const stable=await tab.evaluate(async state=>{
        const root=document.querySelector('[data-cc]'),tile=root.querySelector('[data-cc-toggle]');
        const wait=async()=>{await new Promise(requestAnimationFrame);await new Promise(requestAnimationFrame);};
        await wait();const sizes=ccHost.sent.filter(message=>message.action==='size').length;
        for(let i=0;i<10;i++)ccHost.emit(structuredClone(state));
        await wait();
        return {same:tile===root.querySelector('[data-cc-toggle]'),sizes:ccHost.sent.filter(message=>message.action==='size').length-sizes};
      },state);
      assert.deepEqual(stable,{same:true,sizes:0},'identical host bursts preserve controls and do not resend height');
      // Heights sent while the open spring scales the panel must still fit every row.
      const fits=await tab.evaluate(()=>window.ccHost.sent.filter(m=>m.action==='size').every(m=>m.height>=document.querySelector('[data-cc]').offsetHeight));
      assert.equal(fits,true,`${name}: no reported height cuts off the last row`);
      if(name==='wired'){
        assert.equal(await tab.locator('[data-cc-toggle][aria-disabled="true"]').count(),4,'missing radios are disabled, not broken');
        assert.equal(await tab.locator('[data-cc-range="brightness"]').count(),0,'no brightness slider without support');
        assert.equal(await tab.locator('[data-cc-tray]').count(),0,'no tray row when the overflow cannot open');
        await tab.locator('[data-cc-toggle="wifi"]').dispatchEvent('click');
        assert.equal(await tab.evaluate(()=>window.ccHost.sent.filter(m=>m.action==='set').length),0,'disabled tiles send nothing');
        assert.match(await tab.textContent('.cc-chip'),/以太网/);
      }
      if(name==='laptop')assert.match(await tab.textContent('.cc-battery'),/76%/);
      if(name==='desktop'){
        await tab.click('[data-cc-toggle="bluetooth"]');
        await tab.click('[data-cc-open="output"]');await tab.waitForTimeout(250);
        await tab.screenshot({path:path.join(output,'desktop-output-menu.png')});
        await tab.click('[data-cc-choose="output"][data-id="b"]');
        await tab.fill('[data-cc-range="volume"]','70');
        await tab.click('[data-cc-mute]');
        await tab.click('[data-cc-tray]');
        const sent=await tab.evaluate(()=>window.ccHost.sent.filter(m=>m.action!=='size'&&m.action!=='ready'));
        assert.deepEqual(sent.find(m=>m.key==='bluetooth'),{version:1,scope:'control-center',action:'set',key:'bluetooth',value:false});
        assert.ok(sent.some(m=>m.key==='output'&&m.value==='b'));
        assert.ok(sent.some(m=>m.key==='volume'&&m.value===70));
        assert.ok(sent.some(m=>m.key==='mute'&&m.value===true));
        assert.ok(sent.some(m=>m.action==='tray'));
        await tab.keyboard.press('Escape');
        assert.ok(await tab.evaluate(()=>window.ccHost.sent.some(m=>m.action==='close')));
      }
      if(name==='desktop'){
        // Wi-Fi list: arrow opens it, a new secured network asks for its key, a failure explains itself.
        await tab.keyboard.press('Escape');
        await tab.evaluate(()=>{window.ccHost.sent.length=0;});
        await tab.click('[data-cc-view="wifi"]');await tab.waitForSelector('[data-cc-network="Office"]');await tab.waitForTimeout(400);
        await tab.screenshot({path:path.join(output,'wifi-list.png')});
        await tab.click('[data-cc-network="Office"]');
        await tab.fill('[data-cc-password]','correct horse');
        await tab.press('[data-cc-password]','Enter');
        await tab.waitForSelector('.cc-note.is-error');
        await tab.screenshot({path:path.join(output,'wifi-join-failed.png')});
        await tab.click('[data-cc-network="Home-5G"]');await tab.click('[data-cc-disconnect]');
        const wifi=await tab.evaluate(()=>window.ccHost.sent.filter(m=>m.action==='set'));
        assert.ok(wifi.some(m=>m.key==='view'&&m.value==='wifi'),'opening the list asks the host for networks');
        assert.ok(wifi.some(m=>m.key==='connect'&&m.value.ssid==='Office'&&m.value.password==='correct horse'),'connect sends the key');
        assert.ok(wifi.some(m=>m.key==='disconnect'),'disconnect');
        await tab.click('[data-cc-back]');await tab.waitForSelector('.cc-grid');
        assert.ok(await tab.evaluate(()=>window.ccHost.sent.some(m=>m.key==='view'&&m.value==='')),'leaving the list tells the host');
        await tab.click('[data-cc-view="bluetooth"]');await tab.waitForSelector('[data-cc-power="bluetooth"]');
        // The earlier tile click turned Bluetooth off; the list header switch turns it back on.
        if(!(await tab.$('.cc-switch.is-on')))await tab.click('[data-cc-power="bluetooth"]');
        await tab.waitForSelector('.cc-row');await tab.waitForTimeout(400);
        assert.match(await tab.textContent('.cc-list'),/WH-1000XM5[\s\S]*已连接/);
        await tab.screenshot({path:path.join(output,'bluetooth-list.png')});
        const accent=await tab.evaluate(()=>getComputedStyle(document.querySelector('.cc-switch.is-on')).backgroundColor);
        assert.equal(accent,'rgb(0, 103, 192)','the system accent colors the switch');
      }
      assert.deepEqual(errors,[]);await tab.close();
    }
  }finally{await harness.close();}
});
