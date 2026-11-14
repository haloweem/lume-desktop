// Presentation only: confirmed selection and menu visibility remain owned by
// the existing controllers. No commands, polling or persistent animation loop.
const groups=[
  {selector:'.HomeModuleTabs',items:'.module-tab',selected:'.active',name:'main-navigation',paint:'.HomeModuleHighlight',duration:240},
  {selector:'.settings-side-nav__links',items:'button[data-tab]',selected:'[aria-selected="true"]',name:'component-navigation',duration:230},
  {selector:'.application-settings__nav',items:'button[data-settings-section]',selected:'.active',name:'application-navigation',duration:230},
  {selector:'.island-appearance__segments',items:'button',selected:'[aria-pressed="true"]',name:'island-segment',duration:210},
  {selector:'.island-home__preview-stage-modes',items:'button',selected:'[aria-pressed="true"]',name:'island-preview',duration:210},
  {selector:'.MusicSidebar',items:'.MusicNavItem',selected:'.is-active',name:'music-navigation',duration:230},
  {selector:'.MusicPanelTabs',items:'button',selected:'[aria-pressed="true"]',name:'music-panel',duration:200},
  {selector:'.MusicRailTabs',items:'button',selected:'[aria-selected="true"]',name:'music-rail',duration:200},
  {selector:'.MusicPlaylistTabs',items:'button[data-music-playlist-group]',selected:'[aria-pressed="true"]',name:'music-playlist',line:true,duration:190},
];
const groupSelector=groups.map(group=>group.selector).join(',');
const menuSelector='.setting-select-options,.wallpaper-select-menu,.lume-titlebar__account-menu,'+
  '.wallpaper-detail-target-menu,.MusicVisualizerMenu,.MusicBackgroundSourceMenu,.MusicAudioQualityMenu,.MusicVolumePopover';
const selectionKeys=['view','tab','settingsSection','value','islandShape','islandScreen','islandExpansion','islandPreviewMode',
  'musicNav','musicPanelTab','musicRailTab','musicPlaylistGroup'];
const easing='cubic-bezier(.2,.8,.2,1)';
const identity=(item,index)=>selectionKeys.map(key=>item.dataset[key]).find(value=>value!==undefined)??(item.id||String(index));
const visible=element=>element.isConnected&&element.getBoundingClientRect().width>0&&element.getBoundingClientRect().height>0;
function relativeBox(element,group,line=false){
  const box=element.getBoundingClientRect(),parent=group.getBoundingClientRect();
  const sx=parent.width/group.offsetWidth||1,sy=parent.height/group.offsetHeight||1;
  const x=(box.left-parent.left)/sx+group.scrollLeft-group.clientLeft;
  const y=(box.top-parent.top)/sy+group.scrollTop-group.clientTop;
  return {x,y:line?y+box.height/sy-1:y,width:box.width/sx,height:line?1:box.height/sy};
}
const frame=box=>({transform:`translate(${box.x}px,${box.y}px)`,width:`${box.width}px`,height:`${box.height}px`});
const distance=(a,b)=>Math.abs(a.x-b.x)+Math.abs(a.y-b.y)+Math.abs(a.width-b.width)+Math.abs(a.height-b.height);

