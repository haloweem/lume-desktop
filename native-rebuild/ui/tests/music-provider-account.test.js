import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {createBridge,validHostMessage} from '../bridge.js';
import {visibleMemberships,createMusicProviderAccount,mountMusicProviderAccountView,musicProviderButtons,musicProviderAccountPanel,
  validMusicProviderAccountMessage,MUSIC_PROVIDERS} from '../music-provider-account.js';
import {createMusicOnline} from '../music-online.js';
import {mountMusicView} from '../music-view.js';
import {initialMusicState,musicPage} from '../music.js';
import encoder from '../music-provider-qr/encoder.js';

const account = (provider,fields={})=>({provider,request:0,status:'signed-out',phase:'',
  userId:'',displayName:'',qrImage:'',qrText:'',error:'',...fields});
const message = (revision=0,patch={})=>({version:1,type:'music-provider-account-state',revision,
  accounts:MUSIC_PROVIDERS.map(provider=>account(provider,patch[provider]))});
const pending = (request,patch={})=>({request,status:'authorizing',phase:'waiting',qrText:'LUME-TEST-ONLY',...patch});
const avatarUrl='https://q1.qlogo.cn/g?b=qq&nk=10001&s=100';
const vipIconUrl='https://y.qq.com/mediastyle/lv-icon/v14/2x/vip5.png';
const svipIconUrl='https://y.qq.com/mediastyle/lv-icon/v14/2x/svip5.png';
const membershipExpiresAt=1893456000000;
const qqProfile = (fields={})=>account('qq',{status:'signed-in',userId:'10001',displayName:'QQ fixture',avatarUrl,
  memberships:[{kind:'qq-green-vip',label:'VIP',level:5,active:true,iconUrl:vipIconUrl,expiresAt:membershipExpiresAt},
    {kind:'qq-super-vip',label:'SVIP',level:5,active:false,iconUrl:svipIconUrl,expiresAt:0}],...fields});
const onlineTrack=(id,fields={})=>({id,title:id,artist:'Fixture',album:'',source:`online:${id}`,
  artwork:'',duration:180,...fields});
function fixture() {
  const sent=[];
  const controller=createMusicProviderAccount({now:()=>10,send:(action,fields)=>{sent.push({action,...fields});return true;}});
  return {controller,sent,request:()=>sent.at(-1).request,state:provider=>controller.snapshot().accounts.find(value=>value.provider===provider)};
}

