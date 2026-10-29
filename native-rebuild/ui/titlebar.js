import { GEAR_ICON_PATH, MUSIC_ICON_PATH, WALLPAPER_ICON_PATH } from "./icons.js";
import {isAccountSnapshot,safeAvatarUrl,accountErrorMessage,VIP_BADGE_URL,LUME_LOGO} from './account.js';
import {syncSettingsControlBusy} from './settings-frame.js';
const icon = (path) =>
  `<svg class="svg-inline--fa" viewBox="0 0 512 512" aria-hidden="true"><path fill="currentColor" d="${path}"/></svg>`;
const labels={
  'zh-CN':{login:'登录',loginTitle:'登录 Lume',menu:'打开账户菜单',account:'Lume 账户菜单',user:'用户设置',logout:'退出登录',
    restoring:'正在恢复...',authorizing:'登录中...',cancel:'取消登录',working:'正在处理...',fallback:'Lume 用户',
    settings:'设置',music:'音乐',wallpaper:'壁纸',minimize:'最小化',maximize:'全屏',restore:'还原',close:'关闭',vip:'VIP 会员'},
  en:{login:'Sign in',loginTitle:'Sign in to Lume',menu:'Open account menu',account:'Lume account menu',user:'User settings',logout:'Sign out',
    restoring:'Restoring...',authorizing:'Signing in...',cancel:'Cancel sign-in',working:'Working...',fallback:'Lume user',
    settings:'Settings',music:'Music',wallpaper:'Wallpaper',minimize:'Minimize',maximize:'Fullscreen',restore:'Restore',close:'Close',vip:'VIP member'},
};

/**
 * update(fullscreen) 保持旧接口；setSurface('music'|'panel') 更新共享入口。
 * updateAccount(snapshot) 只接受公开确认快照；setLanguage('zh-CN'|'en')。
 * onSettings(section) 负责主页面导航；onMusic() 负责打开音乐页；onLogin/onLogout 若提供则替代默认 bridge.account 调用，
 * 应同步返回 true/false（可直接传账号控制器 login/logout），不得重复发送。取消始终经 bridge。
 * setAccountBusy(boolean) 接收组合层传入的账号忙碌状态，不读取其他组件的 DOM。
 * 不主动订阅快照；dispose() 幂等释放文档/窗口事件及 DOM。
 */
