import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createBrowserHarness} from './component-settings-browser.mjs';
import {defaultIslandNotificationPreferences} from '../island-notifications-protocol.js';

// Real index.html, production controllers, CSS and Edge; only the native bridge
// is replaced. Test drafts and failed saves never reach the user's settings.
function installChoiceFixture({defaults,language}) {
  const listeners=new Set(),commands=[],changes=[];
  let pendingNotification=null,pendingSettings=null,notificationRevision=0;
  let preferences=JSON.parse(sessionStorage.getItem('choice-preferences')||'null')||structuredClone(defaults);
  let settings={version:1,type:'app-settings-state',revision:1,
    language:sessionStorage.getItem('choice-language')||language,balancedMode:true,autostart:false,interfaceScale:75,
    busy:false,error:'',update:{status:'idle',version:'',progress:0}};
  let island={version:1,type:'dynamicIsland-state',enabled:false,running:false,locked:true,
    expanded:false,focusing:false,finished:false,remaining:0,hideFullscreen:true,topmost:false,
    hover:false,autoHide:true,size:'standard',material:'none',loadStatus:'loaded',error:'',result:'snapshot',saved:false};
  const account={version:1,type:'account-state',revision:1,status:'signed-out',busy:false,error:'',profile:null};
  const receive=message=>listeners.forEach(listener=>listener({data:message}));
  const notificationSnapshot=(fields={})=>{
    const care=preferences.waterEnabled||preferences.standEnabled||preferences.sleepEnabled||preferences.offWorkEnabled;
    const weather=preferences.weatherSevere||preferences.weatherRain||preferences.weatherTemperatureDrop;
    receive({version:1,type:'island-notifications-state',preferences:structuredClone(preferences),
      revision:notificationRevision,loaded:'loaded',result:'snapshot',saved:false,error:'',
      availability:'not-connected',receiving:false,runtime:{islandEnabled:false,
        care:care?'island-off':'off',weather:weather?'island-off':'off',qq:'disabled',wechat:'disabled',qqRealtime:false},...fields});
  };
  document.addEventListener('change',event=>{
    const source=event.target;
    if(source.matches?.('[data-island-notification-key],[data-setting="language"]'))
      changes.push({key:source.dataset.islandNotificationKey||source.dataset.setting,value:source.value});
  },true);
  window.choiceFixture={commands,changes,
    notificationReply(fail=false) {
      if(!pendingNotification)throw Error('No pending notification write');
      if(!fail)preferences[pendingNotification.key]=pendingNotification.value;
      pendingNotification=null;++notificationRevision;
      sessionStorage.setItem('choice-preferences',JSON.stringify(preferences));
      notificationSnapshot({result:'applied',saved:!fail,error:fail?'save':''});
    },
    settingsReply(fail=false) {
      if(!pendingSettings)throw Error('No pending application settings write');
      if(!fail)settings={...settings,[pendingSettings.action]:pendingSettings.value};
      pendingSettings=null;settings={...settings,revision:settings.revision+1,busy:false,error:fail?'save':''};
      sessionStorage.setItem('choice-language',settings.language);receive(settings);
    },
    settingsSnapshot(fields={}) {settings={...settings,...fields,revision:settings.revision+1};receive(settings);},
    notificationSnapshot,
    preferences:()=>structuredClone(preferences),
    settings:()=>structuredClone(settings),
    navigate(page,section) {receive({version:1,type:'navigate',page,...(section?{section}:{})});},
  };
  window.chrome={webview:{
    addEventListener(type,listener){if(type==='message')listeners.add(listener);},
    removeEventListener(type,listener){if(type==='message')listeners.delete(listener);},
    postMessage(command) {
      commands.push(command);
      if(command==='v1|ready')queueMicrotask(()=>{
        receive(settings);receive(account);receive(island);notificationSnapshot();
        receive({version:1,type:'navigate',page:'dynamicIsland'});
      });
      else if(command?.scope==='island-notifications') {
        if(command.action==='snapshot')queueMicrotask(()=>notificationSnapshot());
        else {if(pendingNotification)throw Error('Concurrent notification writes');pendingNotification=command;}
      } else if(command?.scope==='app-settings') {
        if(command.action==='snapshot')queueMicrotask(()=>receive(settings));
        else {if(pendingSettings)throw Error('Concurrent application settings writes');pendingSettings=command;}
      } else if(command?.scope==='account'&&command.action==='snapshot')queueMicrotask(()=>receive(account));
      else if(typeof command==='string'&&command.startsWith('v1|dynamicIsland|topmost|')) {
        island={...island,topmost:command.endsWith('|true'),result:'applied',saved:true};
        queueMicrotask(()=>receive(island));
      }
    },
  }};
}