test('provider bridge enforces exact three-account public contract and bounded commands',()=>{
  const sent=[],bridge=createBridge({postMessage:value=>sent.push(value)});
  assert.equal(bridge.musicProviderAccount('snapshot'),true);
  for (const provider of MUSIC_PROVIDERS) for (const action of ['begin','cancel','refresh','logout'])
    assert.equal(bridge.musicProviderAccount(action,{provider,request:1}),true);
  assert.deepEqual(sent[0],{version:1,scope:'music-provider-account',action:'snapshot'});
  for (const fields of [{provider:'other',request:1},{provider:'qq',request:0},{provider:'qq',request:-1},
    {provider:'qq',request:1.5},{provider:'qq',request:Number.MAX_SAFE_INTEGER+1},
    {provider:'qq',request:1,url:'https://example.invalid'},{provider:'qq',request:1,cookie:'not-a-cookie'}])
    assert.equal(bridge.musicProviderAccount('begin',fields),false);
  assert.equal(bridge.musicProviderAccount('snapshot',{provider:'qq'}),false);
  assert.equal(bridge.musicProviderAccount('openBrowser',{provider:'qq',request:1}),false);
  assert.equal(createBridge(null).musicProviderAccount('snapshot'),false);
  assert.equal(createBridge({postMessage(){throw Error('offline');}}).musicProviderAccount('snapshot'),false);
  assert.equal(validHostMessage(message()),true);
  for (const status of ['signed-out','loading','authorizing','signed-in','expired','error'])
    assert.equal(validHostMessage(message(1,{qq:{status}})),true);
  for (const error of ['qq-credential-code-104009','qq-credential-http-503',
    'qq-credential-network','qq-credential-malformed','qq-credential-missing-key',
    'rate-limited','not-confirmed','netease-code-8821','network-proxy'])
    assert.equal(validHostMessage(message(1,{qq:{status:'error',error}})),true);
  for (const patch of [{provider:'other'},{request:-1},{request:1.5},{request:Number.MAX_SAFE_INTEGER+1},
    {status:'authenticated'},{phase:'confirmed'},{userId:'a'.repeat(129)},{displayName:'a'.repeat(257)},
    {qrText:'a'.repeat(2049)},{qrImage:'data:image/png;base64,'},{qrImage:'data:image/svg+xml;base64,AAAA'},
    {qrImage:'https://example.invalid/qr.png'},{qrImage:'data:image/png;base64,AAAA" onerror="x'},
    {qrImage:'data:image/png;base64,'+'A'.repeat(350000)},{error:'raw service response'},
    {error:'qq-credential-code-1234567890'},{error:'qq-credential-code-104009-secret'},
    {error:'qq-credential-http-9999'},{error:'netease-code-8821-secret'},{error:'netease-code-'},
    {cookie:'secret'},{token:'secret'}])
    assert.equal(validHostMessage(message(1,{qq:patch})),false,JSON.stringify(Object.keys(patch)));
  for (const patch of [{revision:-1},{revision:1.1},{accounts:[]},{accounts:[account('qq'),account('qq'),account('qq')]},
    {accounts:[null,null,null]},{extra:'secret'},{version:2}])
    assert.equal(validMusicProviderAccountMessage({...message(),...patch}),false);
});

test('lazy snapshots, monotonic requests and revision fencing survive cancel, switch and reopen',()=>{
  const f=fixture();
  assert.deepEqual(f.sent,[]);
  assert.equal(f.controller.load(),true);assert.deepEqual(f.sent,[{action:'snapshot'}]);
  f.controller.receive(message(1,{qq:{request:50,status:'signed-in',userId:'fixture',displayName:'Fixture'}}));
  f.controller.open('netease');const first=f.request();assert.ok(first>50);
  f.controller.receive(message(2,{netease:pending(first)}));
  assert.equal(f.state('netease').qrText,'LUME-TEST-ONLY');
  f.controller.open('kugou');
  assert.deepEqual(f.sent.slice(-2).map(value=>[value.action,value.provider]),[['cancel','netease'],['begin','kugou']]);
  const second=f.request();
  f.controller.receive(message(3,{netease:pending(first,{status:'signed-in',userId:'late'}),
    kugou:pending(second)}));
  assert.equal(f.state('netease').status,'signed-out');
  assert.equal(f.state('kugou').status,'authorizing');
  assert.equal(f.controller.receive(message(2)),false);
  f.controller.close();const cancel=f.request();assert.ok(cancel>second);
  f.controller.receive(message(4,{kugou:pending(second)}));
  assert.equal(f.state('kugou').qrText,'');
  assert.equal(f.controller.snapshot().modal,'');
  f.controller.open('kugou');assert.ok(f.request()>cancel);
  f.controller.dispose();const count=f.sent.length;
  f.controller.dispose();f.controller.open('qq');f.controller.load();
  assert.equal(f.sent.length,count);
  assert.equal(f.controller.receive(message(99)),false);
});

