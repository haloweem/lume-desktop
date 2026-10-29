import {USER_ICON_PATH} from './icons.js';
import {syncSettingsControlBusy} from './settings-frame.js';
import {validAccountState,validAccountName} from './settings-account-protocol.js';

export {validAccountState as isAccountSnapshot};

// 原版 shared/lume-titlebar/assets/vip-badge.webp 的 48px 副本；不新增远程资源或 CSP。
export const VIP_BADGE_URL='data:image/webp;base64,UklGRqYFAABXRUJQVlA4WAoAAAAQAAAALwAALwAAQUxQSFMBAAABkEPbtqk9+7fNylZnu3Nsp3Iq27Zt27Ztm52Nd27wvXfPvVEbEROA/3insLTyhu279R8zbcHy9du2rF0yd9KwXp1aVecn+tkxeAy4+oVIEPebkx0cJfzOkPKtLpYcdpGG0y0NIC0bW8j/qseraBOf+6TpcSdbi0jbQTZqSV4IIXi+ZAAIfC4nTBnoiguwgKSFRQbqjwxDhWB4H7GKlAg5mvhUt5PnGUjNpk5CswrH0wykYg0Q84KB+G75Aaj8poBk36Ti1z4MxGTUhU27VQzEMwimHue5SHKdvRnCn8txnveE1ZyP6p6GwXoLQ9X7LMgOVWQ0hrT9KjUDwOh+QsVKew6EPuQ76Q7elBdc90PAXfKJ50UK+JsbHJ9KoLIPg9EMaqfLdYdih7UyE6Hcda+1xfbq4HXMynon6Oh7zGy1M/T0WGDjy3AHaJsz8+ihMYn4NwYAVlA4ICwEAADQFQCdASowADAAPjEYiUMiIaEVXVSsIAMEtgBOmWEhZkCVN+l/fzkTyrQmvE66UXmA86D0Z+gB/Veo59ADy0v2d+EX9yK6r9M3rH4XfHXtflEIzqzFqrpLdxf/qvTHvBjDebH6G9gX+Wf0//Sf2H90viq9fn7gex9+qK7QOtq1DK22h3Z95jKAIuvwPRFsPL/1oGGnonaIfpZjkUFs+LaE0nay81FeOe/t3/Dl9HF9yGITvpnymAAA7UJl/06VJD4r+72uQYafwMZewiS3+sQk5oLO6udPJLFt4HUwn/7u3WgxJf7hIRslSAYUIJtr6dNPhIPsG0SaMkG0QRFt56+1JXh/P/Vo65bqj7iRd7bE1F7pajVY0PxI4muH6jjDXBCOgqvGPLlR5l3UkHP/uB/8PdF4dLqJGvmU9qKGWCf8BNtfjtu00enHvM7PYYOW+UbqVPl+r6fCccisEGEJcnxIWe2tXE7Cvrs7XbHyve6fcqxycztP2DgP0mJ/Uqmev/5aBzKE9oHec2nfhCCFoBInduWbcQ8IvkYBiecR7Ob86Y+tISsT/rBN5MN3Su+9Svc6Jo7gyQEd9KBIU6Ip9usaGnrKb/N3fn9oPGI/zzUFQICjLCZWJIB8eqft9ynSU7qcISv+fFBizavVYVTRdseHP9QaaN/W27vCRKojSeHD8nbRLoy1VgkaPhbx/olW3XcvZ9Nwq7jeq2uWzE6pAcaSciYJxux2NI08KroFpiOo4RtYe31U0Te6MC6P3zDwWydnVCrSz49qSCv8dQ7cxwgqnSpza2442Xg31xuG7cIHCHdNJR+1bM+KfKvp7cgfZvvguklX6q2xQq3Mx0s7g7Gbwa/D7xrSU9X6eatpzNSvtkXCuaUd9ER/Q9FO4jzgR9bPTNaKnrfOj1V9hf/HJ33chqb0k4J0NN38BQfEB/QMC8IJlcgwbNoT6QKLzZCd4P/53sgHf39d+SobmWUM8zSm0PadvIhCDWbXn7iwG71YeD/7mJLlrpc9qzEUrsj4sn4Kne/c8gzXmMqo8jZgq0lO3hb8sDuJZ/0DlOWWk5CBbzRZ1m4Si+G67KgS9+MdEnHFTHj7sS5Mpj+EUeDdEkQ2ecAFsFMpMIE+GyVcpgybnYRIsHLPOjt//o2ab9DpKZPn/n8+W+cmLkjQBzOQN/dbNLraLq+IYtyXAYR/lqCuGoYcsc2jJ3mH/jsKLV4dQQF90Mad9ek5xClk7Q7N8rfLuZqvLnjdD/8o08u3+Pg54FWzJA0u80cyBODKiLPULMPPM+Z0lYS252RegtwMqbEfOheizLbfKz5J8pVijxAszBQCDXMnG8sNFN+JloeXFP5i4CNudZ/DXS5T+g5YgJzIfD7xTpdVF7aZ5c7D/0yO/T/9vO53Df09ULT/HlXX1JDuqwgyugh0CVMAAAA=';
export const LUME_LOGO='<svg viewBox="0 0 512 512" aria-hidden="true"><path fill="currentColor" fill-rule="evenodd" d="M100.5 166.545V346.545L143.338 371.259L143.339 284.45L234.561 337.078L234.56 423.887L256.5 436.545L278.439 423.887V261.16L234.561 235.776V286.45L143.339 233.822V190.776L100.5 166.545ZM256.5 76.5449L149.175 138.788L303.843 227.105V360.854L303.559 361.021L303.843 361.513L303.842 409.232L412.5 346.545V298.169L347.721 335.54V201.16L235.347 136.96L255.955 125.071L370.815 191.336V298.083L412.5 275.006V166.545L256.5 76.5449Z"/></svg>';

