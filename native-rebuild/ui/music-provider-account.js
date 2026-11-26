import {MUSIC_ICONS} from './music-icons.js';

export const MUSIC_PROVIDERS = ['qq','netease','kugou','qishui'];
const labels = {netease:'网易云音乐',qq:'QQ 音乐',kugou:'酷狗音乐',qishui:'汽水音乐'};
const badges = {netease:'云',qq:'Q',kugou:'K',qishui:'汽'};
const exact = (value, keys, optional=[]) => value && typeof value === 'object' && !Array.isArray(value) &&
  keys.every(key => Object.hasOwn(value,key)) && Object.keys(value).every(key=>keys.includes(key)||optional.includes(key));
const count = value => Number.isSafeInteger(value) && value >= 0;
const text = (value,max) => typeof value === 'string' && value.length <= max;
const busy = account => ['loading','authorizing'].includes(account.status);
const empty = provider => ({provider,request:0,status:'signed-out',phase:'',userId:'',displayName:'',qrImage:'',qrText:'',error:''});
const cleanQr = account => ({...account,qrImage:'',qrText:'',phase:''});
const membershipKinds = ['qq-green-vip','qq-super-vip','netease-vip','netease-svip','kugou-vip','qishui-vip'];
// Higher tiers include the lower ones' benefits, so one badge describes the
// account: the highest active tier, or a single dimmed badge when none is.
const membershipRank = {'qq-super-vip':2,'qq-green-vip':1,'netease-svip':2,'netease-vip':1,'kugou-vip':1,'qishui-vip':1};
export function visibleMemberships(list) {
  const items = Array.isArray(list) ? list : [];
  const active = items.filter(item=>item.active).sort((a,b)=>(membershipRank[b.kind]||0)-(membershipRank[a.kind]||0));
  if (active.length) return [active[0]];
  return items.length ? [items.slice().sort((a,b)=>(membershipRank[a.kind]||0)-(membershipRank[b.kind]||0))[0]] : [];
}
function membershipTitle(item) {
  if (!item.active) return `${item.label}\uff08\u672a\u5f00\u901a\uff09`;
  if (!item.expiresAt) return item.label;
  const date = new Date(item.expiresAt);
  return `${item.label} \u00b7 ${date.getFullYear()}-${String(date.getMonth()+1).padStart(2,'0')}-${String(date.getDate()).padStart(2,'0')} \u5230\u671f`;
}
export function musicProviderImage(value) {
  if (!text(value,2048) || /[\u0000-\u0020\\]/.test(value)) return '';
  try {
    const url = new URL(value);
    return url.protocol==='https:'&&!url.username&&!url.password&&!url.port&&[
      'y.qq.com','y.gtimg.cn','qpic.y.qq.com','q1.qlogo.cn','q2.qlogo.cn','thirdqq.qlogo.cn',
      'p1.music.126.net','p2.music.126.net','p3.music.126.net','p4.music.126.net',
      'imge.kugou.com','imgessl.kugou.com',
      'p3.douyinpic.com','p6.douyinpic.com','p9.douyinpic.com','p11.douyinpic.com','p26.douyinpic.com',
      'p1-douyin.byteimg.com','p3-douyin.byteimg.com','p6-douyin.byteimg.com','p9-douyin.byteimg.com',
    ].includes(url.hostname) ? url.href : '';
  } catch {return '';}
}

export function validMusicProviderAccountCommand(action, fields) {
  return action === 'snapshot' ? exact(fields,[]) :
    ['begin','cancel','logout','refresh'].includes(action) && exact(fields,['provider','request']) &&
    MUSIC_PROVIDERS.includes(fields.provider) && count(fields.request) && fields.request > 0;
}