export function mountInteractionMotion(root){
  const document=root.ownerDocument,window=document.defaultView;
  const reduced=window.matchMedia('(prefers-reduced-motion: reduce)');
  let disposed=false,queued=false,initial=true;
  const states=new Map(),menus=new Map();
  const canAnimate=()=>!reduced.matches&&typeof root.animate==='function'&&!document.hidden;
  function cancel(state){state.animation?.cancel();state.animation=null;}
  function animate(state,element,keyframes,options,finished=()=>{}){
    const animation=element.animate(keyframes,options);state.animation=animation;
    animation.finished.then(()=>{
      if(state.animation!==animation)return;state.animation=null;finished();
    }).catch(()=>{});
  }
  function capture(){
    if(disposed)return;
    for(const state of states.values())if(state.animation&&visible(state.indicator))state.box=relativeBox(state.indicator,state.group);
  }
  function releaseGroup(state){
    cancel(state);state.indicator.remove();
    state.group.removeAttribute('data-lume-motion-group');state.group.removeAttribute('data-lume-motion-static');
    state.group.querySelectorAll('[data-lume-motion-item]').forEach(item=>{
      item.removeAttribute('data-lume-motion-item');item.removeAttribute('data-lume-motion-selected');
    });
  }
  function syncGroups(motion){
    const live=new Set();
    for(const spec of groups)root.querySelectorAll(spec.selector).forEach((group,index)=>{
      const items=[...group.querySelectorAll(spec.items)],selected=items.find(item=>item.matches(spec.selected));
      if(!selected||!visible(group)||!visible(selected))return;
      const scope=group.closest('[data-settings-scope]')?.dataset.settingsScope||'';
      const key=[scope,spec.name,group.getAttribute('aria-labelledby')||index].join(':');
      live.add(key);
      const selectedId=identity(selected,items.indexOf(selected)),next=relativeBox(selected,group,spec.line);
      let state=states.get(key),from=state?.box,changed=!!state&&state.selected!==selectedId;
      if(state?.group===group&&state.animation)from=relativeBox(state.indicator,group);
      if(state?.group!==group||!state.indicator.isConnected){
        if(state)releaseGroup(state);
        const paint=window.getComputedStyle((spec.paint&&group.querySelector(spec.paint))||selected);
        const indicator=document.createElement('span');indicator.dataset.lumeMotionIndicator='';indicator.setAttribute('aria-hidden','true');
        Object.assign(indicator.style,{background:spec.line?paint.borderBottomColor:paint.backgroundColor,
          borderRadius:spec.line?'1px':paint.borderRadius,boxShadow:spec.line?'none':paint.boxShadow,
          border:spec.paint?`${paint.borderTopWidth} solid ${paint.borderTopColor}`:'none'});
        state={group,indicator,selected:selectedId,box:next,animation:null};states.set(key,state);
        group.toggleAttribute('data-lume-motion-static',window.getComputedStyle(group).position==='static');
        group.append(indicator);group.dataset.lumeMotionGroup=spec.line?'underline':'fill';
      }
      items.forEach(item=>{
        item.setAttribute('data-lume-motion-item','');item.toggleAttribute('data-lume-motion-selected',item===selected);
      });
      // Host polling and busy-state updates must not restart a running slide.
      if(!changed&&state.animation&&motion)return;
      cancel(state);Object.assign(state.indicator.style,frame(next));
      state.selected=selectedId;state.box=next;
      if(motion&&changed&&from&&distance(from,next)>.3&&canAnimate())
        animate(state,state.indicator,[frame(from),frame(next)],{duration:spec.duration,easing},()=>{state.box=next;});
    });
    for(const [key,state] of states)if(!live.has(key)){releaseGroup(state);states.delete(key);}
  }
  function anchorFor(menu){
    if(menu.id){
      const trigger=[...root.querySelectorAll('[aria-controls]')].find(item=>item.getAttribute('aria-controls')===menu.id);
      if(trigger)return trigger;
    }
    return menu.parentElement?.querySelector('[aria-expanded],.wallpaper-apply')||menu.previousElementSibling;
  }
  function entrance(menu){
    const box=menu.getBoundingClientRect(),anchor=anchorFor(menu)?.getBoundingClientRect();
    if(!anchor)return {origin:'50% 0%',translate:'0 -6px'};
    const sides=[Math.abs(box.top-anchor.bottom),Math.abs(box.bottom-anchor.top),Math.abs(box.left-anchor.right),Math.abs(box.right-anchor.left)];
    const side=sides.indexOf(Math.min(...sides));
    const x=Math.max(0,Math.min(100,((anchor.left+anchor.right)/2-box.left)/box.width*100));
    const y=Math.max(0,Math.min(100,((anchor.top+anchor.bottom)/2-box.top)/box.height*100));
    return side===0?{origin:`${x}% 0%`,translate:'0 -6px'}:side===1?{origin:`${x}% 100%`,translate:'0 6px'}:
      side===2?{origin:`0% ${y}%`,translate:'-6px 0'}:{origin:`100% ${y}%`,translate:'6px 0'};
  }
  function restoreMenu(state){
    const menu=state.menu;
    menu.removeAttribute('data-lume-motion-closing');menu.inert=state.inert;
    if(state.ariaHidden===null)menu.removeAttribute('aria-hidden');else menu.setAttribute('aria-hidden',state.ariaHidden);
  }
  function syncMenus(motion){
    for(const menu of root.querySelectorAll(menuSelector)){
      let state=menus.get(menu);
      if(!state){state={menu,open:false,animation:null,inert:menu.inert,ariaHidden:menu.getAttribute('aria-hidden')};menus.set(menu,state);}
      const open=!menu.hidden&&visible(menu);
      if(open===state.open)continue;
      state.open=open;
      if(open){
        const reversing=menu.hasAttribute('data-lume-motion-closing'),current=window.getComputedStyle(menu);
        const from=reversing?{opacity:current.opacity,translate:current.translate,scale:current.scale}:null;
        cancel(state);restoreMenu(state);
        const style=window.getComputedStyle(menu);state.display=style.display;
        state.entrance=entrance(menu);menu.style.transformOrigin=state.entrance.origin;
        menu.setAttribute('data-lume-motion-menu','');
        if(motion&&canAnimate())animate(state,menu,[from||{opacity:0,translate:state.entrance.translate,scale:'.98 .94'},
          {opacity:1,translate:'0 0',scale:'1 1'}],{duration:170,easing});
      }else if(menu.hidden&&motion&&canAnimate()&&state.entrance){
        // Preserve only the closing pixels. Business visibility is already
        // hidden; inert and aria-hidden keep the outgoing menu non-interactive.
        menu.style.setProperty('--lume-menu-display',state.display);menu.setAttribute('data-lume-motion-closing','');
        menu.inert=true;menu.setAttribute('aria-hidden','true');
        const current=window.getComputedStyle(menu),from={opacity:current.opacity,translate:current.translate,scale:current.scale};
        cancel(state);
        animate(state,menu,[from,{opacity:0,translate:state.entrance.translate,scale:'.98 .94'}],{duration:120,easing},()=>restoreMenu(state));
      }else {cancel(state);restoreMenu(state);}
    }
    for(const [menu,state] of menus)if(!menu.isConnected){cancel(state);restoreMenu(state);menus.delete(menu);}
  }
  function sync(motion=true){if(disposed)return;syncGroups(motion&&!initial);syncMenus(motion&&!initial);initial=false;}
  const schedule=()=>{if(disposed||queued)return;queued=true;queueMicrotask(()=>{queued=false;sync();});};
  const observer=new window.MutationObserver(records=>{
    if(records.some(record=>record.type==='attributes'?
      record.oldValue!==record.target.getAttribute(record.attributeName)&&
        (record.target.matches(menuSelector+',.application-settings,.MusicRoot,.settings')||!!record.target.closest(groupSelector)):
      [...record.addedNodes,...record.removedNodes].some(node=>node.nodeType===1&&!node.hasAttribute('data-lume-motion-indicator')&&
        (record.target.closest(groupSelector)||node.matches(groupSelector+','+menuSelector)||node.querySelector(groupSelector+','+menuSelector)))))schedule();
  });
  observer.observe(root,{subtree:true,childList:true,attributes:true,
    attributeOldValue:true,
    attributeFilter:['class','aria-selected','aria-pressed','aria-current','aria-checked','hidden']});
  const resize=()=>sync(false);
  const preference=()=>{
    if(reduced.matches){for(const state of states.values())cancel(state);for(const state of menus.values()){cancel(state);restoreMenu(state);}}
    sync(false);
  };
  root.addEventListener('click',capture,true);root.addEventListener('keydown',capture,true);
  window.addEventListener('resize',resize);reduced.addEventListener('change',preference);
  document.fonts?.addEventListener('loadingdone',resize);
  sync(false);
  return {capture,sync,dispose(){
    if(disposed)return;disposed=true;observer.disconnect();
    root.removeEventListener('click',capture,true);root.removeEventListener('keydown',capture,true);
    window.removeEventListener('resize',resize);reduced.removeEventListener('change',preference);
    document.fonts?.removeEventListener('loadingdone',resize);
    for(const state of states.values())releaseGroup(state);states.clear();
    for(const state of menus.values()){cancel(state);restoreMenu(state);state.menu.removeAttribute('data-lume-motion-menu');
      state.menu.style.removeProperty('--lume-menu-display');state.menu.style.removeProperty('transform-origin');}menus.clear();
  }};
}
