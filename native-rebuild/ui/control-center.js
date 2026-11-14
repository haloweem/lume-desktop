// Lume control center (control-center.html), opened from the Dock. The native
// window (platform/windows/control_center) runs every system call and sends
// the state; the page only renders it and asks for changes.
import {escapeHTML as h} from './dom.js';
import {setLanguage,t} from './localization.js';

const webview=globalThis.chrome?.webview;
const post=(action,fields={})=>{try{webview?.postMessage({version:1,scope:'control-center',action,...fields});}catch{}};
const root=document.querySelector('[data-cc]');

const svg=path=>`<svg viewBox="0 0 20 20" aria-hidden="true">${path}</svg>`;
// A gear: eight rounded teeth around a ring, drawn as one even-odd shape.
const gearPath=(()=>{
  const c=10,outer=8.4,inner=6.5,hole=2.6,teeth=8,parts=[];
  for(let i=0;i<teeth;i++){
    const a=i*2*Math.PI/teeth,w=Math.PI/teeth*.52;
    const pt=(r,ang)=>`${(c+r*Math.cos(ang)).toFixed(2)} ${(c+r*Math.sin(ang)).toFixed(2)}`;
    parts.push(`${i?'L':'M'}${pt(inner,a-w*1.45)}L${pt(outer,a-w)}A${outer} ${outer} 0 0 1 ${pt(outer,a+w)}L${pt(inner,a+w*1.45)}A${inner} ${inner} 0 0 1 ${pt(inner,a+2*Math.PI/teeth-w*1.45)}`);
  }
  return `${parts.join('')}Z M${c+hole} ${c}A${hole} ${hole} 0 1 0 ${c-hole} ${c}A${hole} ${hole} 0 1 0 ${c+hole} ${c}Z`;
})();
const wifiArcs=(level=4,lock=false)=>{
  const arc=(d,on)=>`<path d="${d}" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" opacity="${on?1:.28}"/>`;
  return svg(arc('M2.5 7.6a10.6 10.6 0 0 1 15 0',level>=4)+arc('M5 10.2a7 7 0 0 1 10 0',level>=3)+arc('M7.5 12.8a3.4 3.4 0 0 1 5 0',level>=2)+
    `<circle cx="10" cy="15.6" r="1.3" fill="currentColor"/>`+(lock?'<rect x="13.2" y="12.6" width="5" height="4.4" rx="1" fill="currentColor"/><path d="M14.3 12.7v-1a1.4 1.4 0 0 1 2.8 0v1" fill="none" stroke="currentColor" stroke-width="1.2"/>':''));
};
const icon={
  wifi:wifiArcs(),
  ethernet:svg('<rect x="4" y="5" width="12" height="9" rx="2" fill="none" stroke="currentColor" stroke-width="1.6"/><path d="M7 14v2.5M10 14v2.5M13 14v2.5M7 8v2M10 8v2M13 8v2" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>'),
  offline:svg('<path d="M3 3l14 14M5 10.2a7 7 0 0 1 4-2M2.5 7.6A10.6 10.6 0 0 1 7 5M12.8 7.6a7 7 0 0 1 2.2 2.6" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/><circle cx="10" cy="15.6" r="1.3" fill="currentColor"/>'),
  bluetooth:svg('<path d="m6 6.5 8 7-4 3.5v-14l4 3.5-8 7" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"/>'),
  hotspot:svg('<rect x="6" y="2.5" width="8" height="15" rx="2" fill="none" stroke="currentColor" stroke-width="1.6"/><path d="M9 15h2" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/><path d="M2.8 6.5a8 8 0 0 0 0 7M17.2 6.5a8 8 0 0 1 0 7" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>'),
  plane:svg('<path d="M10 2.5c.8 0 1.3.8 1.3 1.6v4.2l5.2 3v1.6l-5.2-1.5v3.3l1.6 1.2v1.3L10 16.4l-2.9.8v-1.3l1.6-1.2v-3.3l-5.2 1.5v-1.6l5.2-3V4.1c0-.8.5-1.6 1.3-1.6z" fill="currentColor"/>'),
  volume:svg('<path d="M3.5 7.5h3l4-3.5v12l-4-3.5h-3z" fill="currentColor"/><path d="M13.5 7a4 4 0 0 1 0 6M15.6 4.8a7 7 0 0 1 0 10.4" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/>'),
  muted:svg('<path d="M3.5 7.5h3l4-3.5v12l-4-3.5h-3z" fill="currentColor"/><path d="m13.5 8 4 4M17.5 8l-4 4" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/>'),
  sun:svg('<circle cx="10" cy="10" r="3.3" fill="none" stroke="currentColor" stroke-width="1.6"/><path d="M10 2.4v1.8M10 15.8v1.8M2.4 10h1.8M15.8 10h1.8M4.6 4.6l1.3 1.3M14.1 14.1l1.3 1.3M4.6 15.4l1.3-1.3M14.1 5.9l1.3-1.3" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/>'),
  gear:svg(`<path d="${gearPath}" fill="currentColor" fill-rule="evenodd"/>`),
  chevron:svg('<path d="m6 8 4 4 4-4" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"/>'),
  right:svg('<path d="m8 5 5 5-5 5" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/>'),
  back:svg('<path d="m12 4.5-5.5 5.5 5.5 5.5" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/>'),
  up:svg('<path d="m6 12 4-4 4 4" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"/>'),
  check:svg('<path d="m5 10.4 3.2 3.1L15 6.8" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>'),
  refresh:svg('<path d="M15.5 6.5A6 6 0 1 0 16 11" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/><path d="M16 3v4h-4" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/>'),
  device:svg('<rect x="3" y="5" width="14" height="10" rx="2" fill="none" stroke="currentColor" stroke-width="1.5"/><path d="M7 17.5h6" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/>'),
  tray:svg('<rect x="3" y="4" width="14" height="12" rx="3" fill="none" stroke="currentColor" stroke-width="1.6"/><circle cx="7" cy="12" r="1.2" fill="currentColor"/><circle cx="10" cy="12" r="1.2" fill="currentColor"/><circle cx="13" cy="12" r="1.2" fill="currentColor"/>'),
  battery:svg('<rect x="2" y="6" width="14" height="8" rx="2" fill="none" stroke="currentColor" stroke-width="1.4"/><path d="M17.5 8.5v3" stroke="currentColor" stroke-width="1.6" stroke-linecap="round"/>'),
};
const signalLevel=signal=>signal>=75?4:signal>=50?3:signal>=25?2:1;
const hotspotReason=reason=>({'no-network':t('未连接网络','No network'),policy:t('已被策略禁用','Blocked by policy'),
  hardware:t('网卡不支持','Not supported by the adapter')}[reason]||t('此电脑不支持','Not supported'));