export function validMusicProviderAccountMessage(message) {
  return exact(message,['version','type','revision','accounts']) && message.version === 1 &&
    message.type === 'music-provider-account-state' && count(message.revision) &&
    Array.isArray(message.accounts) && message.accounts.length === MUSIC_PROVIDERS.length &&
    new Set(message.accounts.map(account=>account?.provider)).size === MUSIC_PROVIDERS.length &&
    message.accounts.every(account => exact(account,['provider','request','status','phase','userId','displayName','qrImage','qrText','error'],['avatarUrl','memberships','loginMode']) &&
      MUSIC_PROVIDERS.includes(account.provider) && count(account.request) &&
      ['signed-out','loading','authorizing','signed-in','expired','error'].includes(account.status) &&
      ['','waiting','scanned','downloading','extracting'].includes(account.phase) &&
      (!['downloading','extracting'].includes(account.phase) ||
        account.status==='loading'&&account.qrImage===''&&account.qrText==='') &&
      text(account.userId,128) && text(account.displayName,256) &&
      (account.loginMode===undefined||account.loginMode==='qr') &&
      text(account.qrText,2048) && text(account.qrImage,350000) &&
      (account.avatarUrl===undefined||account.avatarUrl===''||!!musicProviderImage(account.avatarUrl)) &&
      (account.memberships===undefined||Array.isArray(account.memberships)&&account.memberships.length<=8&&
        account.memberships.every(item=>exact(item,['kind','label','iconUrl','level','active'],['expiresAt'])&&
          membershipKinds.includes(item.kind)&&text(item.label,128)&&
          (item.iconUrl===''||!!musicProviderImage(item.iconUrl))&&count(item.level)&&item.level<=1000&&
          typeof item.active==='boolean'&&(item.expiresAt===undefined||count(item.expiresAt)))) &&
      (account.qrImage === '' || /^data:image\/png;base64,(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(account.qrImage) &&
        account.qrImage.length > 22) &&
      (['','network','service','invalid-response','storage','expired','cancelled','unavailable','unsupported','access-denied','runtime-download',
        'qq-status-response','qq-authorization-response','qq-device-response','qq-credential-response','qq-profile-response',
        'qq-credential-network','qq-credential-malformed','qq-credential-missing-id',
        'qq-credential-missing-key','qq-credential-expired','rate-limited','not-confirmed'].includes(account.error) ||
        /^(?:qq-credential-(?:http-[0-9]{3}|code-[0-9]{1,8})|netease-code-[0-9]{1,8})$/.test(account.error) ||
        /^network-(?:dns-failed|ipv6-failed|ipv4-failed|timeout|proxy)$/.test(account.error)));
}

// This controller owns public auth state only. Native owns credentials and polling.
export function createMusicProviderAccount({send, now=Date.now} = {}) {
  let accounts = MUSIC_PROVIDERS.map(empty), revision = -1, sequence = 0;
  let selected = 'netease', modal = '', active = '', disposed = false, awaitingSnapshot = false;
  const listeners = new Set();
  const snapshot = () => structuredClone({accounts,selected,modal});
  const emit = () => {for (const listener of listeners) listener(snapshot());};
  const get = provider => accounts.find(account=>account.provider === provider);
  const replace = account => {accounts = accounts.map(value=>value.provider === account.provider ? account : value);};
  const post = (action,fields={}) => {
    try {return send?.(action,fields) === true;} catch {return false;}
  };
  function command(action,provider) {
    const request = Math.max(sequence,now(),...accounts.map(account=>account.request)) + 1;
    if (!Number.isSafeInteger(request)) return false;
    sequence = request;
    const previous = get(provider);
    // Fence replies before posting: test/native transports may reply synchronously.
    replace({...cleanQr(previous),request,
      status:action === 'cancel' ? (previous.userId ? 'signed-in' : 'signed-out') : 'loading',error:''});
    if (!post(action,{provider,request})) {
      replace({...cleanQr(previous),request,status:'error',error:'service'});
      return false;
    }
    return true;
  }
  function close() {
    modal = '';
    const provider = active;
    active = '';
    if (provider) command('cancel',provider);
    accounts = accounts.map(cleanQr);
    emit();
  }
  return {
    snapshot,
    subscribe(listener) {listeners.add(listener);return ()=>listeners.delete(listener);},
    load() {
      if(disposed)return false;
      if(revision<0){awaitingSnapshot=true;accounts=accounts.map(account=>({...account,status:'loading'}));emit();}
      const sent=post('snapshot');
      if(!sent&&awaitingSnapshot){awaitingSnapshot=false;accounts=accounts.map(account=>({...account,status:'error',error:'service'}));emit();}
      return sent;
    },
    open(provider=selected) {
      if (disposed || !MUSIC_PROVIDERS.includes(provider)) return false;
      if (modal === provider) return true;
      if (modal || active) close();
      modal = provider; selected = provider;
      if (!connected(get(provider)) && !busy(get(provider)) && !awaitingSnapshot) {
        active = provider;
        if (!command('begin',provider)) active = '';
      }
      emit();
      return true;
    },
    select(provider) {
      if (disposed || !MUSIC_PROVIDERS.includes(provider)) return false;
      if (modal && modal !== provider) close();
      selected = provider;
      emit();
      return true;
    },
    retry() {
      if (disposed || !modal || connected(get(modal)) || get(modal).status === 'loading') return false;
      if (active) command('cancel',active);
      active = modal;
      const sent = command('begin',modal);
      if (!sent) active = '';
      emit();
      return sent;
    },
    action(action,provider=selected) {
      if (disposed || !['refresh','logout'].includes(action) || !MUSIC_PROVIDERS.includes(provider) ||
        busy(get(provider))) return false;
      if (active) close();
      const sent = command(action,provider);
      emit();
      return sent;
    },
    close() {if (!disposed) close();},
    receive(message) {
      if (disposed || !validMusicProviderAccountMessage(message) || message.revision <= revision) return false;
      const resumeOpen=awaitingSnapshot;
      awaitingSnapshot=false;
      revision = message.revision;
      for (const account of message.accounts) {
        if (account.request < get(account.provider).request) continue;
        const loginSucceeded = active === modal && active === account.provider &&
          account.request === get(account.provider).request && account.status === 'signed-in';
        sequence = Math.max(sequence,account.request);
        replace(account.status==='loading' || active === account.provider && busy(account) ? {...account} : cleanQr(account));
        if (active === account.provider && !busy(account)) active = '';
        if (loginSucceeded) modal = '';
      }
      if (!modal && !get(selected).userId) selected = accounts.find(account=>account.userId)?.provider || selected;
      if(resumeOpen&&modal&&!active&&!connected(get(modal))&&get(modal).status==='signed-out') {
        active=modal;
        if(!command('begin',modal))active='';
      }
      emit();
      return true;
    },
    dispose() {
      if (disposed) return;
      close();
      disposed = true;
      accounts = MUSIC_PROVIDERS.map(empty);
      listeners.clear();
    },
  };
}

const badge = provider => `<span class="MusicProviderBadge" data-provider="${provider}" aria-hidden="true">${badges[provider]}</span>`;
const connected=account=>account?.status==='signed-in'||!!account?.userId;
// Platforms already signed in are switched to; the rest open their QR login.
const connectLabel=(provider,signedIn)=>`${signedIn?'管理':'连接'}${labels[provider]}`;
export function musicProviderButtons(accounts=[]) {
  return MUSIC_PROVIDERS.map(provider=>{
    const signedIn=connected(accounts.find(account=>account?.provider===provider));
    return `<button type="button" class="MusicProviderButton" data-music-provider-open="${provider}" data-signed-in="${signedIn}">${badge(provider)}<span data-music-provider-label="${provider}">${connectLabel(provider,signedIn)}</span></button>`;
  }).join('');
}
export function musicProviderAccountPanel() {
  return `<div class="MusicProviderAccounts">
    <button type="button" class="MusicProviderAccountSync" data-music-provider-open="">${MUSIC_ICONS.user}<span>音乐账号</span></button>
    <section class="MusicProviderAccountActive" data-music-provider-active data-provider="netease" aria-label="\u5f53\u524d\u97f3\u4e50\u8d26\u53f7" hidden>
      <div class="MusicProviderAccountIdentity">
        <span class="MusicProviderAccountAvatar">${MUSIC_ICONS.user}<img data-music-provider-avatar alt="" decoding="async" referrerpolicy="no-referrer" hidden></span>
        <div class="MusicProviderAccountCopy"><strong data-music-provider-active-name></strong><div class="MusicProviderMemberships" data-music-provider-memberships aria-label="\u4f1a\u5458\u6807\u8bc6"></div></div>
      </div>
      <button type="button" class="MusicIconButton" data-music-provider-manage title="切换音乐账号" aria-label="切换音乐账号">${MUSIC_ICONS.logout}</button>
    </section>
    <div class="MusicProviderAccountGuest" data-music-provider-guest><p data-music-provider-summary>\u6e38\u5ba2\u6a21\u5f0f</p><button type="button" data-music-action="account-home">\u5728\u9996\u9875\u767b\u5f55</button></div>
    <button type="button" class="MusicProviderAccountSync" data-music-provider-refresh hidden>${MUSIC_ICONS.reset}<span data-music-provider-sync-label>\u540c\u6b65\u6570\u636e</span></button>
    <p class="MusicProviderAccountError" data-music-provider-error role="alert" hidden></p>
  </div>`;
}

const errors = {
  'runtime-download':'音乐组件下载失败，请检查网络后点击重试',
  unavailable:'该平台的登录服务暂不可用，请稍后重试',
  unsupported:'当前运行环境不支持此登录方式',
  'access-denied':'未完成平台授权，请重新登录并确认授权',
  'qq-status-response':'QQ 扫码状态响应异常，请重新扫码',
  'qq-authorization-response':'QQ 扫码已确认，授权交换未完成，请重试',
  'qq-device-response':'QQ 设备验证未完成，请稍后重试',
  'qq-credential-response':'QQ 音乐登录凭证获取失败，请重新扫码',
  'qq-credential-network':'QQ 音乐凭证请求网络失败，请重试',
  'qq-credential-malformed':'QQ 音乐凭证响应格式异常，请重试',
  'qq-credential-missing-id':'QQ 音乐凭证缺少账号标识，请重试',
  'qq-credential-missing-key':'QQ 音乐凭证缺少密钥，请重试',
  'qq-credential-expired':'QQ 音乐返回的凭证已过期，请重试',
  'qq-profile-response':'QQ 音乐账号信息读取失败，请稍后重试',
  network:'\u7f51\u7edc\u8fde\u63a5\u5931\u8d25\uff0c\u8bf7\u91cd\u8bd5',
  service:'\u767b\u5f55\u670d\u52a1\u6682\u4e0d\u53ef\u7528\uff0c\u8bf7\u91cd\u8bd5',
  'invalid-response':'\u4e8c\u7ef4\u7801\u52a0\u8f7d\u5931\u8d25\uff0c\u8bf7\u91cd\u8bd5',
  storage:'\u8d26\u6237\u4fdd\u5b58\u5931\u8d25\uff0c\u8bf7\u91cd\u8bd5',
  expired:'\u4e8c\u7ef4\u7801\u5df2\u8fc7\u671f\uff0c\u8bf7\u5237\u65b0',
  cancelled:'\u767b\u5f55\u5df2\u53d6\u6d88',
  'not-confirmed':'\u5df2\u626b\u7801\u4f46\u672a\u5728\u624b\u673a\u4e0a\u786e\u8ba4\uff0c\u4e8c\u7ef4\u7801\u5df2\u5931\u6548\uff0c\u8bf7\u5237\u65b0\u540e\u91cd\u65b0\u626b\u7801',
};
function statusText(account) {
  if (account.status==='loading' && account.phase==='downloading') return '正在下载音乐组件…';
  if (account.status==='loading' && account.phase==='extracting') return '正在准备音乐组件…';
  if (account.error) {
    // QQ Music rejects the confirmation with 20279 once the account has no free
    // device slot (free 1, green VIP 3, super VIP 5). Point at the fix instead of
    // reporting a bare interface code.
    if (account.error === 'qq-credential-code-20279')
      return 'QQ 音乐登录设备已达上限：请打开 QQ 音乐客户端 → 设置 → 设备管理，移除不用的设备后重新扫码';
    // The rest of Folia's QQ business-code table (protocol/businessCodes.js).
    const qqCode = /^qq-credential-code-([0-9]{1,8})$/.exec(account.error)?.[1];
    if (qqCode) return {
      '20277':'QQ 音乐账号当前受限，暂时无法在新设备登录，请在 QQ 音乐 App 内查看账号状态',
      '20278':'QQ 音乐账号当前受限，暂时无法在新设备登录，请在 QQ 音乐 App 内查看账号状态',
      '20450':'QQ 音乐账号已被封禁，无法登录',
      '20272':'QQ 音乐账号需要先完成绑定：请在 QQ 音乐 App 内按提示绑定后重新扫码',
      '20274':'QQ 音乐账号需要先完成绑定：请在 QQ 音乐 App 内按提示绑定后重新扫码',
      '20261':'QQ 音乐拒绝了本次登录，请稍后重新扫码',
      '104604':'QQ 音乐登录过于频繁，请等几分钟后再刷新二维码',
    }[qqCode] || `QQ 音乐登录被拒绝（接口码 ${qqCode}）`;
    const neteaseCode = /^netease-code-([0-9]{1,8})$/.exec(account.error)?.[1];
    if (neteaseCode) return neteaseCode === '8821'
      ? '网易云音乐触发了安全验证（8821）：请稍后再刷新二维码，或先在网易云音乐 App 内正常使用一会儿再扫码'
      : `网易云音乐拒绝了本次登录（接口码 ${neteaseCode}），请稍后重试`;
    if (account.error === 'rate-limited')
      return `${labels[account.provider]}要求稍后再试：请等几分钟后再刷新二维码`;
    if (/^network-(?:dns-failed|ipv6-failed|ipv4-failed|timeout|proxy)$/.test(account.error))
      return {
        'network-dns-failed':'无法解析音乐平台服务器地址：请检查网络或把 DNS 换成 223.5.5.5 / 119.29.29.29 后重启 Lume 再扫码',
        'network-ipv6-failed':'IPv6 连接失败、IPv4 正常：请在系统网络设置里关闭 IPv6（或 netsh interface teredo set state disabled）后重启 Lume 再扫码',
        'network-ipv4-failed':'到音乐平台的连接失败：请换一个网络（例如手机热点）、关闭代理或加速器后重启 Lume 再扫码',
        'network-timeout':'连接音乐平台超时：请检查网络或关闭代理后重启 Lume 再扫码',
        'network-proxy':'检测到代理或加速器（系统代理 / TUN 模式）接管了登录请求：请让音乐平台域名直连或暂时关闭代理后重启 Lume 再扫码',
      }[account.error];
    if (/^qq-credential-http-[0-9]{3}$/.test(account.error))
      return `QQ 音乐凭证请求失败（HTTP ${account.error.slice(19)}）`;
    return errors[account.error] || errors.service;
  }
  if (account.status === 'signed-in') return '\u767b\u5f55\u6210\u529f';
  if (account.status === 'expired') return errors.expired;
  if (account.status === 'error') return errors.service;
  if (account.status === 'loading') return '\u6b63\u5728\u52a0\u8f7d\u2026';
  if (account.status === 'authorizing') return account.phase === 'scanned'
    ? '\u5df2\u626b\u7801\uff0c\u8bf7\u5728\u624b\u673a\u4e0a\u786e\u8ba4' : '\u8bf7\u626b\u63cf\u4e8c\u7ef4\u7801';
  return '\u672a\u767b\u5f55';
}
async function encodeQr(value,document) {
  const {default:encoder} = await import('./music-provider-qr/encoder.js');
  return encoder.toDataURL(document.createElement('canvas'),value,{width:320,margin:4,errorCorrectionLevel:'M'});
}

// Subscribe separately from music rendering: auth must never update the lyric clock.
export function mountMusicProviderAccountView(root, controller, {encode=encodeQr,onSelect,onSync} = {}) {
  if (!controller) return {refresh(){},setVisible(){},dispose(){}};
  const document = root.ownerDocument, window = document.defaultView;
  let dialog = null, opener = null, visible = true, disposed = false, qrKey = '', qrEpoch = 0, syncing = '';
  let latest = controller.snapshot(), qrFailed = false;
  const $ = selector => dialog.querySelector(selector);
  function renderMemberships(container, account) {
    container.replaceChildren(...visibleMemberships(account.memberships).map(item=>{
      const title=membershipTitle(item);
      const label=()=>{
        const badge=document.createElement('span');
        badge.className='MusicProviderMembershipText';badge.dataset.kind=item.kind;
        badge.textContent=item.label;badge.title=title;
        badge.classList.toggle('is-inactive',!item.active);
        return badge;
      };
      const url=musicProviderImage(item.iconUrl);
      if(!url)return label();
      const image=document.createElement('img');
      image.src=url;image.alt=item.label;image.title=title;
      image.classList.toggle('is-inactive',!item.active);
      image.referrerPolicy='no-referrer';image.onerror=()=>image.replaceWith(label());
      return image;
    }));
  }
  function removeDialog() {
    qrEpoch++; qrKey = '';
    if (!dialog) return;
    dialog.remove(); dialog = null;
    if (opener?.isConnected) opener.focus({preventScroll:true});
    else root.querySelector('[data-music-provider-open], [data-music-panel-tab="account"]')?.focus({preventScroll:true});
    opener = null;
  }
  function showDialog() {
    if (dialog) return;
    opener = document.activeElement;
    dialog = document.createElement('dialog');
    dialog.className = 'MusicProviderDialog';
    dialog.setAttribute('aria-modal','true');
    dialog.setAttribute('aria-labelledby','music-provider-title');
    dialog.innerHTML = `<button type="button" class="MusicIconButton MusicProviderClose" data-provider-close title="关闭" aria-label="关闭">${MUSIC_ICONS.close}</button>
      <div class="MusicProviderLoginLayout"><nav class="MusicProviderTabs" aria-label="音乐平台">${MUSIC_PROVIDERS.map(provider=>
        `<button type="button" data-provider-select="${provider}" title="${labels[provider]}" aria-label="${labels[provider]}">${badge(provider)}<span class="MusicProviderPlatformCopy"><strong>${labels[provider]}</strong><small data-provider-row-status="${provider}">未登录</small></span><span class="MusicProviderRowAvatar" data-provider-row-identity hidden>${MUSIC_ICONS.user}<img data-provider-row-avatar alt="" referrerpolicy="no-referrer" hidden></span></button>`).join('')}</nav>
      <section class="MusicProviderLoginContent"><h2 id="music-provider-title"><span data-provider-title-icon></span><span data-provider-title-label></span></h2>
      <div class="MusicProviderDetailLayout">
      <div class="MusicProviderQr"><img width="160" height="160" alt="登录二维码" draggable="false" hidden>
      <span data-provider-placeholder>${MUSIC_ICONS.loader}</span></div>
      <div class="MusicProviderLoginIdentity" data-provider-identity hidden><span class="MusicProviderLargeAvatar"><span class="MusicProviderAccountAvatar">${MUSIC_ICONS.user}<img data-provider-avatar alt="" decoding="async" referrerpolicy="no-referrer" hidden></span><span data-provider-avatar-badge></span></span></div>
      <div class="MusicProviderDetailCopy"><p data-provider-name></p><div class="MusicProviderMemberships" data-provider-detail-memberships aria-label="会员标识"></div><p data-provider-identity-platform></p><p data-provider-message role="status" aria-live="polite"></p>
      <div class="MusicProviderDialogActions">
      <button type="button" data-provider-retry>${MUSIC_ICONS.reset}<span data-provider-retry-label>刷新二维码</span></button>
      <button type="button" data-provider-refresh>${MUSIC_ICONS.reset}<span>同步数据</span></button>
      <button type="button" data-provider-logout>${MUSIC_ICONS.logout}<span>\u9000\u51fa\u767b\u5f55</span></button></div>
      <p class="MusicProviderNote" data-provider-note></p></div></div></section></div>`;
    document.body.append(dialog);
    dialog.addEventListener('cancel',event=>{event.preventDefault();controller.close();});
    dialog.addEventListener('close',()=>{if (latest.modal) controller.close();});
    dialog.addEventListener('click',event=>{
      const button = event.target.closest?.('button');
      if (!button || button.disabled) return;
      if (button.hasAttribute('data-provider-close')) controller.close();
      else if (button.dataset.providerSelect) {
        const provider=button.dataset.providerSelect;
        if(controller.open(provider))onSelect?.(provider);
      }
      else if (button.hasAttribute('data-provider-retry')) controller.retry();
      else if (button.hasAttribute('data-provider-refresh')) {
        const provider=latest.modal;
        syncing=provider;
        if(!controller.action('refresh',provider))syncing='';
      }
      else if (button.hasAttribute('data-provider-logout')) controller.action('logout',latest.modal);
    });
    dialog.addEventListener('keydown',event=>{
      event.stopPropagation();
      if (event.key === 'Escape') {event.preventDefault();controller.close();return;}
      if (event.key !== 'Tab') return;
      const buttons = [...dialog.querySelectorAll('button')].filter(button=>!button.closest('[hidden]') && !button.disabled);
      const index = buttons.indexOf(document.activeElement);
      if (event.shiftKey && index <= 0 || !event.shiftKey && index === buttons.length-1) {
        event.preventDefault();buttons[event.shiftKey ? buttons.length-1 : 0].focus();
      }
    });
    if (typeof dialog.showModal === 'function') dialog.showModal();
    else dialog.setAttribute('open','');
    $('[data-provider-close]').focus({preventScroll:true});
  }
  function refresh() {
    if (disposed) return;
    const active = latest.accounts.find(account=>account.provider===latest.selected) || latest.accounts[0];
    if (!active) return;
    const signedIn=active.status==='signed-in'||!!active.userId, provider=active.provider;
    const activeCard=root.querySelector('[data-music-provider-active]');
    if (activeCard) {
      activeCard.dataset.provider=provider;
      activeCard.dataset.signedIn=String(signedIn);
      activeCard.hidden=!signedIn;
      activeCard.querySelector('[data-music-provider-active-name]').textContent=active.displayName||labels[provider];
      const avatar=activeCard.querySelector('[data-music-provider-avatar]');
      const source=musicProviderImage(active.avatarUrl);
      avatar.hidden=!source;
      if(source) {if(avatar.getAttribute('src')!==source)avatar.src=source;}
      else avatar.removeAttribute('src');
      avatar.onerror=()=>{avatar.hidden=true;};
      const memberships=activeCard.querySelector('[data-music-provider-memberships]');
      renderMemberships(memberships,active);
      activeCard.querySelector('[data-music-provider-manage]').disabled=false;
    }
    for (const button of root.querySelectorAll('.MusicProviderButton[data-music-provider-open]')) {
      const target=button.dataset.musicProviderOpen;
      const signedIn=connected(latest.accounts.find(value=>value.provider===target));
      button.dataset.signedIn=String(signedIn);
      const label=button.querySelector('[data-music-provider-label]');
      if(label)label.textContent=connectLabel(target,signedIn);
    }
    const guest=root.querySelector('[data-music-provider-guest]');
    if(guest)guest.hidden=signedIn;
    const summary=root.querySelector('[data-music-provider-summary]');
    if(summary)summary.textContent=['downloading','extracting'].includes(active.phase)||active.error==='runtime-download'
      ?statusText(active):'游客模式';
    const sync=root.querySelector('[data-music-provider-refresh]');
    if(sync) {
      sync.hidden=!signedIn;sync.disabled=busy(active);
      sync.dataset.provider=provider;
      sync.querySelector('[data-music-provider-sync-label]').textContent=busy(active)?'\u6b63\u5728\u540c\u6b65...':'\u540c\u6b65\u6570\u636e';
    }
    const error=root.querySelector('[data-music-provider-error]');
    if(error) {
      error.textContent=active.error?statusText(active):'';
      error.hidden=!active.error;
    }
    if (!latest.modal || !visible || document.hidden) {removeDialog();return;}
    showDialog();
    const account = latest.accounts.find(value=>value.provider === latest.modal);
    const preparing=account.status==='loading'&&['downloading','extracting'].includes(account.phase);
    const modalSignedIn = account.status === 'signed-in', hasProfile = connected(account);
    $('[data-provider-title-icon]').innerHTML = badge(account.provider);
    $('[data-provider-title-label]').textContent = preparing?'准备音乐组件':hasProfile?`${labels[account.provider]}账号`:`登录到${labels[account.provider]}`;
    for (const button of dialog.querySelectorAll('[data-provider-select]')) {
      button.setAttribute('aria-pressed',String(button.dataset.providerSelect === account.provider));
      const row = latest.accounts.find(value=>value.provider===button.dataset.providerSelect);
      const signedIn = connected(row);
      button.dataset.signedIn = String(signedIn);
      button.querySelector('[data-provider-row-status]').textContent = signedIn
        ? row.displayName || '已登录' : ['downloading','extracting'].includes(row.phase) ? statusText(row)
          : busy(row) ? '正在登录…' : row.status==='expired' ? '登录已过期' : '未登录';
      const avatar=button.querySelector('[data-provider-row-avatar]'),source=musicProviderImage(row.avatarUrl);
      button.querySelector('[data-provider-row-identity]').hidden=!signedIn;
      avatar.hidden=!signedIn||!source;
      if(source) {if(avatar.getAttribute('src')!==source)avatar.src=source;}
      else avatar.removeAttribute('src');
      avatar.onerror=()=>{avatar.hidden=true;};
    }
    $('[data-provider-name]').textContent = hasProfile ? account.displayName || labels[account.provider] : '';
    $('[data-provider-name]').hidden = !hasProfile;
    renderMemberships($('[data-provider-detail-memberships]'),account);
    $('[data-provider-detail-memberships]').hidden = !hasProfile;
    $('[data-provider-message]').textContent = statusText(account);
    $('[data-provider-message]').hidden = modalSignedIn;
    dialog.dataset.status = account.status;
    dialog.dataset.loginMode = 'qr';
    dialog.dataset.profile = String(hasProfile);
    $('[data-provider-identity]').hidden = !hasProfile;
    $('[data-provider-identity-platform]').hidden = !hasProfile;
    $('[data-provider-identity-platform]').textContent = `${labels[account.provider]}${account.userId?` · ID: ${account.userId}`:''}`;
    $('[data-provider-avatar-badge]').innerHTML = badge(account.provider);
    const modalAvatar=$('[data-provider-avatar]'), avatarSource=musicProviderImage(account.avatarUrl);
    modalAvatar.hidden=!avatarSource;
    if(avatarSource) {if(modalAvatar.getAttribute('src')!==avatarSource)modalAvatar.src=avatarSource;}
    else modalAvatar.removeAttribute('src');
    modalAvatar.onerror=()=>{modalAvatar.hidden=true;};
    $('.MusicProviderQr').hidden = hasProfile;
    $('[data-provider-retry]').hidden = !['signed-out','expired','error'].includes(account.status) || hasProfile;
    $('[data-provider-retry]').textContent = account.error==='runtime-download'?'重试下载':'刷新二维码';
    for (const action of ['refresh','logout']) {
      $(`[data-provider-${action}]`).hidden = !hasProfile;
      $(`[data-provider-${action}]`).disabled = busy(account);
    }
    $('[data-provider-note]').textContent = preparing ? '首次使用需要下载音乐组件，完成后自动继续。' : modalSignedIn
      ? ''
      : account.provider==='qq' ? '使用手机 QQ、微信或 QQ 音乐扫码确认'
        : account.provider==='qishui' ? '使用抖音 App 扫码并确认汽水音乐授权'
          : `请使用${labels[account.provider]} App 扫码确认`;
    const qr = account.status === 'authorizing' ? account.qrImage || account.qrText : '';
    const key = `${account.provider}:${account.request}:${qr}`;
    if (qrKey === key) {
      if (qrFailed) {
        $('[data-provider-message]').textContent = errors['invalid-response'];
        $('[data-provider-retry]').hidden = false;
      }
      return;
    }
    qrKey = key;
    qrFailed = false;
    const epoch = ++qrEpoch, image = $('.MusicProviderQr img'), placeholder = $('[data-provider-placeholder]');
    image.hidden = true; image.removeAttribute('src');
    placeholder.hidden = false;
    placeholder.innerHTML = busy(account) ? MUSIC_ICONS.loader : MUSIC_ICONS.user;
    const fail = () => {
      if (disposed || !dialog || epoch !== qrEpoch) return;
      qrFailed = true;
      image.hidden = true; image.removeAttribute('src');placeholder.hidden = false;
      $('[data-provider-message]').textContent = errors['invalid-response'];
      $('[data-provider-retry]').hidden = false;
    };
    if (!qr || hasProfile) return;
    const show = source => {
      if (disposed || !dialog || epoch !== qrEpoch) return;
      image.onerror = fail;
      image.onload = () => {
        if (epoch === qrEpoch && dialog) {image.hidden = false;placeholder.hidden = true;}
      };
      image.src = source;
    };
    if (account.qrImage) show(account.qrImage);
    else Promise.resolve().then(()=>encode(account.qrText,document)).then(show,fail);
  }
  const unsubscribe = controller.subscribe(value=>{
    latest=value;refresh();
    const account=latest.accounts.find(value=>value.provider===syncing);
    if(account&&!busy(account)) {
      syncing='';
      if(account.status==='signed-in'&&account.provider===latest.selected)onSync?.(account.provider);
    }
  });
  function click(event) {
    const button = event.target.closest?.('[data-music-provider-open], [data-music-provider-refresh], [data-music-provider-manage]');
    if (!button || !root.contains(button) || button.disabled || !visible || document.hidden) return;
    if (button.hasAttribute('data-music-provider-open')||button.hasAttribute('data-music-provider-manage')) {
      const provider=button.dataset.musicProviderOpen||button.closest('[data-music-provider-active]')?.dataset.provider||latest.selected;
      if(controller.open(provider))onSelect?.(provider);
    }
    else if (button.hasAttribute('data-music-provider-refresh')) {
      const provider=button.dataset.provider||latest.selected;
      syncing=provider;
      if(!controller.action('refresh',provider))syncing='';
    }
  }
  const hide = () => controller.close();
  const visibility = () => {if (document.hidden) hide();};
  root.addEventListener('click',click);
  document.addEventListener('visibilitychange',visibility);
  window.addEventListener('pagehide',hide);
  refresh();
  return {
    refresh,
    setVisible(value) {visible = value === true;if (!visible) hide();},
    dispose() {
      if (disposed) return;
      controller.close();disposed = true;unsubscribe();removeDialog();
      root.removeEventListener('click',click);
      document.removeEventListener('visibilitychange',visibility);
      window.removeEventListener('pagehide',hide);
    },
  };
}