export function mountTitlebar(host, bridge, unavailable=()=>{}, callbacks={}) {
  const doc=host.ownerDocument,win=doc.defaultView;
  host.innerHTML = `<div data-lume-shared-titlebar data-visible="true"><div class="lume-titlebar__drag"></div><div class="lume-titlebar__controls">
    <div class="lume-titlebar__account"><button type="button" class="lume-titlebar__button lume-titlebar__login" aria-expanded="false">
    <span class="lume-titlebar__avatar-frame"><span data-lume-account-avatar><span data-lume-avatar-fallback>${LUME_LOGO}</span>
    <img class="lume-titlebar__avatar" alt="" referrerpolicy="no-referrer" hidden></span>
    <span class="lume-titlebar__vip-badge" hidden><img src="${VIP_BADGE_URL}" alt=""></span></span><span data-lume-account-label></span></button>
    <div class="lume-titlebar__account-menu" data-lume-account-menu role="menu" hidden><div class="lume-titlebar__account-name" data-lume-account-name></div>
    <button type="button" class="lume-titlebar__account-command" role="menuitem" data-account-menu-action="settings" tabindex="-1"></button>
    <button type="button" class="lume-titlebar__account-command" role="menuitem" data-account-menu-action="logout" tabindex="-1"></button></div></div>
    <button type="button" class="lume-titlebar__button lume-titlebar__launcher" data-titlebar-settings>${icon(GEAR_ICON_PATH)}</button>
    <button type="button" class="lume-titlebar__button lume-titlebar__launcher" data-titlebar-music>${icon(MUSIC_ICON_PATH)}</button>${[
    ["minimize", "最小化", "\uE921"],
    ["maximize", "全屏", "\uE922"],
    ["close", "关闭", "\uE8BB"],
  ]
    .map(
      ([action, label, glyph]) =>
        `<button type="button" class="lume-titlebar__button lume-titlebar__window lume-titlebar__${action}" data-window="${action}" aria-label="${label}"><span class="lume-titlebar__window-glyph" aria-hidden="true">${glyph}</span></button>`,
    )
    .join("")}</div></div>`;
  const root=host.firstElementChild,$=selector=>root.querySelector(selector);
  const login=$('.lume-titlebar__login'),menu=$('[data-lume-account-menu]'),account=$('.lume-titlebar__account');
  const menuItems=[...menu.querySelectorAll('[role="menuitem"]')],avatar=$('.lume-titlebar__avatar');
  let fullscreen=false,immersive=false,hovered=false,menuOpen=false,disposed=false,language='zh-CN',surface='panel',pending=null,localError='',failedAvatar='';
  let accountBusy=false;
  let state={revision:-1,status:'signed-out',busy:false,error:'',profile:null};
  const listeners=[];
  const listen=(target,type,fn)=>{target.addEventListener(type,fn);listeners.push(()=>target.removeEventListener(type,fn));};
  const visible = () => {
    if(disposed)return;
    // Fullscreen and music immersive mode tuck the bar away until the pointer reaches it.
    // Focus keeps it up only when it came from the keyboard: a clicked button keeps
    // focus after the click (the maximize button does, as the window goes fullscreen).
    let keyboard=false;try{keyboard=!!root.querySelector(':focus-visible');}catch{keyboard=root.contains(doc.activeElement);}
    const show = (!fullscreen && !immersive) || hovered || menuOpen || keyboard;
    root.dataset.visible = String(show);
    root.querySelectorAll('.lume-titlebar__button').forEach(b=>{b.tabIndex=show?0:-1;});
    menuItems.forEach(b=>{b.tabIndex=-1;});
  };
  function closeMenu(restoreFocus=false) {
    menuOpen=false;menu.hidden=true;login.setAttribute('aria-expanded','false');
    if(restoreFocus && !disposed)login.focus({preventScroll:true});
    visible();
  }
  function openMenu(last=false) {
    if(disposed || state.status!=='signed-in' || !state.profile || pending || state.busy || accountBusy)return;
    menuOpen=true;menu.hidden=false;login.setAttribute('aria-expanded','true');visible();
    menuItems[last?menuItems.length-1:0].focus({preventScroll:true});
  }
  function sync() {
    if(disposed)return;
    const t=labels[language],p=state.status==='signed-in'?state.profile:null;
    const authorizing=state.status==='authorizing' || pending==='login' || pending==='cancelLogin';
    const label=p?(p.displayName || t.fallback):authorizing?t.authorizing:state.status==='restoring'?t.restoring:t.login;
    const error=accountErrorMessage(localError || state.error,language);
    root.lang=language;
    $('[data-lume-account-label]').textContent=label;
    $('[data-lume-account-name]').textContent=p?.displayName || '';
    login.setAttribute('aria-label',authorizing?t.cancel:p?t.menu:t.loginTitle);
    login.title=error || (authorizing?t.cancel:label);
    if(p)login.setAttribute('aria-haspopup','menu');else login.removeAttribute('aria-haspopup');
    syncSettingsControlBusy(login,pending==='cancelLogin' || (!!pending && pending!=='login') ||
      ((state.busy || accountBusy) && !authorizing) || state.status==='restoring');
    menu.setAttribute('aria-label',t.account);
    menuItems[0].textContent=t.user;menuItems[1].textContent=t.logout;
    menuItems.forEach(button=>syncSettingsControlBusy(button,!!pending || state.busy || accountBusy));
    const frame=$('.lume-titlebar__avatar-frame'),badge=$('.lume-titlebar__vip-badge');
    frame.classList.toggle('lume-titlebar__avatar-frame--vip',!!p?.vip);badge.hidden=!p?.vip;
    if(p?.vip) {frame.setAttribute('data-lume-vip-frame','');badge.setAttribute('data-lume-vip-badge','');}
    else {frame.removeAttribute('data-lume-vip-frame');badge.removeAttribute('data-lume-vip-badge');}
    badge.title=t.vip;badge.querySelector('img').alt=t.vip;
    const url=safeAvatarUrl(p?.avatarUrl);
    if(url && url!==failedAvatar) {if(avatar.getAttribute('src')!==url)avatar.src=url;avatar.hidden=false;}
    else {avatar.removeAttribute('src');avatar.hidden=true;}
    $('[data-lume-avatar-fallback]').hidden=!avatar.hidden;
    for(const [selector,key] of [['[data-titlebar-settings]','settings'],
      ['[data-window="minimize"]','minimize'],['[data-window="maximize"]',fullscreen?'restore':'maximize'],['[data-window="close"]','close']]) {
      $(selector).title=t[key];$(selector).setAttribute('aria-label',t[key]);
    }
    const launcher=$('[data-titlebar-music]'),launcherLabel=surface==='music'?t.wallpaper:t.music;
    launcher.innerHTML=icon(surface==='music'?WALLPAPER_ICON_PATH:MUSIC_ICON_PATH);
    launcher.title=launcherLabel;launcher.setAttribute('aria-label',launcherLabel);
    $('[data-window="maximize"] span').textContent=fullscreen?'\uE923':'\uE922';
    if(!p)closeMenu(menu.contains(doc.activeElement));
    visible();
  }
  function request(action) {
    if(disposed)return false;
    if(action==='cancelLogin' ? pending==='cancelLogin' :
      pending || state.busy || accountBusy || state.status==='restoring' || state.status==='authorizing')return false;
    pending=action;localError='';closeMenu();sync();
    let sent=false;
    try {
      const callback=action==='login'?callbacks.onLogin:action==='logout'?callbacks.onLogout:null;
      sent=(callback?callback():bridge?.account?.(action,{}))===true;
    } catch { /* 仅映射短代码，避免把宿主异常内容带入界面。 */ }
    if(!sent) {pending=null;localError='unavailable';sync();}
    return sent;
  }
  listen(host,"pointerenter",()=>{hovered=true;visible();});
  listen(host,"pointerleave",()=>{hovered=false;visible();});
  listen(root,'focusin',visible);
  listen(root,'focusout',event=>{
    // 鼠标切换菜单项时 activeElement 会暂时指向 body，不能因此提前关闭菜单。
    if(account.contains(event.relatedTarget))return;
    queueMicrotask(()=>{
      if(disposed)return;
      if(menuOpen && !account.contains(doc.activeElement))closeMenu();
      visible();
    });
  });
  listen(root,"click", (event) => {
    const button = event.target.closest("button");
    if (!button || !root.contains(button) || button.closest('[hidden]') || button.disabled || button.getAttribute('aria-disabled')==='true') return;
    if(button===login) {
      if(state.status==='signed-in' && state.profile) {if(menuOpen)closeMenu(true);else openMenu();}
      else request(state.status==='authorizing' || pending==='login'?'cancelLogin':'login');
    } else if(button.dataset.accountMenuAction) {
      closeMenu(true);
      if(button.dataset.accountMenuAction==='settings')callbacks.onSettings?.('user');
      else request('logout');
    } else if(button.hasAttribute('data-titlebar-settings')) {
      closeMenu();if(callbacks.onSettings)callbacks.onSettings('general');else unavailable(labels[language].settings);
    } else if(button.hasAttribute('data-titlebar-music')) { closeMenu(); if(callbacks.onMusic) callbacks.onMusic(); else unavailable(labels[language].music); }
    else if(button.dataset.window) {closeMenu();bridge?.send?.("window", button.dataset.window);}
  });
  listen($('.lume-titlebar__drag'),"pointerdown",(event) => {
      if (event.button === 0 && !fullscreen) {
        event.preventDefault();
        bridge?.send?.("window", "drag");
      }
  });
  listen(root,'keydown',event=>{
    const target=event.target;
    if(target===login && ['ArrowDown','ArrowUp','Enter',' '].includes(event.key)) {
      event.preventDefault();
      if(event.key==='ArrowDown' || event.key==='ArrowUp')openMenu(event.key==='ArrowUp');
      else login.click();
      return;
    }
    if(!menuOpen || !menu.contains(target))return;
    if(event.key==='Tab') {closeMenu(true);return;}
    if(['ArrowDown','ArrowUp','Home','End'].includes(event.key)) {
      event.preventDefault();
      const index=menuItems.indexOf(doc.activeElement);
      const next=event.key==='Home'?0:event.key==='End'?menuItems.length-1:
        (index+(event.key==='ArrowDown'?1:-1)+menuItems.length)%menuItems.length;
      menuItems[next].focus({preventScroll:true});
    } else if(event.key==='Enter' || event.key===' ') {event.preventDefault();target.click();}
  });
  listen(doc,'keydown',event=>{
    if(event.key==='Escape' && menuOpen) {event.preventDefault();closeMenu(true);}
    else if(event.key==='F6' && fullscreen && !root.contains(doc.activeElement)) {
      const target=[...root.querySelectorAll('.lume-titlebar__button')].find(button=>!button.disabled);
      if(target){event.preventDefault();target.focus({preventScroll:true});visible();}
    }
  });
  listen(doc,'pointerdown',event=>{
    if(menuOpen && !account.contains(event.target))closeMenu(menu.contains(doc.activeElement));
  });
  listen(doc,'mousemove',event=>{
    if(!fullscreen)return;
    const rect=root.getBoundingClientRect();
    hovered=event.clientX>=rect.left && event.clientX<=rect.right && event.clientY>=rect.top && event.clientY<=rect.bottom;
    visible();
  });
  listen(win,'blur',()=>{hovered=false;closeMenu();});
  listen(avatar,'error',()=>{failedAvatar=avatar.getAttribute('src') || '';sync();});
  sync();
  return {
    update(expanded) {
      if(disposed)return;
      fullscreen=!!expanded;sync();
    },
    setSurface(value) {if(disposed)return;surface=value==='music'?'music':'panel';sync();},
    setImmersive(value) {if(disposed)return;immersive=!!value;visible();},
    updateAccount(message) {
      if(disposed || !isAccountSnapshot(message))return false;
      if(message.revision<=state.revision)return true;
      const previous=state.profile;
      // 原生错误快照仍携带已确认资料；错误或忙碌不等于退出登录。
      const profile=message.status==='signed-in'?{...message.profile}:null;
      if(previous?.id!==profile?.id)closeMenu(menu.contains(doc.activeElement));
      state={...message,profile};
      if(previous?.avatarUrl!==profile?.avatarUrl)failedAvatar='';
      if(!message.busy)pending=null;
      localError='';sync();return true;
    },
    setLanguage(value) {if(disposed)return;language=value==='en'?'en':'zh-CN';sync();},
    setAccountBusy(value) {if(disposed)return;accountBusy=!!value;sync();},
    dispose() {
      if(disposed)return;
      state={revision:state.revision,status:'signed-out',profile:null,busy:false,error:''};
      pending=null;localError='';closeMenu();sync();disposed=true;
      listeners.forEach(remove=>remove());root.remove();
    },
  };
}