test('success, refresh and logout do not claim catalogs or change another platform',()=>{
  const f=fixture();
  f.controller.open('qq');const request=f.request();
  f.controller.receive(message(1,{qq:pending(request,{status:'signed-in',userId:'fixture',displayName:'Test user'})}));
  assert.equal(f.state('qq').qrText,'','terminal replies never retain QR payloads');
  assert.equal(f.controller.snapshot().modal,'','successful login closes its QR dialog');
  assert.equal(f.sent.length,1,'successful login does not send cancel');
  f.controller.close();
  assert.equal(f.sent.at(-1).action,'begin','confirmed login is not cancelled');
  f.controller.open('qq');
  assert.equal(f.sent.length,1,'signed-in platform opens its account instead of restarting login');
  assert.equal(f.controller.action('refresh','qq'),true);
  assert.equal(f.controller.action('refresh','qq'),false,'single-flight refresh');
  assert.equal(f.state('qq').userId,'fixture');
  f.controller.receive(message(2,{qq:{request:f.request(),status:'signed-in',userId:'fixture'}}));
  f.controller.action('logout','qq');
  f.controller.receive(message(3,{qq:{request:f.request()}}));
  assert.equal(f.state('qq').status,'signed-out');
  assert.equal(f.state('netease').request,0);
  assert.equal('library' in f.controller.snapshot(),false);
});

test('stale or other-provider success cannot dismiss the active login',()=>{
  const f=fixture();
  f.controller.open('qq');const request=f.request();
  f.controller.receive(message(1,{qq:pending(request),netease:{status:'signed-in',userId:'other'}}));
  assert.equal(f.controller.snapshot().modal,'qq');
  f.controller.receive(message(2,{qq:pending(request-1,{status:'signed-in',userId:'stale'})}));
  assert.equal(f.controller.snapshot().modal,'qq');
  f.controller.receive(message(3,{qq:pending(request,{status:'signed-in',userId:'current'})}));
  assert.equal(f.controller.snapshot().modal,'');
  assert.deepEqual(f.sent.map(value=>value.action),['begin']);
});

test('QQ public profile is accepted by the UI protocol and retained by the controller',()=>{
  const profile=qqProfile(),state=message(1,{qq:profile}),f=fixture();
  assert.equal(validMusicProviderAccountMessage(state),true,'safe avatar and memberships are public profile fields');
  assert.equal(validHostMessage(state),true,'the host bridge must forward the QQ public profile');
  assert.equal(f.controller.receive(state),true);
  assert.equal(f.state('qq').avatarUrl,avatarUrl);
  assert.deepEqual(f.state('qq').memberships,profile.memberships);
  for (const fields of [{cookie:'secret'},{token:'secret'},{musicKey:'secret'}])
    assert.equal(validHostMessage(message(2,{qq:qqProfile(fields)})),false,'credentials never enter the public profile');
  for (const fields of [{kind:'vip'},{kind:'svip'},{level:'5'},{level:-1},{level:1.5},{level:1001}]) {
    const memberships=[{...profile.memberships[0],...fields}];
    assert.equal(validHostMessage(message(2,{qq:qqProfile({memberships})})),false,JSON.stringify(fields));
  }
  for (const level of [0,1000])
    assert.equal(validHostMessage(message(2,{qq:qqProfile({memberships:[{...profile.memberships[0],level}]})})),true);
});

test('membership expiresAt retains epoch milliseconds, with zero for unknown and no string dates',()=>{
  const profile=qqProfile(),f=fixture();
  assert.equal(f.controller.receive(message(1,{qq:profile})),true,'the C++ six-field membership payload must be accepted');
  assert.deepEqual(f.state('qq').memberships.map(item=>item.expiresAt),[membershipExpiresAt,0]);
  for (const expiresAt of [String(membershipExpiresAt),'2030-01-01T00:00:00Z',null,false,-1,1.5,Number.MAX_SAFE_INTEGER+1]) {
    const memberships=[{...profile.memberships[0],expiresAt}];
    assert.equal(validHostMessage(message(2,{qq:qqProfile({memberships})})),false,`expiresAt: ${JSON.stringify(expiresAt)}`);
  }
});