// 沿用原 UserSettings 的 Font Awesome 路径，不增加运行时图标依赖。
const actionPaths={
  chooseAvatar:'M149.1 64.8L138.7 96 64 96C28.7 96 0 124.7 0 160L0 416c0 35.3 28.7 64 64 64l384 0c35.3 0 64-28.7 64-64l0-256c0-35.3-28.7-64-64-64l-74.7 0-10.4-31.2C356.4 45.2 338.1 32 317.4 32L194.6 32c-20.7 0-39 13.2-45.5 32.8zM256 192a96 96 0 1 1 0 192 96 96 0 1 1 0-192z',
  refresh:'M480.1 192l7.9 0c13.3 0 24-10.7 24-24l0-144c0-9.7-5.8-18.5-14.8-22.2S477.9 .2 471 7L419.3 58.8C375 22.1 318 0 256 0 127 0 20.3 95.4 2.6 219.5 .1 237 12.2 253.2 29.7 255.7s33.7-9.7 36.2-27.1C79.2 135.5 159.3 64 256 64 300.4 64 341.2 79 373.7 104.3L327 151c-6.9 6.9-8.9 17.2-5.2 26.2S334.3 192 344 192l136.1 0zm29.4 100.5c2.5-17.5-9.7-33.7-27.1-36.2s-33.7 9.7-36.2 27.1c-13.3 93-93.4 164.5-190.1 164.5-44.4 0-85.2-15-117.7-40.3L185 361c6.9-6.9 8.9-17.2 5.2-26.2S177.7 320 168 320L24 320c-13.3 0-24 10.7-24 24L0 488c0 9.7 5.8 18.5 14.8 22.2S34.1 511.8 41 505l51.8-51.8C137 489.9 194 512 256 512 385 512 491.7 416.6 509.4 292.5z',
  logout:'M505 273c9.4-9.4 9.4-24.6 0-33.9L361 95c-6.9-6.9-17.2-8.9-26.2-5.2S320 102.3 320 112l0 80-112 0c-26.5 0-48 21.5-48 48l0 32c0 26.5 21.5 48 48 48l112 0 0 80c0 9.7 5.8 18.5 14.8 22.2s19.3 1.7 26.2-5.2L505 273zM160 96c17.7 0 32-14.3 32-32s-14.3-32-32-32L96 32C43 32 0 75 0 128L0 384c0 53 43 96 96 96l64 0c17.7 0 32-14.3 32-32s-14.3-32-32-32l-64 0c-17.7 0-32-14.3-32-32l0-256c0-17.7 14.3-32 32-32l64 0z',
};
const actionIcon=path=>`<svg viewBox="0 0 512 512" aria-hidden="true"><path fill="currentColor" d="${path}"/></svg>`;
export const REFRESH_ICON=actionIcon(actionPaths.refresh);