let state=null;
// Values the user just changed win over refreshes until the system confirms them.
const pending=new Map();
const hold=(key,value,ms=2500)=>pending.set(key,{value,until:Date.now()+ms});
const valueOf=(key,actual)=>{const p=pending.get(key);if(!p)return actual;if(p.value===actual||Date.now()>p.until){pending.delete(key);return actual;}return p.value;};
let dragging=null,openPicker=null,view='main',expanded='',password='',typing=false,justExpanded=false;

// --- main page -----------------------------------------------------------------
function tile(key,title,sub,on,available,glyph,detail){
  const toggle=`<button type="button" class="cc-tile__main" data-cc-toggle="${key}" role="switch" aria-checked="${on}" aria-disabled="${!available}">
    <span class="cc-tile__icon">${glyph}</span><span class="cc-tile__text"><b>${h(title)}</b><small>${h(sub)}</small></span></button>`;
  const more=detail&&available?`<button type="button" class="cc-tile__more" data-cc-view="${key}" aria-label="${h(title)} ${t('列表','list')}">${icon.right}</button>`:'';
  return `<div class="cc-tile${on?' is-on':''}${more?' is-split':''}" aria-disabled="${!available}">${toggle}${more}</div>`;
}
function picker(key,label,choices,current){
  if(!choices.length)return '';
  const chosen=choices.find(item=>item.id===current)||choices[0];
  return `<div class="cc-picker${openPicker===key?' is-open':''}" data-cc-picker="${key}">
    <button type="button" data-cc-open="${key}" aria-haspopup="menu" aria-expanded="${openPicker===key}" title="${h(chosen.name)}"><span>${h(label)} · ${h(chosen.name)}</span>${icon.chevron}</button>
    <div class="cc-menu" role="menu">${choices.map(item=>`<button type="button" role="menuitemradio" data-cc-choose="${key}" data-id="${h(item.id)}" aria-checked="${item.id===chosen.id}">${icon.check}<span>${h(item.name)}</span></button>`).join('')}</div></div>`;
}
function slider(key,level,glyph,muted,available,missing){
  if(!available)return `<div class="cc-unavailable">${glyph}<span>${h(missing)}</span></div>`;
  return `<div class="cc-slider${muted?' is-muted':''}" data-cc-slider="${key}">
    ${key==='volume'?`<button type="button" class="cc-icon" data-cc-mute aria-label="${muted?t('取消静音','Unmute'):t('静音','Mute')}">${muted?icon.muted:icon.volume}</button>`:`<span class="cc-icon is-static">${glyph}</span>`}
    <input class="cc-range" type="range" min="0" max="100" step="1" value="${level}" data-cc-range="${key}" aria-label="${key==='volume'?t('音量','Volume'):t('亮度','Brightness')}" style="--p:${level}%">
    <output>${level}%</output></div>`;
}
function mainPage(s){
  const wifiOn=valueOf('wifi',s.wifi.on),btOn=valueOf('bluetooth',s.bluetooth.on),hotOn=valueOf('hotspot',s.hotspot.on),airOn=valueOf('airplane',s.airplane.on);
  const volume=valueOf('volume',s.audio.volume),muted=valueOf('mute',s.audio.muted),bright=valueOf('brightness',s.brightness.level);
  const network=s.wifi.on&&s.wifi.ssid?[icon.wifi,s.wifi.ssid]:s.ethernet?[icon.ethernet,t('以太网','Ethernet')]:[icon.offline,t('未连接','Not connected')];
  const battery=s.battery.available?`<span class="cc-battery" title="${s.battery.charging?t('充电中','Charging'):''}">${icon.battery}${s.battery.percent}%${s.battery.charging?' ⚡':''}</span>`:'';
  const wifiSub=!s.wifi.available?t('此电脑没有 Wi-Fi','No Wi-Fi adapter'):!wifiOn?t('已关闭','Off'):s.wifi.ssid||t('未连接','Not connected');
  const btSub=!s.bluetooth.available?t('此电脑没有蓝牙','No Bluetooth'):btOn?t('已开启','On'):t('已关闭','Off');
  const hotSub=!s.hotspot.available?hotspotReason(s.hotspot.reason):hotOn?t('已开启','On'):t('共享当前网络','Share this connection');
  const airSub=!s.airplane.available?t('没有无线设备','No wireless radios'):airOn?t('无线已全部关闭','All wireless off'):t('关闭 Wi-Fi 和蓝牙','Turns off Wi-Fi and Bluetooth');
  const pickers=[picker('output',t('输出','Output'),s.audio.outputs,s.audio.output),picker('display',t('亮度','Brightness'),s.brightness.displays,s.brightness.display)].filter(Boolean);
  return `<header class="cc-head"><h1>${t('控制中心','Control center')}</h1>
      <div class="cc-chip" title="${h(network[1])}">${network[0]}<span>${h(network[1])}</span></div>${battery}
      <button type="button" class="cc-icon" data-cc-settings="home" aria-label="${t('Windows 设置','Windows Settings')}">${icon.gear}</button></header>
    <div class="cc-grid">
      ${tile('wifi','Wi-Fi',wifiSub,wifiOn&&s.wifi.available,s.wifi.available,icon.wifi,true)}
      ${tile('bluetooth',t('蓝牙','Bluetooth'),btSub,btOn&&s.bluetooth.available,s.bluetooth.available,icon.bluetooth,true)}
      ${tile('hotspot',t('移动热点','Mobile hotspot'),hotSub,hotOn&&s.hotspot.available,s.hotspot.available,icon.hotspot)}
      ${tile('airplane',t('飞行模式','Airplane mode'),airSub,airOn&&s.airplane.available,s.airplane.available,icon.plane)}
    </div>
    ${slider('volume',volume,icon.volume,muted,s.audio.available,t('没有可用的音频输出','No audio output'))}
    ${slider('brightness',bright,icon.sun,false,s.brightness.available,t('当前显示器不支持调节亮度','Brightness cannot be adjusted on this display'))}
    ${pickers.length?`<div class="cc-pickers${pickers.length===1?' is-single':''}">${pickers.join('')}</div>`:''}
    ${s.tray?`<button type="button" class="cc-tray" data-cc-tray>${icon.tray}<span>${t('系统托盘图标','Notification icons')}</span>${icon.up}</button>`:''}`;
}