test('avatar and membership image URLs reject executable, local and credential-bearing sources',()=>{
  assert.equal(validHostMessage(message(1,{qq:qqProfile()})),true,'a valid profile must pass before testing URL rejection');
  const unsafe=['javascript:alert(1)','file:///C:/fixture.png','data:image/svg+xml;base64,PHN2Zz4=',
    'https://user:secret@q1.qlogo.cn/fixture.png','//q1.qlogo.cn/fixture.png'];
  for (const url of unsafe) {
    assert.equal(validHostMessage(message(2,{qq:qqProfile({avatarUrl:url})})),false,`avatar: ${url}`);
    const memberships=[{...qqProfile().memberships[0],iconUrl:url}];
    assert.equal(validHostMessage(message(2,{qq:qqProfile({memberships})})),false,`membership: ${url}`);
  }
});

test('account panel has one active account and no platform switchers or connect controls',()=>{
  const panel=musicProviderAccountPanel();
  assert.equal((panel.match(/\bdata-music-provider-active(?=[\s>])/g)||[]).length,1);
  assert.doesNotMatch(panel,/\bMusicProvider(?:Switchers|Switcher|AccountConnect)\b/);
  assert.doesNotMatch(panel,/\bdata-music-provider-select\b/);
});

test('guest account panel offers guest mode and a home-page login entry',()=>{
  const panel=musicProviderAccountPanel();
  assert.match(panel,/游客模式/);
  assert.match(panel,/<button\b[^>]*data-music-action="account-home"[^>]*>[\s\S]*?首页[\s\S]*?登录[\s\S]*?<\/button>/);
});

for (const action of ['home','search']) {
  test(`online ${action} next appends and deduplicates results, with no duplicate request while loading`,t=>{
    const sent=[],online=createMusicOnline({now:()=>10,send:(action,fields)=>{sent.push({action,...fields});return true;}});
    t.after(()=>online.dispose());
    online.accounts(message(1,{netease:{status:'signed-in',userId:'fixture'}}));
    online.setVisible(true);
    if(action==='search')assert.equal(online.request('search',{query:'Fixture'}),true);
    const reply=(ids,more)=>{
      const {action,request,provider}=sent.at(-1);
      const collections=ids.map(id=>({id,providerId:provider,type:'playlist',title:id,artwork:'',description:'',owned:true}));
      return online.receive({version:1,type:'music-online-state',action,request,provider,status:'ready',error:'',
        data:{tracks:ids.map(id=>onlineTrack(id)),playlists:collections,recommendations:collections,more}});
    };
    assert.equal(reply(['netease:a','netease:b'],true),true);
    assert.equal(online.next(),true);
    assert.equal(sent.at(-1).action,action);
    assert.equal(sent.at(-1).offset,50);
    assert.deepEqual(online.snapshot().tracks.map(track=>track.id),['netease:a','netease:b'],'existing rows stay while appending');
    const count=sent.length;
    assert.equal(online.next(),false);
    assert.equal(sent.length,count,'loading blocks repeated next commands');
    assert.equal(reply(['netease:b','netease:c'],false),true);
    const state=online.snapshot();
    assert.deepEqual(state.tracks.map(track=>track.id),['netease:a','netease:b','netease:c']);
    if(action==='search')assert.deepEqual(state.searchTracks,state.tracks);
    else for(const key of ['playlists','recommendations'])
      assert.deepEqual(state[key].map(item=>item.id),['netease:a','netease:b','netease:c']);
    assert.equal(state.loading,false);
    assert.equal(online.next(),false,'exhausted lists do not request more');
  });
}