const text={
  'zh-CN':{
    login:'登录 Lume',cancelLogin:'取消登录',logout:'退出登录',refresh:'刷新资料',updateName:'保存更改',chooseAvatar:'更换头像',snapshot:'重试',
    signedOut:'登录后可管理 Lume 用户资料。',restoring:'正在恢复登录状态...',authorizing:'请在浏览器中完成登录...',
    signedIn:'已登录',profileTitle:'个人资料',name:'昵称',avatar:'用户头像',sessionTitle:'账户会话',
    sessionDescription:'退出登录会移除此设备上保存的登录状态。',vip:'VIP 会员',unknownExpiry:'VIP 有效期未知',
    expires:'有效期至',remaining:'剩余',expired:'VIP 已过期',saving:'正在处理...',saved:'资料已保存',fallbackName:'Lume 用户',
  },
  en:{
    login:'Sign in to Lume',cancelLogin:'Cancel sign-in',logout:'Sign out',refresh:'Refresh profile',updateName:'Save changes',chooseAvatar:'Change avatar',snapshot:'Retry',
    signedOut:'Sign in to manage your Lume profile.',restoring:'Restoring your session...',authorizing:'Complete sign-in in your browser...',
    signedIn:'Signed in',profileTitle:'Profile',name:'Nickname',avatar:'User avatar',sessionTitle:'Account session',
    sessionDescription:'Signing out removes the saved session on this device.',vip:'VIP member',unknownExpiry:'VIP expiry is unknown',
    expires:'Valid until',remaining:'Remaining',expired:'VIP expired',saving:'Working...',saved:'Profile saved',fallbackName:'Lume user',
  },
};
// 原生 error 字段也携带本地退出成功的状态码，不代表服务端撤销。
const statusMessages={
  'local-logout':['已退出此设备的登录。','Signed out on this device.'],
};
const errors={
  network:['网络连接失败，请检查网络后重试。','Network connection failed. Please try again.'],
  storage:['登录状态保存失败，请重试。','Could not save the session. Please try again.'],
  expired:['登录已失效，请重新登录。','Your session expired. Please sign in again.'],
  cancelled:['登录已取消。','Sign-in cancelled.'],
  'invalid-name':['昵称需要 2-32 个 Unicode 字符，且不能包含控制字符。','Use 2-32 Unicode characters without control characters.'],
  'avatar-type':['头像仅支持 JPG、PNG 或 WebP。','Choose a JPG, PNG or WebP image.'],
  'avatar-too-large':['头像不能超过 2 MiB。','The avatar must not exceed 2 MiB.'],
  'avatar-upload':['头像上传失败，请重试。','Avatar upload failed. Please try again.'],
  browser:['无法打开登录浏览器，请重试。','Could not open the sign-in browser.'],
  'protocol-registration-failed':['登录回调注册失败，请重试。','Sign-in callback registration failed. Please try again.'],
  timeout:['登录超时，请重新登录。','Sign-in timed out. Please try again.'],
  unavailable:['宿主未连接，账户服务暂时不可用。','The host is disconnected. Account service is unavailable.'],
  busy:['账户操作尚未完成，请稍候。','An account operation is still in progress.'],
  service:['账户服务暂时不可用，请稍后重试。','The account service is unavailable. Please try again.'],
  verification:['登录验证失败，请重新登录。','Sign-in verification failed. Please sign in again.'],
  denied:['登录授权被拒绝，请重新登录。','Sign-in authorization was denied. Please try again.'],
  corrupt:['已保存的登录状态损坏，请重新登录。','The saved session is damaged. Please sign in again.'],
  logout:['退出登录未完全完成，请重试。','Sign-out did not finish completely. Please try again.'],
  unknown:['账户操作失败，请稍后重试。','The account operation failed. Please try again.'],
};
const aliases={
  'auth-required':'expired','session-expired':'expired','invalid-session':'expired','token-expired':'expired',
  'invalid-display-name':'invalid-name','invalid-avatar-type':'avatar-type','avatar-upload-failed':'avatar-upload',
  'storage-failed':'storage','save':'storage','credential-save':'storage','credential-store':'storage','dpapi':'storage',
  'network-error':'network','network-failed':'network','login-cancelled':'cancelled','login-timeout':'timeout',
  'browser-open':'browser','browser-failed':'browser',
  'network-unavailable':'network','service-unavailable':'service','invalid-response':'service',
  'credential-corrupt':'corrupt','device-inactive':'expired','invalid-avatar':'avatar-type',
  'callback-expired':'timeout','callback-invalid':'verification','callback-inactive':'verification',
  'callback-state':'verification','invalid-token':'verification','authorization-denied':'denied',
};
export function accountErrorMessage(code,language='zh-CN') {
  if(!code) return '';
  const key=String(code).toLowerCase().replaceAll('_','-');
  return (statusMessages[key] || errors[aliases[key] || key] || errors.unknown)[language==='en'?1:0];
}
export function normalizeDisplayName(value) {
  // 先查原始控制字符，避免 trim 把换行隐藏；长度和 Unicode 验证复用协议。
  if(typeof value!=='string' || /[\p{Cc}\u061c\u200e\u200f\u2028-\u202e\u2066-\u2069]/u.test(value)) return null;
  const name=value.trim();
  return validAccountName(name)?name:null;
}
export function safeAvatarUrl(value) {
  if(typeof value!=='string' || value.length>2796240) return '';
  const match=/^data:image\/(?:png|jpeg|webp);base64,((?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?)$/.exec(value);
  if(!match || !match[1]) return '';
  const bytes=match[1].length/4*3-(match[1].endsWith('==')?2:match[1].endsWith('=')?1:0);
  return bytes<=2*1024*1024 ? value : '';
}
/**
 * 独立账号子控制器。只由 handleMessage 接收公开快照；不订阅 bridge，不读写全局页面状态。
 * bridge.account(action, fields) 同步返回 true 表示已发送，false/抛错表示未发送。
 * render() 复用节点；setLanguage() 只改显示；requestSnapshot()/login()/cancelLogin()/logout()/refresh()
 * 返回是否发送；dispose() 幂等、清除资料与事件。昵称草稿仅归当前 controller/账号所有。
 * onBusyChange(boolean) 仅在忙碌状态变化时通知组合层，不暴露资料、DOM 或 bridge。
 */
export function mountAccount(host,{bridge,language='zh-CN',onBusyChange=()=>{}}={}) {
  const doc=host.ownerDocument,root=doc.createElement('div');
  root.className='application-account';
  root.innerHTML=`<div class="application-account__empty" data-account-empty>
    <svg viewBox="0 0 512 512" aria-hidden="true"><path fill="currentColor" d="${USER_ICON_PATH}"/></svg>
    <span data-account-status role="status"></span>
    <div class="application-account__actions"><button type="button" data-account-action="login" class="primary"></button>
    <button type="button" data-account-action="cancelLogin"></button><button type="button" data-account-action="snapshot" hidden></button></div></div>
    <div data-account-profile hidden><div class="application-account__header">
    <div class="application-account__avatar-frame"><span data-account-avatar-fallback>${LUME_LOGO}</span><img data-account-avatar hidden referrerpolicy="no-referrer" alt="">
    <span class="application-account__avatar-badge" data-account-avatar-badge hidden><img src="${VIP_BADGE_URL}" alt=""></span></div>
    <div class="application-account__summary"><div class="application-account__name" data-account-profile-name></div>
    <div class="application-account__vip" data-account-vip hidden><span class="application-account__vip-label" data-account-text="vip"></span>
    <span data-account-expiry></span><span data-account-remaining></span></div>
    <div class="application-account__signed-in" data-account-text="signedIn"></div></div></div>
    <section class="application-account__section"><h3 data-account-text="profileTitle"></h3>
    <div class="application-account__actions"><button type="button" data-account-action="chooseAvatar"></button></div>
    <label class="application-account__field"><span data-account-text="name"></span><input data-account-name-input type="text" maxlength="64" autocomplete="off" spellcheck="false"></label>
    <div class="application-account__actions"><button type="button" class="primary" data-account-action="updateName"></button>
    <button type="button" data-account-action="refresh"></button></div></section>
    <section class="application-account__section"><h3 data-account-text="sessionTitle"></h3><p data-account-text="sessionDescription"></p>
    <div class="application-account__actions"><button type="button" class="danger" data-account-action="logout"></button></div></section></div>
    <p class="application-account__message" data-account-message role="status" aria-live="polite"></p>`;
  root.querySelectorAll('[data-account-action]').forEach(button=>{
    const path=actionPaths[button.dataset.accountAction];
    button.innerHTML=(path?actionIcon(path):'')+'<span data-account-action-label></span>';
  });
  host.replaceChildren(root);
  const $=selector=>root.querySelector(selector),field=$('[data-account-name-input]'),avatar=$('[data-account-avatar]');
  let state={revision:-1,status:'restoring',busy:false,error:'',profile:null};
  let pending=null,draft='',dirty=false,localError='',saved=false,disposed=false,failedAvatar='';
  let lastBusy=null;
  language=language==='en'?'en':'zh-CN';
  const busy=()=>state.busy || !!pending || state.status==='restoring';
  function render() {
    if(disposed) return null;
    const t=text[language],p=state.status==='signed-in'?state.profile:null;
    const authorizing=state.status==='authorizing' || pending?.action==='login' || pending?.action==='cancelLogin';
    root.lang=language;
    $('[data-account-empty]').hidden=!!p;
    $('[data-account-profile]').hidden=!p;
    root.querySelectorAll('[data-account-text]').forEach(e=>{e.textContent=t[e.dataset.accountText];});
    root.querySelectorAll('[data-account-action]').forEach(e=>{
      const action=e.dataset.accountAction;
      e.querySelector('[data-account-action-label]').textContent=t[action];e.title=t[action];
      const blocked=action==='snapshot' ? state.busy || !!pending :
        action==='cancelLogin' ? pending?.action==='cancelLogin' : busy() || authorizing;
      syncSettingsControlBusy(e,!!blocked);
    });
    $('[data-account-action="login"]').hidden=authorizing || state.status==='restoring';
    $('[data-account-action="cancelLogin"]').hidden=!authorizing;
    $('[data-account-action="snapshot"]').hidden=!(localError && state.status==='restoring');
    $('[data-account-status]').textContent=localError?'':authorizing?t.authorizing:state.status==='restoring'?t.restoring:t.signedOut;
    $('[data-account-profile-name]').textContent=p?.displayName || (p?t.fallbackName:'');
    $('[data-account-profile-name]').title=p?.displayName || '';
    $('.application-account__avatar-frame').classList.toggle('vip',!!p?.vip);
    $('[data-account-vip]').hidden=!p?.vip;
    $('[data-account-avatar-badge]').hidden=!p?.vip;
    $('[data-account-avatar-badge] img').alt=t.vip;
    const url=safeAvatarUrl(p?.avatarUrl);
    if(url && url!==failedAvatar) {
      if(avatar.getAttribute('src')!==url) avatar.src=url;
      avatar.hidden=false;
    } else {avatar.removeAttribute('src');avatar.hidden=true;}
    avatar.alt=t.avatar;$('[data-account-avatar-fallback]').hidden=!avatar.hidden;
    const expiry=p?.vipExpiresAt?new Date(p.vipExpiresAt):null;
    const validExpiry=expiry && Number.isFinite(expiry.getTime());
    $('[data-account-expiry]').textContent=p?.vip?(validExpiry?
      `${t.expires}: ${expiry.toLocaleString(language,{year:'numeric',month:'2-digit',day:'2-digit',hour:'2-digit',minute:'2-digit',hour12:false})}`:t.unknownExpiry):'';
    const minutes=validExpiry?Math.floor((expiry.getTime()-Date.now())/60000):0;
    $('[data-account-remaining]').textContent=p?.vip && validExpiry?(minutes<=0?t.expired:
      `${t.remaining}: ${language==='en'?`${Math.floor(minutes/1440)}d ${Math.floor(minutes%1440/60)}h`:`${Math.floor(minutes/1440)} 天 ${Math.floor(minutes%1440/60)} 小时`}`):'';
    if(field.value!==draft) field.value=draft;
    field.setAttribute('aria-label',t.name);
    syncSettingsControlBusy(field,busy() || !p);
    const error=localError || state.error,message=$('[data-account-message]');
    const isError=!!error && !Object.hasOwn(statusMessages,error);
    message.textContent=error?accountErrorMessage(error,language):pending?t.saving:saved?t.saved:'';
    message.setAttribute('role',isError?'alert':'status');
    message.classList.toggle('error',isError);message.hidden=!message.textContent;
    const nextBusy=busy() || authorizing;
    if(nextBusy!==lastBusy) {lastBusy=nextBusy;onBusyChange(nextBusy);}
    return root;
  }
  function request(action,fields={}) {
    if(disposed) return false;
    const cancelling=action==='cancelLogin',snapshot=action==='snapshot';
    const authorizing=state.status==='authorizing' || pending?.action==='login';
    if(!snapshot && (cancelling ? !authorizing || pending?.action==='cancelLogin' : busy() || authorizing)) return false;
    if(['updateName','chooseAvatar','refresh','logout'].includes(action) && !state.profile) return false;
    if(!snapshot) {pending={action,fields,revision:state.revision};localError='';saved=false;render();}
    let sent=false;
    try {sent=bridge?.account?.(action,fields)===true;} catch { /* 统一展示受控错误，不显示异常文本。 */ }
    if(!sent) {
      if(!snapshot) pending=null;
      localError='unavailable';render();
    }
    return sent;
  }
  function handleMessage(message) {
    if(disposed || !validAccountState(message)) return false;
    if(message.revision<=state.revision) return true;
    const previous=state.profile,operation=pending;
    // 原生错误快照仍携带已确认资料；错误或忙碌不等于退出登录。
    const nextProfile=message.status==='signed-in'?{...message.profile}:null;
    state={...message,profile:nextProfile};
    if(!message.busy && operation && message.revision>operation.revision) pending=null;
    if(!nextProfile) {
      draft='';dirty=false;saved=false;failedAvatar='';
    } else if(previous?.id!==nextProfile.id) {
      draft=nextProfile.displayName;dirty=false;saved=false;failedAvatar='';
    } else {
      if(previous.avatarUrl!==nextProfile.avatarUrl) failedAvatar='';
      const confirmed=operation?.action==='updateName' && !message.busy && !message.error &&
        nextProfile.displayName===operation.fields.value;
      if(confirmed) {
        if(draft.trim()===operation.fields.value) {draft=nextProfile.displayName;dirty=false;}
        saved=true;
      } else if(!dirty) draft=nextProfile.displayName;
    }
    localError='';render();return true;
  }
  const onInput=event=>{
    if(event.target!==field || busy()) return;
    draft=field.value;dirty=true;localError='';saved=false;
    const message=$('[data-account-message]');message.textContent='';message.hidden=true;
  };
  const onClick=event=>{
    const button=event.target.closest?.('[data-account-action]');
    if(!button || !root.contains(button) || button.closest('[hidden]') ||
      button.disabled || button.getAttribute('aria-disabled')==='true') return;
    const action=button.dataset.accountAction;
    if(action==='updateName') {
      const value=normalizeDisplayName(draft);
      if(value===null) {localError='invalid-name';saved=false;render();return;}
      request(action,{value});
    } else request(action);
  };
  const onAvatarError=()=>{failedAvatar=avatar.getAttribute('src') || '';render();};
  root.addEventListener('input',onInput);root.addEventListener('click',onClick);
  avatar.addEventListener('error',onAvatarError);
  render();
  return {
    render,handleMessage,
    setLanguage(value) {if(disposed)return;language=value==='en'?'en':'zh-CN';render();},
    requestSnapshot:()=>request('snapshot'),
    login:()=>request('login'),cancelLogin:()=>request('cancelLogin'),
    logout:()=>request('logout'),refresh:()=>request('refresh'),
    dispose() {
      if(disposed) return;
      state={revision:state.revision,status:'signed-out',busy:false,error:'',profile:null};
      pending=null;draft='';dirty=false;localError='';saved=false;render();
      disposed=true;root.removeEventListener('input',onInput);root.removeEventListener('click',onClick);
      avatar.removeEventListener('error',onAvatarError);root.remove();
    },
  };
}