// --- list pages ----------------------------------------------------------------
function detailHead(key,title,on){
  return `<header class="cc-detail-head"><button type="button" class="cc-icon" data-cc-back aria-label="${t('返回','Back')}">${icon.back}</button>
    <h2>${h(title)}</h2><button type="button" class="cc-switch${on?' is-on':''}" data-cc-power="${key}" role="switch" aria-checked="${on}" aria-label="${h(title)}"></button></header>`;
}
function joinNote(join,ssid){
  if(!join||join.ssid!==ssid)return '';
  return {started:`<p class="cc-note">${t('正在连接…','Connecting…')}</p>`,
    failed:`<p class="cc-note is-error">${t('无法连接，请检查密码后重试。','Could not connect. Check the password and try again.')}</p>`,
    'needs-password':`<p class="cc-note is-error">${t('密码需要 8 到 63 个字符。','The password needs 8 to 63 characters.')}</p>`,
    unsupported:`<p class="cc-note">${t('这个网络需要在 Windows 设置中连接。','Connect to this network in Windows Settings.')}</p>`}[join.status]||'';
}
function wifiPage(s){
  const on=valueOf('wifi',s.wifi.on);
  const rows=(s.networks||[]).map(n=>{
    const open=expanded===n.ssid,join=s.join&&s.join.ssid===n.ssid?s.join:null;
    const sub=n.connected?(n.secured?t('已连接，安全','Connected, secured'):t('已连接','Connected')):join?.status==='started'?t('正在连接…','Connecting…'):n.secured?t('安全','Secured'):t('开放','Open');
    let action='';
    if(open){
      if(n.connected)action=`<div class="cc-row__actions${justExpanded?' is-new':''}"><button type="button" class="cc-button" data-cc-disconnect>${t('断开连接','Disconnect')}</button></div>`;
      else if(n.secured&&!n.known)action=`<div class="cc-row__actions${justExpanded?' is-new':''}"><input class="cc-input" type="password" data-cc-password placeholder="${t('输入网络安全密钥','Enter the network security key')}" value="${h(password)}" maxlength="63" autocomplete="off">
        <button type="button" class="cc-button is-primary" data-cc-connect="${h(n.ssid)}">${t('连接','Connect')}</button></div>${joinNote(s.join,n.ssid)}`;
      else action=`<div class="cc-row__actions${justExpanded?' is-new':''}"><button type="button" class="cc-button is-primary" data-cc-connect="${h(n.ssid)}">${t('连接','Connect')}</button></div>${joinNote(s.join,n.ssid)}`;
      if(join?.status==='unsupported')action+=`<button type="button" class="cc-link" data-cc-settings="wifi-more">${t('打开 Wi-Fi 设置','Open Wi-Fi settings')}</button>`;
    }
    return `<li class="cc-row${n.connected?' is-connected':''}${open?' is-open':''}"><button type="button" class="cc-row__main" data-cc-network="${h(n.ssid)}" aria-expanded="${open}">
      <span class="cc-row__icon">${wifiArcs(signalLevel(n.signal),n.secured)}</span><span class="cc-row__text"><b>${h(n.ssid)}</b><small>${h(sub)}</small></span></button>${action}</li>`;
  }).join('');
  const body=!on?`<p class="cc-empty">${t('Wi-Fi 已关闭','Wi-Fi is off')}</p>`:s.networks?(rows?`<ul class="cc-list">${rows}</ul>`:`<p class="cc-empty">${t('附近没有找到网络','No networks found nearby')}</p>`):`<p class="cc-empty">${t('正在搜索网络…','Looking for networks…')}</p>`;
  return `${detailHead('wifi','Wi-Fi',on)}${body}
    <footer class="cc-detail-foot"><button type="button" class="cc-link" data-cc-settings="wifi-more">${t('更多 Wi-Fi 设置','More Wi-Fi settings')}</button>
      <button type="button" class="cc-icon" data-cc-scan aria-label="${t('刷新','Refresh')}">${icon.refresh}</button></footer>`;
}
function bluetoothPage(s){
  const on=valueOf('bluetooth',s.bluetooth.on);
  const rows=(s.devices||[]).map(d=>`<li class="cc-row${d.connected?' is-connected':''}"><button type="button" class="cc-row__main" data-cc-settings="bluetooth">
      <span class="cc-row__icon">${icon.device}</span><span class="cc-row__text"><b>${h(d.name)}</b><small>${d.connected?t('已连接','Connected'):t('已配对','Paired')}</small></span></button></li>`).join('');
  const body=!on?`<p class="cc-empty">${t('蓝牙已关闭','Bluetooth is off')}</p>`:s.devices?(rows?`<ul class="cc-list">${rows}</ul>`:`<p class="cc-empty">${t('还没有配对的设备','No paired devices yet')}</p>`):`<p class="cc-empty">${t('正在读取设备…','Reading devices…')}</p>`;
  return `${detailHead('bluetooth',t('蓝牙','Bluetooth'),on)}${body}
    <p class="cc-hint">${t('连接、断开或添加设备请在蓝牙设置中进行。','Connect, disconnect or add devices in Bluetooth settings.')}</p>
    <footer class="cc-detail-foot"><button type="button" class="cc-link" data-cc-settings="bluetooth">${t('更多蓝牙设置','More Bluetooth settings')}</button></footer>`;
}