test('online lists show official QQ membership images without pagination; local lists keep page controls',()=>{
  const tracks=[onlineTrack('qq:a',{membershipBadge:'qq-green-vip'}),
    onlineTrack('qq:b',{membershipBadge:'qq-super-vip'})];
  const online=musicPage(initialMusicState(),{view:{screen:'home',nav:'favorites'},
    library:{signedIn:true,providerId:'qq',tracks,favorites:tracks,more:true,offset:50}});
  assert.match(online,new RegExp(`<img\\b[^>]*src="${vipIconUrl.replaceAll('.','\\.')}"`));
  assert.match(online,new RegExp(`<img\\b[^>]*src="${svipIconUrl.replaceAll('.','\\.')}"`));
  assert.doesNotMatch(online,/MusicPagination|data-music-action="online-(?:more|previous)"/);
  const local=musicPage(initialMusicState(),{view:{screen:'home',nav:'local'},
    library:{localTracks:[],localTotal:100,localOffset:50,localLimit:50,localMore:true}});
  assert.match(local,/data-music-action="local-previous"/);
  assert.match(local,/data-music-action="local-more"/);
});

test('transport failure fences old replies and synchronous receive is not overwritten',()=>{
  const controller=createMusicProviderAccount({now:()=>1,send:()=>false});
  controller.open('qq');
  assert.equal(controller.snapshot().accounts[1].error,'service');
  controller.receive(message(1,{qq:pending(1)}));
  assert.equal(controller.snapshot().accounts[1].error,'service');
  let sync;
  sync=createMusicProviderAccount({now:()=>1,send:(_,fields)=>{
    sync.receive(message(1,{qq:pending(fields.request)}));return true;
  }});
  sync.open('qq');
  assert.equal(sync.snapshot().accounts[1].status,'authorizing');
});