const popupSelector='[data-settings-choice-popup]';
const triggerOf=source=>source.locator('xpath=ancestor::*[@data-settings-choice][1]').locator('[data-settings-choice-trigger]');
const notificationWrites=page=>page.evaluate(()=>choiceFixture.commands.filter(c=>c?.scope==='island-notifications'&&c.action==='set'));
const settingWrites=page=>page.evaluate(()=>choiceFixture.commands.filter(c=>c?.scope==='app-settings'&&c.action!=='snapshot'));
const waitClosed=page=>page.waitForFunction(selector=>!document.querySelector(selector),popupSelector);
async function openChoice(page,source) {
  const trigger=triggerOf(source);
  await trigger.click();
  await page.locator(popupSelector).waitFor({state:'visible'});
  assert.equal(await trigger.getAttribute('aria-expanded'),'true');
  const sourceStatus=await source.evaluate(node=>{
    const style=getComputedStyle(node),rect=node.getBoundingClientRect();
    return {hidden:node.hidden||style.display==='none'||style.visibility==='hidden'||Number(style.opacity)===0||rect.width<=1||rect.height<=1,
      tabIndex:node.tabIndex};
  });
  assert.ok(sourceStatus.hidden,'the visible DOM trigger replaces the native gray picker');
  assert.equal(sourceStatus.tabIndex,-1,'the native source is excluded from the keyboard path');
  return page.locator(popupSelector);
}
async function inspectPopup(page,popup,report,name) {
  const appearance=await popup.evaluate(node=>{
    const r=node.getBoundingClientRect(),selected=[...node.querySelectorAll('[aria-selected="true"]')];
    const trigger=node.hasAttribute('data-settings-choice-popup')?document.querySelector('[aria-controls="'+node.id+'"]'):null;
    const t=trigger?.getBoundingClientRect();
    return {rect:{left:r.left,top:r.top,right:r.right,bottom:r.bottom,width:r.width,height:r.height},
      trigger:t?{left:t.left,top:t.top,right:t.right,bottom:t.bottom}:null,
      viewport:{width:innerWidth,height:innerHeight},background:getComputedStyle(node).backgroundColor,
      backgroundImage:getComputedStyle(node).backgroundImage,
      kind:node.getAttribute('data-settings-choice-popup')||'legacy',
      desktopBeauty:node.classList.contains('settings-choice-popup--desktop-beauty')||!!node.closest('.settings--sidebar,.DesktopBeautyLayout'),
      selected:selected.map(option=>({text:option.textContent.trim(),background:getComputedStyle(option).backgroundColor,
        image:getComputedStyle(option).backgroundImage,color:getComputedStyle(option).color,
        column:option.closest('[data-settings-choice-column]')?.dataset.settingsChoiceColumn||''}))};
  });
  assert.ok(appearance.rect.left>=-1&&appearance.rect.right<=appearance.viewport.width+1,name+' stays within the viewport horizontally');
  assert.ok(appearance.rect.top>=-1&&appearance.rect.bottom<=appearance.viewport.height+1,name+' stays within the viewport vertically');
  if(appearance.trigger)assert.ok(Math.min(Math.abs(appearance.rect.top-appearance.trigger.bottom),
    Math.abs(appearance.trigger.top-appearance.rect.bottom))<=14,name+' stays anchored next to its trigger under interface scaling');
  assert.ok(appearance.selected.length>0,name+' exposes the selected value');
  assert.equal(appearance.backgroundImage,'none',name+' has no menu background gradient');
  assert.ok(['rgb(34, 34, 34)','rgb(36, 36, 36)'].includes(appearance.background),
    name+' keeps a neutral dark menu background: '+appearance.background);
  for(const selected of appearance.selected) {
    const purple=!appearance.desktopBeauty&&(appearance.kind==='legacy'||selected.column==='minute');
    assert.equal(selected.image,'none',name+' selected row has no mixed gradient');
    assert.equal(selected.background,appearance.desktopBeauty?'rgb(91, 123, 234)':purple?'rgb(121, 84, 201)':'rgb(22, 119, 255)',
      name+' selected row uses its desktop-style or application accent');
  }
  if(appearance.kind==='time')assert.deepEqual(appearance.selected.map(selected=>selected.column),['hour','minute'],
    name+' shows the selected hour and minute');
  report.appearances.push({name,...appearance});
  await page.screenshot({path:path.join(report.output,report.prefix+'-'+name+'.png'),fullPage:true});
}

