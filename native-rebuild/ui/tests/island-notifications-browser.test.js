import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createBrowserHarness} from './component-settings-browser.mjs';
import {defaultIslandNotificationPreferences} from '../island-notifications-protocol.js';

function installNotificationFixture({defaults,language}){
  const listeners=new Set(),commands=[];let pending=null,revision=0,legacyRuntime=false,legacyPreferences='',legacyMasters={},lastSnapshot=null;
  let runtime={islandEnabled:false,care:'off',weather:'off',qq:'disabled',wechat:'disabled',qqRealtime:false};
  let preferences=JSON.parse(sessionStorage.getItem('notification-fixture-preferences')||'null')||structuredClone(defaults);
  const receive=m=>listeners.forEach(listener=>listener({data:m}));
  const snapshot=(fields={})=>{
    const projected=structuredClone(preferences),effective=structuredClone(preferences);
    if(legacyPreferences){
      delete projected.offWorkStyle;
      Object.assign(projected,{careEnabled:true,weatherEnabled:true,messagesEnabled:true},legacyMasters);
      if(legacyPreferences==='old18'){delete projected.offWorkEnabled;delete projected.offWorkTime;effective.offWorkEnabled=false;}
      if(!projected.careEnabled)for(const key of ['waterEnabled','standEnabled','sleepEnabled','offWorkEnabled'])effective[key]=false;
      if(!projected.weatherEnabled)for(const key of ['weatherSevere','weatherRain','weatherTemperatureDrop'])effective[key]=false;
      if(!projected.messagesEnabled)effective.messagesQQ=effective.messagesWeChat=false;
    }
    const care=effective.waterEnabled||effective.standEnabled||effective.sleepEnabled||effective.offWorkEnabled;
    const weather=effective.weatherSevere||effective.weatherRain||effective.weatherTemperatureDrop;
    const activeRuntime={...runtime,care:!care?'off':runtime.islandEnabled?'active':'island-off',
      weather:!weather?'off':runtime.islandEnabled?'waiting':'island-off',
      qq:runtime.islandEnabled&&effective.messagesQQ?runtime.qq:'disabled',
      wechat:runtime.islandEnabled&&effective.messagesWeChat?runtime.wechat:'disabled',
      qqRealtime:runtime.islandEnabled&&effective.messagesQQ&&runtime.qqRealtime};
    if(legacyRuntime)delete activeRuntime.qqRealtime;
    const receiving=activeRuntime.islandEnabled&&(['connected','attention'].includes(activeRuntime.qq)&&effective.messagesQQ||activeRuntime.wechat==='listening'&&effective.messagesWeChat);
    lastSnapshot={version:1,type:'island-notifications-state',preferences:projected,revision,loaded:'loaded',result:'snapshot',saved:false,error:'',availability:receiving?'ready':'not-connected',receiving,runtime:activeRuntime,...fields};
    receive(lastSnapshot);
  };
  const island={version:1,type:'dynamicIsland-state',enabled:false,running:false,locked:true,expanded:false,focusing:false,finished:false,remaining:0,hideFullscreen:true,topmost:false,hover:false,autoHide:true,size:'standard',material:'none',loadStatus:'loaded',error:'',result:'snapshot',saved:false};
  window.notificationFixture={commands,snapshot,
    setRuntime(value,{legacy=false}={}){runtime={qqRealtime:false,...value};legacyRuntime=legacy;snapshot();},
    setLegacyPreferences(kind,masters={}){legacyPreferences=kind;legacyMasters=masters;snapshot();},
    lastSnapshot:()=>structuredClone(lastSnapshot),
    reply(fail=false){if(!pending)throw Error('No pending notification preference');
      if(!fail){if(pending.action==='reset')preferences=structuredClone(defaults);else preferences[pending.key]=pending.value;sessionStorage.setItem('notification-fixture-preferences',JSON.stringify(preferences));}
      pending=null;++revision;snapshot({result:'applied',saved:!fail,error:fail?'save':''});
    },
    heartbeat(){receive(island);snapshot();},preferences:()=>structuredClone(preferences),
  };
  window.chrome={webview:{addEventListener(type,listener){if(type==='message')listeners.add(listener);},removeEventListener(type,listener){if(type==='message')listeners.delete(listener);},
    postMessage(command){commands.push(command);
      if(command==='v1|ready')queueMicrotask(()=>{receive({version:1,type:'app-settings-state',revision:1,language,balancedMode:true,autostart:false,busy:false,error:'',update:{status:'idle',version:'',progress:0}});receive(island);snapshot();receive({version:1,type:'navigate',page:'dynamicIsland'});});
      else if(command?.scope==='island-notifications'){
        if(command.action==='snapshot')queueMicrotask(()=>snapshot());
        else if(command.action==='authorize'){runtime.qq='requesting';queueMicrotask(()=>snapshot({result:'applied'}));}
        else {if(pending)throw Error('Concurrent preference writes');pending=command;}
      }
    }}};
}