let markup='',heightFrame=null,lastHeight=-1;
function measureHeight(force=false){
  if(force)lastHeight=-1;
  if(heightFrame!==null)return;
  heightFrame=requestAnimationFrame(()=>{
    heightFrame=null;
    const height=root.offsetHeight;
    if(height!==lastHeight){lastHeight=height;post('size',{height});}
  });
}
// State snapshots update existing controls so focus, device rows and SVGs do not
// get recreated for a volume or battery change. Structural changes still replace
// their branch; no input state is read from the native snapshot while typing.
function syncNode(current,next){
  if(current.nodeType!==next.nodeType||current.nodeName!==next.nodeName){current.replaceWith(next);return;}
  if(current.nodeType!==1){if(current.nodeValue!==next.nodeValue)current.nodeValue=next.nodeValue;return;}
  for(const attribute of [...current.attributes])if(!next.hasAttribute(attribute.name))current.removeAttribute(attribute.name);
  for(const attribute of next.attributes)if(current.getAttribute(attribute.name)!==attribute.value)current.setAttribute(attribute.name,attribute.value);
  if(current.tagName==='INPUT'&&current.value!==next.value)current.value=next.value;
  const previous=[...current.childNodes],incoming=[...next.childNodes];
  for(let i=0;i<Math.max(previous.length,incoming.length);i++){
    if(!incoming[i])previous[i].remove();
    else if(!previous[i])current.append(incoming[i]);
    else syncNode(previous[i],incoming[i]);
  }
}
function render(){
  if(!state)return;
  const s=state;
  document.documentElement.style.setProperty('--accent',/^#[0-9a-f]{6}$/i.test(s.accent||'')?s.accent:'#0067c0');
  if(view==='wifi'&&!s.wifi.available||view==='bluetooth'&&!s.bluetooth.available)view='main';
  const focusPassword=document.activeElement?.matches?.('[data-cc-password]');
  const nextMarkup=`<div class="cc-page cc-page--${view}">${view==='wifi'?wifiPage(s):view==='bluetooth'?bluetoothPage(s):mainPage(s)}</div>`;
  if(markup===nextMarkup)return;
  markup=nextMarkup;
  const template=document.createElement('template');template.innerHTML=markup;
  const current=root.firstElementChild,next=template.content.firstElementChild;
  if(current?.classList.contains(`cc-page--${view}`))syncNode(current,next);
  else root.replaceChildren(next);
  root.hidden=false;
  if(focusPassword){const input=root.querySelector('[data-cc-password]');if(input){input.focus();input.setSelectionRange(input.value.length,input.value.length);}}
  // Layout height, not the box on screen: the open spring scales the panel, and
  // measuring mid-animation cut off the last row.
  measureHeight();
}
let renderFrame=null;
const keepRendering=()=>{
  if(dragging||typing)return;
  if(!root.firstElementChild){render();return;}
  if(renderFrame===null)renderFrame=requestAnimationFrame(()=>{renderFrame=null;if(!dragging&&!typing)render();});
};
function go(next){
  view=next;expanded='';password='';openPicker=null;
  post('set',{key:'view',value:next==='main'?'':next});
  render();
  root.querySelector('.cc-page')?.classList.add(next==='main'?'is-back':'is-forward');
}

root.addEventListener('click',event=>{
  const target=event.target.closest('button');
  if(!target){if(openPicker){openPicker=null;render();}return;}
  const d=target.dataset;
  if(d.ccToggle||d.ccPower){
    const key=d.ccToggle||d.ccPower;
    if(target.getAttribute('aria-disabled')==='true')return;
    const next=target.getAttribute('aria-checked')!=='true';
    hold(key,next,key==='hotspot'?8000:3000);post('set',{key,value:next});
    if(key==='airplane'){hold('wifi',state.wifi.on&&!next,3000);hold('bluetooth',state.bluetooth.on&&!next,3000);}
    render();
  }
  else if(d.ccView)go(d.ccView);
  else if(d.ccBack!==undefined)go('main');
  else if(d.ccNetwork!==undefined){expanded=expanded===d.ccNetwork?'':d.ccNetwork;password='';justExpanded=true;render();justExpanded=false;root.querySelector('[data-cc-password]')?.focus();}
  else if(d.ccConnect!==undefined){
    const value={ssid:d.ccConnect};const input=root.querySelector('[data-cc-password]');if(input)value.password=input.value;
    state.join={ssid:d.ccConnect,status:'started'};typing=false;post('set',{key:'connect',value});render();
  }
  else if(d.ccDisconnect!==undefined){post('set',{key:'disconnect',value:true});expanded='';render();}
  else if(d.ccScan!==undefined){post('set',{key:'scan',value:true});target.classList.add('is-spinning');}
  else if(d.ccMute!==undefined){const next=!valueOf('mute',state.audio.muted);hold('mute',next);post('set',{key:'mute',value:next});render();}
  else if(d.ccOpen){openPicker=openPicker===d.ccOpen?null:d.ccOpen;render();}
  else if(d.ccChoose){
    const key=d.ccChoose,id=d.id;openPicker=null;
    if(key==='output'){state.audio.output=id;post('set',{key:'output',value:id});}
    else{state.brightness.display=id;post('set',{key:'display',value:id});}
    render();
  }
  else if(d.ccSettings)post('settings',{page:d.ccSettings});
  else if(d.ccTray!==undefined)post('tray');
});
// Right-click a tile or slider to open its Windows settings page.
root.addEventListener('contextmenu',event=>{
  event.preventDefault();
  const tile=event.target.closest('[data-cc-toggle]'),slider=event.target.closest('[data-cc-slider]');
  const page=tile?{wifi:'wifi',bluetooth:'bluetooth',hotspot:'hotspot',airplane:'wifi'}[tile.dataset.ccToggle]:slider?(slider.dataset.ccSlider==='volume'?'sound':'display'):'';
  if(page)post('settings',{page});
});
root.addEventListener('input',event=>{
  if(event.target.matches('[data-cc-password]')){password=event.target.value;typing=true;return;}
  const range=event.target.closest('[data-cc-range]');if(!range)return;
  const key=range.dataset.ccRange,value=Number(range.value);dragging=key;
  range.style.setProperty('--p',value+'%');range.parentElement.querySelector('output').textContent=value+'%';
  hold(key,value,1500);post('set',{key,value});
  if(key==='volume'&&value>0&&state.audio.muted){hold('mute',false);}
});
root.addEventListener('focusout',event=>{if(event.target.matches?.('[data-cc-password]'))typing=false;});
root.addEventListener('keydown',event=>{
  if(event.key==='Enter'&&event.target.matches('[data-cc-password]')){event.preventDefault();root.querySelector('[data-cc-connect]')?.click();}
});
root.addEventListener('change',event=>{if(event.target.closest('[data-cc-range]')){dragging=null;keepRendering();}});
root.addEventListener('pointerup',()=>{if(dragging){dragging=null;setTimeout(keepRendering,0);}});
root.addEventListener('wheel',event=>{
  const range=event.target.closest('.cc-slider')?.querySelector('[data-cc-range]');if(!range)return;
  event.preventDefault();const key=range.dataset.ccRange;
  const value=Math.max(0,Math.min(100,Number(range.value)+(event.deltaY<0?2:-2)));
  hold(key,value,1500);post('set',{key,value});
  if(key==='volume')state.audio.volume=value;else state.brightness.level=value;render();
},{passive:false});
document.addEventListener('keydown',event=>{
  if(event.key!=='Escape')return;
  if(openPicker){openPicker=null;render();}else if(view!=='main')go('main');else post('close');
});
webview?.addEventListener('message',event=>{
  const message=event.data;if(!message||message.version!==1)return;
  if(message.type==='control-center-init'){
    setLanguage(message.english?'en':'zh');document.documentElement.lang=message.english?'en':'zh-CN';
    applyMaterial(message.material);
  }else if(message.type==='control-center-material'){applyMaterial(message.material);}
  else if(message.type==='control-center-hidden'){
    openPicker=null;dragging=null;typing=false;root.classList.remove('is-shown');
    if(view!=='main'){view='main';expanded='';password='';post('set',{key:'view',value:''});}
  }
  else if(message.type==='control-center-shown'){
    openPicker=null;render();
    measureHeight(true);
    // Restart the spring from its closed pose on every open.
    root.classList.remove('is-shown');void root.offsetWidth;root.classList.add('is-shown');
  }
  else if(message.type==='control-center-state'&&message.state&&typeof message.state==='object'){
    const joined=message.state.join?.status==='connected'&&state?.join?.status!=='connected';
    state=message.state;
    if(joined){expanded='';password='';typing=false;}
    root.querySelector('[data-cc-scan].is-spinning')?.classList.remove('is-spinning');
    keepRendering();
  }
});
// The page is transparent over Lume's glass; it turns opaque if the material is unavailable.
function applyMaterial(name){
  const material=['liquid','frosted'].includes(name)?name:'solid';
  document.body.dataset.material=material;
}
applyMaterial('liquid');
post('ready');