test('auth implementation has no playback owner, browser login, persistence or polling dependency',()=>{
  const source=readFileSync(new URL('../music-provider-account.js',import.meta.url),'utf8');
  assert.doesNotMatch(source,/from ['"].\/(?:music|music-view|account)\.js/);
  assert.doesNotMatch(source,/setInterval|setTimeout|localStorage|sessionStorage|fetch\(|window\.open|location\s*[.=]|<a\b/);
});

test('local QR bundle produces a genuine matrix and has recorded source/license provenance',()=>{
  const qr=encoder.create('LUME-TEST-ONLY',{errorCorrectionLevel:'M'});
  assert.equal(qr.modules.size,21);
  assert.ok(qr.modules.data.some(value=>value===1));
  assert.ok(qr.modules.data.some(value=>value===0));
  const manifest=JSON.parse(readFileSync(new URL('../music-provider-qr/SOURCES.json',import.meta.url)));
  const bytes=readFileSync(new URL('../music-provider-qr/encoder.js',import.meta.url));
  assert.equal(createHash('sha256').update(bytes).digest('hex'),manifest.encoderSha256);
  assert.equal(manifest.packages.qrcode,'1.5.4');
  assert.ok(Object.keys(manifest.sources).some(value=>value.includes('dijkstra')));
  assert.match(readFileSync(new URL('../music-provider-qr/LICENSE.txt',import.meta.url),'utf8'),/Permission is hereby granted/);
});

const jsdomPath=process.env.LUME_JSDOM_PATH;
const {JSDOM}=jsdomPath?createRequire(import.meta.url)(jsdomPath):{};
function domFixture(t,options={},markup=musicProviderButtons()) {
  const dom=new JSDOM('<main></main>',{url:'https://lume.local/',pretendToBeVisual:true});
  const root=dom.window.document.querySelector('main'),f=fixture();
  root.innerHTML=markup;
  const view=mountMusicProviderAccountView(root,f.controller,options);
  t.after(()=>{view.dispose();f.controller.dispose();dom.window.close();});
  const find=selector=>{const node=dom.window.document.querySelector(selector);assert.ok(node,selector);return node;};
  return {...f,dom,root,view,find,key:key=>dom.window.document.activeElement.dispatchEvent(
    new dom.window.KeyboardEvent('keydown',{key,bubbles:true,cancelable:true}))};
}
test('signed-in panel renders only the selected profile, real images, logout and data sync',{skip:!jsdomPath},t=>{
  const f=domFixture(t,{},musicProviderAccountPanel());
  f.controller.select('qq');
  assert.equal(f.controller.receive(message(1,{qq:qqProfile(),
    netease:{status:'signed-in',userId:'netease-user',displayName:'Other account'}})),true);
  const row=f.find('[data-music-provider-active]');
  assert.equal(f.root.querySelectorAll('[data-music-provider-active]').length,1);
  assert.equal(row.dataset.provider,'qq');
  assert.equal(f.find('[data-music-provider-active-name]').textContent,'QQ fixture');
  assert.doesNotMatch(row.textContent,/Other account/);
  assert.deepEqual([...row.querySelectorAll('img')].map(node=>node.getAttribute('src')).sort(),
    [avatarUrl,vipIconUrl].sort(),'a green-only account shows only its active green badge');
  assert.equal(f.root.querySelector('.MusicProviderSwitchers, .MusicProviderSwitcher, .MusicProviderAccountConnect'),null);
  const logout=f.find('[data-music-provider-logout]'),sync=f.find('[data-music-provider-refresh]');
  assert.equal(logout.hidden,false);
  assert.ok(logout.querySelector('svg'),'logout is an icon button');
  assert.equal(sync.hidden,false);
  assert.match(sync.textContent,/同步数据/);
  sync.click();
  assert.deepEqual([f.sent.at(-1).action,f.sent.at(-1).provider],['refresh','qq']);
});
test('modal traps focus, escapes to opener, never displays raw QR text, and drops late encoding',{skip:!jsdomPath},async t=>{
  let resolve;
  const f=domFixture(t,{encode:()=>new Promise(done=>{resolve=done;})});
  const opener=f.find('[data-music-provider-open="qq"]');opener.focus();opener.click();
  assert.equal(f.dom.window.document.activeElement,f.find('[data-provider-close]'));
  assert.equal(f.find('dialog').getAttribute('aria-modal'),'true');
  f.controller.receive(message(1,{qq:pending(f.request())}));
  await Promise.resolve();
  assert.equal(f.root.textContent.includes('LUME-TEST-ONLY'),false);
  assert.equal(f.find('dialog').querySelector('a,iframe,input'),null);
  f.find('[data-provider-select="kugou"]').focus();f.key('Tab');
  assert.equal(f.dom.window.document.activeElement,f.find('[data-provider-close]'));
  f.key('Escape');
  assert.equal(f.dom.window.document.querySelector('dialog'),null);
  assert.equal(f.dom.window.document.activeElement,opener);
  assert.equal(f.sent.at(-1).action,'cancel');
  resolve('data:image/png;base64,AAAA');await Promise.resolve();await Promise.resolve();
  assert.equal(f.dom.window.document.querySelector('dialog'),null);
});

for (const end of ['native-hide','document-hide','pagehide','dispose']) {
  test(`modal cancels exactly once on ${end}`,{skip:!jsdomPath},t=>{
    const f=domFixture(t);
    f.find('[data-music-provider-open="netease"]').click();
    if(end==='native-hide')f.view.setVisible(false);
    else if(end==='document-hide') {
      Object.defineProperty(f.dom.window.document,'hidden',{value:true,configurable:true});
      f.dom.window.document.dispatchEvent(new f.dom.window.Event('visibilitychange'));
    } else if(end==='pagehide')f.dom.window.dispatchEvent(new f.dom.window.Event('pagehide'));
    else f.view.dispose();
    f.view.dispose();
    assert.equal(f.sent.filter(value=>value.action==='cancel').length,1);
    assert.equal(f.dom.window.document.querySelector('dialog'),null);
  });
}
test('encoding failure has controlled retry and platform switch discards previous QR',{skip:!jsdomPath},async t=>{
  const f=domFixture(t,{encode:()=>Promise.reject(Error('must not surface raw text'))});
  f.find('[data-music-provider-open="qq"]').click();
  f.controller.receive(message(1,{qq:pending(f.request())}));
  await new Promise(resolve=>setTimeout(resolve,0));
  assert.equal(f.find('[data-provider-retry]').hidden,false);
  assert.doesNotMatch(f.find('dialog').textContent,/must not surface/);
  const previous=f.request();
  f.find('[data-provider-retry]').click();
  assert.deepEqual(f.sent.slice(-2).map(value=>value.action),['cancel','begin']);
  assert.ok(f.request()>previous);
  f.find('[data-provider-select="kugou"]').click();
  assert.equal(f.controller.snapshot().modal,'kugou');
  assert.equal(f.find('.MusicProviderQr img').hasAttribute('src'),false);
});
test('provider updates leave stage, playback clock calls, controls and local queue untouched',{skip:!jsdomPath},t=>{
  const dom=new JSDOM('<main></main>',{url:'https://lume.local/',pretendToBeVisual:true});
  const root=dom.window.document.querySelector('main'),f=fixture(),updates=[];
  const track={id:'local-test',title:'Local',artist:'Fixture',album:'',artwork:'',source:'C:/fixture.mp3',duration:100};
  const snapshot={...initialMusicState(),session:1,pageVisible:true,current:track,queue:[track],playing:true,
    timeline:{position:12,duration:100}};
  const view=mountMusicView(root,{snapshot,providerAccount:f.controller,
    viewState:{screen:'player',panel:'lyrics',panelTab:'account'},
    createBackground:()=>({update(){},dispose(){}}),
    createEffects:()=>({update(){},dispose(){}}),
    createVisualizer:()=>({update:state=>updates.push(state),dispose(){}})});
  t.after(()=>{view.dispose();f.controller.dispose();dom.window.close();});
  const queueBefore=view.snapshot().queue;
  const stage=root.querySelector('[data-music-stage]'),range=root.querySelector('[data-music-progress]');
  f.controller.open('qq');
  const count=updates.length,request=f.request();
  for (let revision=1;revision<12;revision++)
    f.controller.receive(message(revision,{qq:pending(request,{qrText:'',phase:revision%2?'waiting':'scanned'})}));
  f.controller.receive(message(12,{qq:{request,status:'signed-in',userId:'test',displayName:'<script>fixture</script>'}}));
  assert.equal(updates.length,count,'auth never calls visualizer.update or reanchors its clock');
  assert.equal(root.querySelector('[data-music-stage]'),stage);
  assert.equal(root.querySelector('[data-music-progress]'),range);
  assert.deepEqual(view.snapshot().queue,queueBefore);
  assert.equal(root.querySelector('script'),null);
  assert.equal(view.snapshot().account.signedIn,false,'Lume account is separate');
  const name=root.querySelector('[data-music-provider-active-name]');
  assert.equal(name.textContent,'<script>fixture</script>');
  assert.equal(name.childElementCount,0,'nickname remains text, never parsed HTML');
  assert.equal(root.querySelector('[data-music-provider-active]').dataset.provider,'qq');
  view.update({...snapshot,account:{signedIn:true}},{syncClock:false});
  assert.equal(root.querySelector('[data-music-progress]'),range,'Lume account projection does not rebuild music chrome');
  view.dispose();assert.equal(dom.window.document.querySelector('dialog'),null);
});

test("account card shows one badge: the highest active membership tier", () => {
  const green={kind:'qq-green-vip',label:'绿钻',level:5,active:true,iconUrl:vipIconUrl,expiresAt:0};
  const svip={kind:'qq-super-vip',label:'超级会员',level:5,active:true,iconUrl:svipIconUrl,expiresAt:0};
  assert.deepEqual(visibleMemberships([green,svip]).map(item=>item.kind),['qq-super-vip']);
  assert.deepEqual(visibleMemberships([green,{...svip,active:false}]).map(item=>item.kind),['qq-green-vip']);
  assert.deepEqual(visibleMemberships([{...green,active:false},svip]).map(item=>item.kind),['qq-super-vip']);
  assert.deepEqual(visibleMemberships([{...green,active:false},{...svip,active:false}]).map(item=>item.kind),['qq-green-vip']);
  assert.deepEqual(visibleMemberships([]),[]);
  assert.deepEqual(visibleMemberships(undefined),[]);
});

test('connect buttons switch to signed-in platforms and open QR login for the rest',{skip:!jsdomPath},t=>{
  const selected=[];
  const f=domFixture(t,{onSelect:provider=>selected.push(provider)},musicProviderAccountPanel()+musicProviderButtons());
  f.view.setVisible(true);
  f.controller.select('qq');
  assert.equal(f.root.querySelector('.MusicProviderTabs'),null,'no platform tab row in the account panel');
  assert.equal(f.controller.receive(message(1,{qq:qqProfile(),
    netease:{status:'signed-in',userId:'netease-user',displayName:'NetEase fixture'}})),true);
  const tab=provider=>f.find(`.MusicProviderButton[data-music-provider-open="${provider}"]`);
  assert.equal(tab('netease').querySelector('[data-music-provider-label]').textContent,'\u5207\u6362\u5230\u7f51\u6613\u4e91\u97f3\u4e50');
  assert.equal(tab('qq').dataset.signedIn,'true');
  assert.equal(tab('kugou').dataset.signedIn,'false');
  assert.match(tab('kugou').querySelector('[data-music-provider-label]').textContent,/^\u8fde\u63a5/);
  assert.match(musicProviderButtons([{provider:'kugou',status:'signed-in',userId:'8'}]),
    /data-music-provider-open="kugou" data-signed-in="true">[^]*?\u5207\u6362\u5230/,'guest page markup starts with the right labels');
  tab('netease').click();
  assert.equal(f.controller.snapshot().selected,'netease','a signed-in platform is switched to, not logged into again');
  assert.equal(f.find('[data-music-provider-active-name]').textContent,'NetEase fixture');
  assert.equal(f.sent.some(item=>item.action==='begin'),false,'switching never starts a QR login');
  assert.equal(f.state('qq').status,'signed-in','the other account stays signed in');
  tab('kugou').click();
  assert.deepEqual([f.sent.at(-1).action,f.sent.at(-1).provider],['begin','kugou'],'an unsigned platform opens its QR login');
  assert.deepEqual(selected,['netease','kugou']);
});
test('platforms without badge artwork show the tier as text with its expiry',{skip:!jsdomPath},t=>{
  const f=domFixture(t,{},musicProviderAccountPanel());
  f.controller.select('netease');
  assert.equal(f.controller.receive(message(1,{
    netease:{status:'signed-in',userId:'7',displayName:'NetEase fixture',avatarUrl:'https://p1.music.126.net/a.jpg',
      memberships:[{kind:'netease-vip',label:'黑胶VIP',level:2,active:true,iconUrl:'',expiresAt:membershipExpiresAt},
        {kind:'netease-svip',label:'黑胶SVIP',level:0,active:false,iconUrl:'',expiresAt:0}]},
    kugou:{status:'signed-in',userId:'8',displayName:'KuGou fixture',
      memberships:[{kind:'kugou-vip',label:'酷狗VIP',level:0,active:false,iconUrl:''}]}})),true);
  let badge=f.find('[data-music-provider-memberships] .MusicProviderMembershipText');
  assert.equal(badge.textContent,'黑胶VIP');
  assert.equal(badge.dataset.kind,'netease-vip');
  assert.equal(badge.classList.contains('is-inactive'),false);
  assert.match(badge.title,/^黑胶VIP · \d{4}-\d{2}-\d{2} 到期$/);
  assert.equal(f.find('[data-music-provider-avatar]').getAttribute('src'),'https://p1.music.126.net/a.jpg');
  f.controller.select('kugou');
  badge=f.find('[data-music-provider-memberships] .MusicProviderMembershipText');
  assert.equal(badge.textContent,'酷狗VIP');
  assert.equal(badge.classList.contains('is-inactive'),true);
  assert.equal(badge.title,'酷狗VIP（未开通）');
});