test('notification page: direct switches, off-work style acknowledgements, migration and persistence',{skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:120000},async t=>{
  const harness=await createBrowserHarness(fileURLToPath(new URL('../../../build-artifacts/island-direct-switches-squidward-20261110/ui/final',import.meta.url)));
  try{
    for(const [width,height,language] of [[1536,864,'zh-CN'],[1280,864,'en'],[680,960,'zh-CN'],[680,960,'en']])await t.test(width+'px '+language,async()=>{
      const page=await harness.browser.newPage({viewport:{width,height},reducedMotion:'reduce'}),errors=[],remote=[];
      page.on('pageerror',e=>errors.push(e.message));
      await page.route('**/*',route=>new URL(route.request().url()).origin===harness.origin?route.continue():(remote.push(route.request().url()),route.abort()));
      await page.addInitScript(installNotificationFixture,{defaults:defaultIslandNotificationPreferences(),language});
      await page.goto(harness.origin);await page.locator('[data-tab="notifications"]').click();
      const root=page.locator('[data-island-notifications]'),control=key=>root.locator('[data-island-notification-key="'+key+'"]');
      const editor=root.locator('[data-island-notification-editor="off-work"]'),panel=root.locator('[data-island-notification-panel="off-work"]');
      // Use the same visible proxy contract as settings-choice-browser.test.js.
      const openChoice=async key=>{
        await control(key).locator('xpath=ancestor::*[@data-settings-choice][1]').locator('[data-settings-choice-trigger]').click();
        const popup=page.locator('[data-settings-choice-popup]');await popup.waitFor({state:'visible'});return popup;
      };
      const draftTime=async value=>{
        const popup=await openChoice('offWorkTime'),[hour,minute]=value.split(':');
        await popup.locator('[data-settings-choice-hour="'+hour+'"]').click();
        await popup.locator('[data-settings-choice-minute="'+minute+'"]').click();return popup;
      };
      const chooseStyle=async value=>{
        const popup=await openChoice('offWorkStyle');await popup.locator('[data-settings-choice-value="'+value+'"]').click();
      };
      await root.waitFor();await page.evaluate(()=>document.fonts.ready);
      await root.locator('.in-icon img').evaluateAll(images=>Promise.all(images.map(image=>image.decode())));
      assert.equal(await root.locator('[data-island-notification-group]').count(),3);
      assert.equal(await root.locator('.in-module__head [role="switch"]').count(),0,'all three title master switches are removed');
      assert.equal(await root.locator('.in-module.is-disabled').count(),0);
      assert.equal(await page.evaluate(()=>Object.keys(notificationFixture.lastSnapshot().preferences).length),18);
      for(const key of ['waterEnabled','standEnabled','sleepEnabled','offWorkEnabled','weatherSevere','weatherRain','weatherTemperatureDrop','messagesQQ','messagesWeChat','messagePreview']){
        assert.equal(await control(key).getAttribute('aria-checked'),'false',key);assert.equal(await control(key).isEnabled(),true,key);
      }
      assert.equal(await editor.isDisabled(),true);assert.equal(await control('offWorkTime').inputValue(),'18:00');
      assert.equal(await control('offWorkStyle').inputValue(),'default');
      assert.equal(await root.locator('.in-off-work-style-info,[data-island-off-work-avatar],[data-island-off-work-style-description]').count(),0);
      assert.equal(await control('offWorkTime').getAttribute('aria-label'),language==='en'?'End-of-work time':'下班时间');
      assert.deepEqual(await root.locator('[data-island-notification-group="care"] [data-island-notification-row]').evaluateAll(rows=>rows.map(row=>row.dataset.islandNotificationRow)),['waterEnabled','standEnabled','sleepEnabled','offWorkEnabled']);
      assert.equal(await page.evaluate(()=>notificationFixture.commands.some(c=>c?.scope==='island-notifications'&&c.action!=='snapshot')),false);
      await page.waitForTimeout(5200);
      assert.equal(await control('waterEnabled').isEnabled(),true,'the real snapshot settles the five-second host timeout');
      assert.equal(await root.locator('[data-island-notification-status]').isVisible(),false);
      await page.screenshot({path:path.join(harness.output,width+'-'+language+'-default.png'),fullPage:true});

      await control('messagesQQ').click();assert.equal(await control('messagesQQ').getAttribute('aria-checked'),'false');
      assert.equal(await control('messagesQQ').isDisabled(),true);await page.evaluate(()=>notificationFixture.reply(true));
      assert.equal(await control('messagesQQ').getAttribute('aria-checked'),'false');
      await control('messagesQQ').click();await page.evaluate(()=>notificationFixture.reply());
      assert.equal(await control('messagesQQ').getAttribute('aria-checked'),'true');
      assert.equal(await page.evaluate(()=>notificationFixture.commands.some(c=>c?.action==='authorize')),false);
      await control('waterEnabled').click();await page.evaluate(()=>notificationFixture.reply());
      await control('offWorkEnabled').click();await page.evaluate(()=>notificationFixture.reply(true));assert.equal(await editor.isDisabled(),true);
      await control('offWorkEnabled').click();await page.evaluate(()=>notificationFixture.reply());await editor.click();
      let timePopup=await draftTime('18:45');await page.evaluate(()=>notificationFixture.heartbeat());
      assert.equal(await timePopup.locator('[data-settings-choice-hour="18"]').getAttribute('aria-selected'),'true');
      assert.equal(await timePopup.locator('[data-settings-choice-minute="45"]').getAttribute('aria-selected'),'true','heartbeat retains the unsent time draft');
      assert.equal(await control('offWorkTime').inputValue(),'18:00','the draft leaves the confirmed source unchanged');
      await timePopup.locator('[data-settings-choice-apply]').click();await page.evaluate(()=>notificationFixture.reply(true));
      assert.equal(await control('offWorkTime').inputValue(),'18:00','failed time save rolls back');
      timePopup=await draftTime('18:45');await timePopup.locator('[data-settings-choice-apply]').click();await page.evaluate(()=>notificationFixture.reply());
      const beforeInvalid=await page.evaluate(()=>notificationFixture.commands.length);
      // The chooser cannot produce an empty time; inject one at the source
      // contract to retain the invalid-value rejection check without a native popup.
      await control('offWorkTime').evaluate(node=>{node.value='';node.dispatchEvent(new Event('change',{bubbles:true}));});
      assert.equal(await control('offWorkTime').inputValue(),'18:45');assert.equal(await page.evaluate(()=>notificationFixture.commands.length),beforeInvalid);
      await chooseStyle('squidward');
      assert.deepEqual(await page.evaluate(()=>notificationFixture.commands.at(-1)),{version:1,scope:'island-notifications',action:'set',key:'offWorkStyle',value:'squidward'});
      await page.evaluate(()=>notificationFixture.reply(true));assert.equal(await control('offWorkStyle').inputValue(),'default');
      assert.equal(await root.locator('.in-off-work-style-info,[data-island-off-work-avatar],[data-island-off-work-style-description]').count(),0);
      await chooseStyle('squidward');await page.evaluate(()=>notificationFixture.reply());
      assert.equal(await root.locator('.in-off-work-style-info,[data-island-off-work-avatar],[data-island-off-work-style-description]').count(),0,'style selection never renders the removed preview and description');
      assert.equal(await panel.locator(':scope > *').count(),2,'the editor contains only time and style fields');
      assert.equal(await root.locator('audio,[data-island-notification-key="offWorkAudio"]').count(),0);
      assert.equal(await control('standEnabled').getAttribute('aria-checked'),'false','one reminder never enables a sibling');
      await root.evaluate(node=>node.closest('.settings-body').scrollTop=0);
      await page.screenshot({path:path.join(harness.output,width+'-'+language+'-squidward.png'),fullPage:true});
      await editor.click();assert.equal(await panel.isVisible(),false);
      await root.locator('[data-island-notification-editor="water"]').click();
      await control('waterIntervalMinutes').fill('90');await page.evaluate(()=>notificationFixture.heartbeat());
      assert.equal(await control('waterIntervalMinutes').inputValue(),'90');
      await control('waterIntervalMinutes').press('Tab');await page.evaluate(()=>notificationFixture.reply());await control('waterGoal').press('Escape');
      await control('weatherRain').click();await page.evaluate(()=>notificationFixture.reply());
      assert.equal(await control('weatherSevere').getAttribute('aria-checked'),'false');

      await page.evaluate(()=>notificationFixture.setRuntime({islandEnabled:true,qq:'access-required',wechat:'disabled'}));
      assert.equal(await root.locator('[data-island-notification-access]').count(),0);
      assert.equal(await page.evaluate(()=>notificationFixture.commands.filter(c=>c?.action==='authorize').length),0);
      await page.evaluate(()=>notificationFixture.setRuntime({islandEnabled:true,qq:'connected',wechat:'disabled',qqRealtime:true}));
      assert.equal(await root.locator('[data-island-notification-access]').isVisible(),false);
      await page.evaluate(()=>notificationFixture.setLegacyPreferences('old20',{careEnabled:false,weatherEnabled:false}));
      assert.equal(await page.evaluate(()=>Object.keys(notificationFixture.lastSnapshot().preferences).length),20);
      for(const key of ['waterEnabled','offWorkEnabled','weatherRain'])assert.equal(await control(key).getAttribute('aria-checked'),'false');
      assert.equal(await control('messagesQQ').getAttribute('aria-checked'),'true');
      assert.equal(await root.locator('[data-island-notification-access]').isVisible(),false,'migration keeps existing QQ reception');
      await page.evaluate(()=>notificationFixture.setLegacyPreferences('old18'));
      assert.equal(await page.evaluate(()=>Object.keys(notificationFixture.lastSnapshot().preferences).length),18);
      assert.equal(await control('waterEnabled').getAttribute('aria-checked'),'true');assert.equal(await control('offWorkEnabled').getAttribute('aria-checked'),'false');
      assert.equal(await control('offWorkTime').inputValue(),'18:00');assert.equal(await control('offWorkStyle').inputValue(),'default');
      await page.evaluate(()=>notificationFixture.setLegacyPreferences(''));
      assert.equal(await control('offWorkStyle').inputValue(),'squidward');assert.equal(await control('offWorkTime').inputValue(),'18:45');
      await page.evaluate(()=>notificationFixture.setRuntime({islandEnabled:true,qq:'access-required',wechat:'disabled'},{legacy:true}));
      assert.equal(await root.locator('[data-island-notification-access]').count(),0);
      await page.evaluate(()=>notificationFixture.setRuntime({islandEnabled:true,qq:'attention',wechat:'disabled'}));
      const geometry=await root.evaluate(node=>({width:node.clientWidth,scroll:node.scrollWidth,pageWidth:innerWidth,documentWidth:document.documentElement.scrollWidth}));
      assert.ok(geometry.scroll<=geometry.width+1,JSON.stringify(geometry));assert.ok(geometry.documentWidth<=geometry.pageWidth+1,JSON.stringify(geometry));
      await root.evaluate(node=>node.closest('.settings-body').scrollTop=0);
      await page.screenshot({path:path.join(harness.output,width+'-'+language+'-configured.png'),fullPage:true});
      await page.reload();await page.locator('[data-tab="notifications"]').click();
      assert.equal(await control('messagesQQ').getAttribute('aria-checked'),'true');assert.equal(await control('waterEnabled').getAttribute('aria-checked'),'true');
      assert.equal(await page.evaluate(()=>notificationFixture.preferences().waterIntervalMinutes),90);
      assert.equal(await control('offWorkEnabled').getAttribute('aria-checked'),'true');assert.equal(await control('offWorkStyle').inputValue(),'squidward');
      await control('offWorkEnabled').click();await page.evaluate(()=>notificationFixture.reply());assert.equal(await editor.isDisabled(),true);
      assert.equal(await control('waterEnabled').getAttribute('aria-checked'),'true','turning off one reminder leaves others enabled');
      assert.equal(await page.evaluate(()=>notificationFixture.commands.some(c=>['careEnabled','weatherEnabled','messagesEnabled'].includes(c?.key))),false);
      assert.deepEqual(errors,[]);assert.deepEqual(remote,[]);harness.report.viewports.push({width,height,language,geometry,passed:true});await page.close();
    });
  }finally{await harness.close();}
});