test('shared settings choices: real Edge popups, native acknowledgements and keyboard/lifecycle behavior',
  {skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:150000},async t=>{
    const harness=await createBrowserHarness(process.env.LUME_SETTINGS_CHOICE_OUTPUT||
      fileURLToPath(new URL('../../../build-artifacts/native-rebuild/browser/settings-choice/',import.meta.url)));
    harness.report.scope='Desktop style accents and application selection colors, native acknowledgements and keyboard navigation';
    try {
      for(const [width,height,language] of [[1536,864,'zh-CN'],[1280,720,'en'],[680,864,'zh-CN'],[680,864,'en']])
        await t.test(width+'px '+language,async()=>{
          const page=await harness.browser.newPage({viewport:{width,height},reducedMotion:'reduce'});
          const report={width,height,language,prefix:width+'-'+language,output:harness.output,appearances:[],cases:[],errors:[],remote:[]};
          harness.report.viewports.push(report);page.setDefaultTimeout(8000);
          page.on('pageerror',error=>report.errors.push(error.message));
          await page.route('**/*',route=>new URL(route.request().url()).origin===harness.origin?route.continue():(report.remote.push(route.request().url()),route.abort()));
          await page.addInitScript(installChoiceFixture,{defaults:defaultIslandNotificationPreferences(),language});
          try {
            await page.goto(harness.origin);await page.locator('[data-tab="notifications"]').click();
            const root=page.locator('[data-island-notifications]'),control=key=>root.locator('[data-island-notification-key="'+key+'"]');
            await root.waitFor();await page.evaluate(()=>document.fonts.ready);
            await control('offWorkEnabled').click();await page.evaluate(()=>choiceFixture.notificationReply());
            await root.locator('[data-island-notification-editor="off-work"]').click();
            const style=control('offWorkStyle'),time=control('offWorkTime'),styleTrigger=triggerOf(style),timeTrigger=triggerOf(time);
            let popup=await openChoice(page,style);await inspectPopup(page,popup,report,'work-style-open');
            let writes=(await notificationWrites(page)).length;
            await popup.locator('[data-settings-choice-value="squidward"]').click();await waitClosed(page);
            assert.deepEqual((await notificationWrites(page)).at(-1),{version:1,scope:'island-notifications',action:'set',key:'offWorkStyle',value:'squidward'});
            assert.equal((await notificationWrites(page)).length,writes+1,'one visible option selection sends one native write');
            assert.equal(await styleTrigger.getAttribute('aria-disabled'),'true','pending native save disables its proxy trigger');
            await page.evaluate(()=>choiceFixture.notificationReply(true));
            assert.equal(await style.inputValue(),'default');assert.match(await styleTrigger.textContent(),language==='en'?/Default/:/默认/);
            assert.equal(await styleTrigger.getAttribute('aria-disabled'),'false');
            await styleTrigger.focus();await styleTrigger.press('ArrowDown');
            await page.locator(popupSelector).waitFor();await page.keyboard.press('End');await page.keyboard.press('Enter');await waitClosed(page);
            assert.equal((await notificationWrites(page)).length,writes+2,'keyboard selection sends one additional write');
            await page.evaluate(()=>choiceFixture.notificationReply());assert.equal(await style.inputValue(),'squidward');
            popup=await openChoice(page,style);await inspectPopup(page,popup,report,'work-style-squidward');
            await page.keyboard.press('Escape');await waitClosed(page);
            assert.equal(await styleTrigger.evaluate(node=>node===document.activeElement),true,'Escape restores trigger focus');
            assert.equal((await notificationWrites(page)).length,writes+2,'Escape does not save');
            report.cases.push('visible style mouse/keyboard choice, one write, pending busy, failed-save rollback, Escape focus');

            writes=(await notificationWrites(page)).length;
            popup=await openChoice(page,time);await inspectPopup(page,popup,report,'work-time-open');
            const minuteList=popup.locator('[data-settings-choice-column="minute"]'),minuteBox=await minuteList.boundingBox();
            const beforeMinuteScroll=await minuteList.evaluate(node=>node.scrollTop);
            await page.mouse.move(minuteBox.x+minuteBox.width/2,minuteBox.y+minuteBox.height/2);await page.mouse.wheel(0,180);
            await page.waitForTimeout(100);
            assert.equal(await popup.isVisible(),true,'scrolling inside the minute list keeps the time dialog open');
            assert.notEqual(await minuteList.evaluate(node=>node.scrollTop),beforeMinuteScroll,'the minute column scrolls independently');
            await popup.locator('[data-settings-choice-hour="19"]').click();
            await popup.locator('[data-settings-choice-minute="35"]').click();
            assert.equal((await notificationWrites(page)).length,writes,'draft hour/minute choices do not save');
            assert.equal(await time.inputValue(),'18:00','time draft does not change the confirmed bridge source');
            await popup.locator('[data-settings-choice-cancel]').click();await waitClosed(page);
            assert.equal(await time.inputValue(),'18:00');assert.equal((await notificationWrites(page)).length,writes);
            popup=await openChoice(page,time);
            await popup.locator('[data-settings-choice-hour="18"]').click();await popup.locator('[data-settings-choice-minute="45"]').click();
            await inspectPopup(page,popup,report,'work-time-draft');await popup.locator('[data-settings-choice-apply]').click();await waitClosed(page);
            assert.deepEqual((await notificationWrites(page)).at(-1),{version:1,scope:'island-notifications',action:'set',key:'offWorkTime',value:'18:45'});
            assert.equal((await notificationWrites(page)).length,writes+1);
            await page.evaluate(()=>choiceFixture.notificationReply(true));assert.equal(await time.inputValue(),'18:00');
            popup=await openChoice(page,time);
            await popup.locator('[data-settings-choice-hour="18"]').click();await popup.locator('[data-settings-choice-minute="45"]').click();
            await popup.locator('[data-settings-choice-apply]').click();await page.evaluate(()=>choiceFixture.notificationReply());
            assert.equal(await time.inputValue(),'18:45');assert.match(await timeTrigger.textContent(),/18:45/);
            const changeCounts=await page.evaluate(()=>choiceFixture.changes.filter(change=>change.key==='offWorkTime'));
            assert.equal(changeCounts.length,2,'each explicit Apply dispatches exactly one source change');
            popup=await openChoice(page,time);await popup.locator('[data-settings-choice-hour="20"]').click();
            await page.keyboard.press('Escape');await waitClosed(page);assert.equal(await time.inputValue(),'18:45');
            assert.equal(await timeTrigger.evaluate(node=>node===document.activeElement),true);
            popup=await openChoice(page,time);await popup.locator('[data-settings-choice-minute="15"]').click();
            await root.locator('h1').click();await waitClosed(page);assert.equal(await time.inputValue(),'18:45');
            assert.equal((await notificationWrites(page)).length,writes+2,'Escape and outside click cancel without writes');
            popup=await openChoice(page,time);
            await page.keyboard.press('ArrowDown');await page.keyboard.press('Enter');
            await page.keyboard.press('ArrowRight');await page.keyboard.press('End');await page.keyboard.press('Enter');
            assert.equal(await popup.locator('[data-settings-choice-hour="19"]').getAttribute('aria-selected'),'true');
            assert.equal(await popup.locator('[data-settings-choice-minute="59"]').getAttribute('aria-selected'),'true');
            assert.equal(await time.inputValue(),'18:45','keyboard draft leaves the confirmed time untouched');
            await page.keyboard.press('Escape');await waitClosed(page);
            assert.equal((await notificationWrites(page)).length,writes+2,'canceling a keyboard time draft does not save');
            report.cases.push('hour/minute drafts, Apply once, cancel/Escape/outside, failed time save and confirmed display');

            if(width===1536)for(const target of ['html','body'])for(const zoom of [.75,1.25]) {
              await page.evaluate(({target,zoom})=>document.querySelector(target).style.zoom=String(zoom),{target,zoom});
              await styleTrigger.scrollIntoViewIfNeeded();popup=await openChoice(page,style);
              await inspectPopup(page,popup,report,'style-'+target+'-zoom-'+zoom);await page.keyboard.press('Escape');await waitClosed(page);
              await timeTrigger.scrollIntoViewIfNeeded();popup=await openChoice(page,time);
              await inspectPopup(page,popup,report,'time-'+target+'-zoom-'+zoom);await page.keyboard.press('Escape');await waitClosed(page);
              await page.evaluate(target=>document.querySelector(target).style.zoom='',target);
            }
            if(width===1536)report.cases.push('0.75/1.25 CSS interface zoom: select/time popups remain next to triggers and within viewport');

            await openChoice(page,style);await page.setViewportSize({width:width-20,height});await waitClosed(page);
            await page.setViewportSize({width,height});
            await page.setViewportSize({width,height:540});
            await styleTrigger.scrollIntoViewIfNeeded();await openChoice(page,style);
            const body=page.locator('.settings-body');const bodyBox=await body.boundingBox();
            const beforeScroll=await body.evaluate(node=>node.scrollTop);
            // Scroll below the title bar's overlapping drag strip.
            await page.mouse.move(bodyBox.x+12,bodyBox.y+80);await page.mouse.wheel(0,beforeScroll>0?-450:450);
            await page.waitForTimeout(100);
            report.parentScroll={before:beforeScroll,after:await body.evaluate(node=>node.scrollTop),box:bodyBox,
              popupVisible:await page.locator(popupSelector).count()};
            await waitClosed(page);
            assert.notEqual(await body.evaluate(node=>node.scrollTop),beforeScroll,'real parent scrolling closes the floating menu');
            await page.setViewportSize({width,height});
            await page.reload();await page.locator('[data-tab="notifications"]').click();
            assert.equal(await control('offWorkStyle').inputValue(),'squidward');assert.equal(await control('offWorkTime').inputValue(),'18:45');
            await root.locator('[data-island-notification-editor="off-work"]').click();
            assert.match(await triggerOf(control('offWorkStyle')).textContent(),/章鱼哥|Squidward/);
            assert.match(await triggerOf(control('offWorkTime')).textContent(),/18:45/);
            report.cases.push('resize/real parent scroll close the popup; saved style/time reopen with confirmed proxy text');

            await page.locator('[data-titlebar-settings]').click();
            const languageSource=page.locator('[data-setting="language"]'),languageTrigger=triggerOf(languageSource);
            popup=await openChoice(page,languageSource);await inspectPopup(page,popup,report,'language-open');
            await page.keyboard.press('Escape');await waitClosed(page);
            await page.evaluate(()=>choiceFixture.settingsSnapshot({interfaceScale:50}));
            assert.equal(await page.locator('#application-interface-scale').inputValue(),'50','real application settings snapshot updates its confirmed scale control');
            popup=await openChoice(page,languageSource);await inspectPopup(page,popup,report,'language-scale-snapshot-50');
            await page.keyboard.press('Escape');await waitClosed(page);
            await page.evaluate(()=>choiceFixture.settingsSnapshot({interfaceScale:75}));
            popup=await openChoice(page,languageSource);
            const appWrites=(await settingWrites(page)).length,targetLanguage=language==='en'?'zh-CN':'en';
            await page.evaluate(()=>choiceFixture.settingsSnapshot({busy:true}));await waitClosed(page);
            assert.equal(await languageTrigger.getAttribute('aria-disabled'),'true','host busy snapshot closes and disables the live proxy');
            await languageTrigger.press('Enter');assert.equal(await page.locator(popupSelector).count(),0,'busy keyboard input cannot reopen the popup');
            assert.equal((await settingWrites(page)).length,appWrites);
            await page.evaluate(()=>choiceFixture.settingsSnapshot({busy:false}));
            assert.equal(await languageTrigger.getAttribute('aria-disabled'),'false');
            popup=await openChoice(page,languageSource);await popup.locator('[data-settings-choice-value="'+targetLanguage+'"]').click();
            assert.deepEqual((await settingWrites(page)).at(-1),{version:1,scope:'app-settings',action:'language',value:targetLanguage});
            await page.evaluate(()=>choiceFixture.settingsReply(true));
            assert.equal(await languageSource.inputValue(),language,'failed language write restores the confirmed source and trigger');
            popup=await openChoice(page,languageSource);await popup.locator('[data-settings-choice-value="'+targetLanguage+'"]').click();
            await page.evaluate(()=>choiceFixture.settingsReply());
            assert.equal(await languageSource.inputValue(),targetLanguage);assert.equal(await page.locator('html').getAttribute('lang'),targetLanguage);
            popup=await openChoice(page,languageSource);await page.keyboard.press('Escape');await waitClosed(page);
            assert.equal(await languageTrigger.evaluate(node=>node===document.activeElement),true);
            report.cases.push('language visible selection, exact native message, failed rollback, live busy close/reopen, localization');

            await page.evaluate(()=>choiceFixture.navigate('dynamicIsland'));await page.locator('[data-tab="settings"]').click();
            const legacy=page.locator('#dynamicIsland-layer');await legacy.click();
            const legacyPopup=page.locator('#dynamicIsland-layer-options');await legacyPopup.waitFor({state:'visible'});
            await inspectPopup(page,legacyPopup,report,'existing-select-open');
            await legacyPopup.locator('[data-select-value="above"]').click();
            assert.equal(await page.evaluate(()=>choiceFixture.commands.filter(c=>c==='v1|dynamicIsland|topmost|true').length),1);
            await legacy.click();await page.keyboard.press('Escape');assert.equal(await legacy.getAttribute('aria-expanded'),'false');
            report.cases.push('existing custom select retains native command with neutral menu and matching desktop blue selection');
            assert.deepEqual(report.errors,[]);assert.deepEqual(report.remote,[]);
            report.passed=true;
          } catch(error) {
            report.failure=error.message;
            await page.screenshot({path:path.join(harness.output,report.prefix+'-failed.png'),fullPage:true}).catch(()=>{});
            throw error;
          } finally {await page.close();}
        });
    } finally {await harness.close();}
  });
