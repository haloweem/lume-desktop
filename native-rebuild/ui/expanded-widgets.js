import {t} from './localization.js';
import {syncSettingsControlBusy} from './settings-frame.js';

// Preserve existing base-6 and Clock base-7 layouts. Five-widget layouts use
// a compact permutation rank shared with core/dynamic_island/expanded_widgets.h.
const clockLayoutOffset=1296,fiveLayoutOffset=3697,countdownLayoutOffset=4417,countdownFiveOffset=8513,expandedLayoutMaximum=11032,maximumExpandedWidgets=5;
export const expandedWidgets=[
  ['music','音乐','Music','♫','歌曲、封面与播放控制','Track, artwork and playback controls'],
  ['calendar-weather','日历','Calendar','▦','月历、农历节气与今日天气','Month calendar, holidays and today’s weather'],
  ['focus','番茄钟','Focus timer','◴','开始一段 25 分钟的专注','Start a 25 minute focus session'],
  ['system','性能','Performance','▥','CPU、GPU、内存，以及笔记本电量','CPU, GPU and memory, with battery on laptops'],
  ['todos','待办事项','To-dos','✓','查看和完成今天的待办','View and complete today’s to-dos'],
  ['clock','时间','Clock','◷','当前日期、星期与24小时制时间','Current local date, weekday and 24-hour time'],
  ['countdown','倒数日','Countdown','◷','距离目标日期还有多少天','Days remaining until a target date'],
];
export function expandedWidgetIcon(id){
  const item=expandedWidgets[id];
  return item?islandCategoryIcon(item[0]):'';
}
export function islandCategoryIcon(name){
  const extension=name==='focus'||name==='countdown'?'svg':'png';
  return `<img class="island-expanded-icon" src="assets/icons/island-expanded/${name}.${extension}" width="24" height="24" alt="" aria-hidden="true" draggable="false">`;
}
export function expandedSelectionSummary(code){
  const ids=decodeExpandedLayout(code)??[0];
  return ids.length?ids.map(expandedWidgetIcon).join(''):'<span class="island-component-summary__empty">—</span>';
}
export function decodeExpandedLayout(code){
  if(!Number.isInteger(code)||code<0||code>expandedLayoutMaximum)return null;
  if(code>=countdownFiveOffset){
    let rank=code-countdownFiveOffset;const digits=Array(5),available=[0,1,2,3,4,5,6];
    for(let index=4;index>=0;--index){const radix=7-index;digits[index]=rank%radix;rank=Math.floor(rank/radix);}
    const ids=digits.map(digit=>available.splice(digit,1)[0]);return ids.includes(6)?ids:null;
  }
  if(code>=countdownLayoutOffset){
    let value=code-countdownLayoutOffset;const ids=[];
    while(value){const digit=value%8;value=Math.floor(value/8);
      if(!digit||ids.includes(digit-1)||ids.length===4)return null;ids.unshift(digit-1);}
    return ids.includes(6)?ids:null;
  }
  if(code>=fiveLayoutOffset){
    let rank=code-fiveLayoutOffset;
    const digits=Array(5),available=[0,1,2,3,4,5];
    for(let index=4;index>=0;--index){const radix=6-index;digits[index]=rank%radix;rank=Math.floor(rank/radix);}
    return digits.map(digit=>available.splice(digit,1)[0]);
  }
  const clockLayout=code>=clockLayoutOffset,base=clockLayout?7:6;
  if(clockLayout)code-=clockLayoutOffset;
  const ids=[];
  while(code){const digit=code%base;code=Math.floor(code/base);
    if(!digit||ids.includes(digit-1)||ids.length===4)return null;
    ids.unshift(digit-1);
  }
  if(clockLayout&&!ids.includes(5))return null;
  return ids;
}
export const validExpandedLayout=code=>decodeExpandedLayout(code)!==null;
export function encodeExpandedLayout(ids){
  if(!Array.isArray(ids)||ids.length>maximumExpandedWidgets||new Set(ids).size!==ids.length||ids.some(id=>!Number.isInteger(id)||id<0||id>=expandedWidgets.length))return null;
  if(ids.length===maximumExpandedWidgets){
    const countdown=ids.includes(6),available=countdown?[0,1,2,3,4,5,6]:[0,1,2,3,4,5];let rank=0;
    ids.forEach((id,index)=>{const digit=available.indexOf(id);rank=rank*((countdown?7:6)-index)+digit;available.splice(digit,1);});
    return (countdown?countdownFiveOffset:fiveLayoutOffset)+rank;
  }
  const countdown=ids.includes(6),clockLayout=ids.includes(5),base=countdown?8:clockLayout?7:6;
  const offset=countdown?countdownLayoutOffset:clockLayout?clockLayoutOffset:0;
  return offset+ids.reduce((code,id)=>code*base+id+1,0);
}
const busy=state=>['loading','saving','unavailable'].includes(state.dynamicIslandStatus);
const chipSelector='[data-island-expanded-chip]';
const reorderAnimations=new WeakMap();
const widgetId=node=>Number(node.dataset.islandExpandedChip);
function arrangeWidgets(grid,ids,settle){
  const nodes=[...grid.querySelectorAll(chipSelector)],byId=new Map(nodes.map(node=>[widgetId(node),node]));
  let next=0;
  const order=nodes.map(node=>ids.includes(widgetId(node))?ids[next++]:widgetId(node));
  if(!settle&&order.every((id,index)=>widgetId(nodes[index])===id))return;
  const before=new Map(nodes.map(node=>[node,node.getBoundingClientRect()]));
  nodes.forEach(node=>{reorderAnimations.get(node)?.cancel();reorderAnimations.delete(node);});
  settle?.();
  order.forEach((id,index)=>{const node=byId.get(id);if(grid.children[index]!==node)grid.insertBefore(node,grid.children[index]??null);});
  const view=grid.ownerDocument.defaultView;
  if(view.matchMedia('(prefers-reduced-motion: reduce)').matches)return;
  for(const node of nodes){
    if(node.classList.contains('is-dragging')||typeof node.animate!=='function')continue;
    const from=before.get(node),to=node.getBoundingClientRect(),scale=to.width/node.offsetWidth||1;
    const x=(from.left-to.left)/scale,y=(from.top-to.top)/scale;
    if(Math.abs(x)+Math.abs(y)<.5)continue;
    const animation=node.animate([{transform:`translate(${x}px,${y}px)`},{transform:'translate(0,0)'}],
      {duration:190,easing:'cubic-bezier(.2,.8,.2,1)'});
    reorderAnimations.set(node,animation);
    animation.finished.then(()=>{if(reorderAnimations.get(node)===animation)reorderAnimations.delete(node);}).catch(()=>{});
  }
}
export function expandedWidgetPicker(state,available=expandedWidgets.map((_,id)=>id)){
  const ids=decodeExpandedLayout(state.dynamicIsland.expandedLayout??1)||[0];
  const chip=id=>{
    const item=expandedWidgets[id],selected=ids.includes(id);
    return `<div class="island-expanded-chip ${selected?'is-enabled':''}" data-island-expanded-chip="${id}" draggable="false" ${selected&&!busy(state)&&available.length>1?'data-island-reorderable':''}>
      <button type="button" role="switch" data-island-expanded-toggle="${id}" aria-checked="${selected}" aria-label="${t(item[1],item[2])}" ${busy(state)||(!selected&&ids.length===maximumExpandedWidgets)?'disabled':''}>
        ${expandedWidgetIcon(id)}<span class="island-expanded-chip__name">${t(item[1],item[2])}</span><span class="island-widget-enable" aria-hidden="true"><span></span></span>
      </button>
      <span class="island-expanded-chip__order" ${selected&&available.length>1?'':'hidden'}>
        <button type="button" data-island-expanded-move="${id}" data-direction="-1" aria-label="${t('向前移动','Move earlier')} ${t(item[1],item[2])}">‹</button>
        <button type="button" data-island-expanded-move="${id}" data-direction="1" aria-label="${t('向后移动','Move later')} ${t(item[1],item[2])}">›</button>
      </span>
    </div>`;
  };
  return `<section class="island-expanded-picker" data-island-expanded-picker aria-label="${t('展开态组件','Expanded widgets')}">
    <div class="island-expanded-picker__widgets" data-expanded-widgets>${available.map(chip).join('')}</div>
  </section>`;
}
export function syncExpandedWidgetPicker(root,state){
  const picker=root.querySelector('[data-island-expanded-picker]');if(!picker)return;
  const ids=decodeExpandedLayout(state.dynamicIsland.expandedLayout??1)||[0],disabled=busy(state);
  const focused=picker.contains(document.activeElement)?document.activeElement:null;
  const grid=picker.querySelector('[data-expanded-widgets]');
  const nodes=new Map([...picker.querySelectorAll(chipSelector)].map(node=>[widgetId(node),node]));
  if(picker.classList.contains('is-drag-active')&&(disabled||picker.dataset.islandReorderLayout!==String(state.dynamicIsland.expandedLayout??1)))
    picker.dispatchEvent(new Event('island-reorder-cancel',{bubbles:true}));
  // Preserve the visible insertion order until native acknowledgement. A
  // failed save animates back to the confirmed layout without toggling widgets.
  const waiting=state.dynamicIslandStatus==='saving'&&picker.dataset.islandReorderPreview!==undefined;
  if(!picker.classList.contains('is-drag-active')&&!waiting)arrangeWidgets(grid,ids);
  if(state.dynamicIslandStatus!=='saving')delete picker.dataset.islandReorderPreview;
  for(const [id,node] of nodes){
    const index=ids.indexOf(id),chosen=index>=0,item=expandedWidgets[id];
    node.draggable=false;node.toggleAttribute('data-island-reorderable',chosen&&!disabled&&nodes.size>1);node.classList.toggle('is-enabled',chosen);
    node.title=t(item[4],item[5]);
    node.querySelector('.island-expanded-chip__order').hidden=!chosen||nodes.size<2;
    const toggle=node.querySelector('[data-island-expanded-toggle]');
    syncSettingsControlBusy(toggle,disabled||(!chosen&&ids.length===maximumExpandedWidgets));
    toggle.setAttribute('aria-checked',String(chosen));
    node.querySelectorAll('[data-island-expanded-move]').forEach(button=>{
      const next=index+Number(button.dataset.direction);syncSettingsControlBusy(button,disabled||next<0||next>=ids.length);
    });
  }
  picker.querySelectorAll('button').forEach(button=>button.toggleAttribute('data-island-pending',disabled&&state.dynamicIslandStatus==='saving'));
  if(focused?.isConnected&&document.activeElement!==focused&&!focused.disabled)focused.focus({preventScroll:true});
}
export function mountExpandedWidgetPicker(root,{getState,onChange}){
  const document=root.ownerDocument,view=document.defaultView;
  let drag=null,swallowClick=false;
  const current=()=>decodeExpandedLayout(getState().dynamicIsland.expandedLayout??1)||[0];
  const commit=ids=>{const code=encodeExpandedLayout(ids);if(!busy(getState())&&code!==null&&code!==getState().dynamicIsland.expandedLayout)onChange(code);};
  const move=(id,direction)=>{const ids=current(),index=ids.indexOf(id),next=index+direction;
    if(index<0||next<0||next>=ids.length)return;
    [ids[index],ids[next]]=[ids[next],ids[index]];commit(ids);
  };
  const click=event=>{
    if(swallowClick&&event.detail){swallowClick=false;event.preventDefault();event.stopImmediatePropagation();return;}
    const button=event.target.closest('button');if(!button||button.disabled||busy(getState()))return;
    if(button.dataset.islandExpandedToggle!==undefined){
      const id=Number(button.dataset.islandExpandedToggle),ids=current();if(!expandedWidgets[id])return;
      if(ids.includes(id))commit(ids.filter(value=>value!==id));
      else if(ids.length<maximumExpandedWidgets){
        const picker=button.closest('[data-island-expanded-picker]');
        commit([...picker.querySelectorAll('[data-island-expanded-chip]')].map(node=>Number(node.dataset.islandExpandedChip)).filter(value=>value===id||ids.includes(value)));
      }
    }else if(button.dataset.islandExpandedMove!==undefined)move(Number(button.dataset.islandExpandedMove),Number(button.dataset.direction));
  };
  const visibleOrder=value=>[...value.grid.querySelectorAll(chipSelector)].map(widgetId).filter(id=>current().includes(id));
  const inside=value=>{const box=value.grid.getBoundingClientRect();return value.x>=box.left-8&&value.x<=box.right+8&&value.y>=box.top-8&&value.y<=box.bottom+8;};
  const finish=save=>{
    const stopped=drag;if(!stopped)return;drag=null;
    view.cancelAnimationFrame(stopped.frame);stopped.observer?.disconnect();
    if(root.hasPointerCapture(stopped.id))root.releasePointerCapture(stopped.id);
    if(!stopped.active)return;
    swallowClick=true;root.style.cursor=stopped.cursor;
    const ids=save&&inside(stopped)&&!busy(getState())?visibleOrder(stopped):current();
    const code=encodeExpandedLayout(ids);
    arrangeWidgets(stopped.grid,ids,()=>{
      stopped.node.classList.remove('is-dragging');stopped.node.style.removeProperty('transform');stopped.node.style.removeProperty('transform-origin');
      stopped.picker.classList.remove('is-drag-active');delete stopped.picker.dataset.islandReorderLayout;
      for(const key of ['x','y','width','height'])stopped.grid.style.removeProperty(`--island-drag-slot-${key}`);
    });
    if(save&&code!==getState().dynamicIsland.expandedLayout&&!busy(getState())){
      stopped.picker.dataset.islandReorderPreview=String(code);commit(ids);
    }
    if(stopped.focus?.isConnected&&!stopped.focus.disabled)stopped.focus.focus({preventScroll:true});
  };
  const layoutBox=(node,grid)=>{
    const box=grid.getBoundingClientRect(),scale=box.width/grid.offsetWidth||1;
    return {left:box.left+node.offsetLeft*scale,top:box.top+node.offsetTop*scale,
      width:node.offsetWidth*scale,height:node.offsetHeight*scale,scale};
  };
  const position=()=>{
    const value=drag;if(!value?.active)return;
    if(!value.node.isConnected||value.picker.closest('[hidden]')||busy(getState())){finish(false);return;}
    if(inside(value)){
      const nodes=[...value.grid.querySelectorAll(chipSelector)].filter(node=>current().includes(widgetId(node)));
      const target=nodes.reduce((nearest,node)=>{
        const box=layoutBox(node,value.grid),distance=(value.x-box.left-box.width/2)**2+(value.y-box.top-box.height/2)**2;
        return !nearest||distance<nearest.distance?{node,distance}:nearest;
      },null)?.node;
      if(target&&target!==value.node){
        const ids=visibleOrder(value),from=ids.indexOf(widgetId(value.node)),to=ids.indexOf(widgetId(target));
        ids.splice(from,1);ids.splice(to,0,widgetId(value.node));arrangeWidgets(value.grid,ids);
      }
    }
    const box=layoutBox(value.node,value.grid);
    value.node.style.transform=`translate3d(${(value.x-value.grabX-box.left)/box.scale}px,${(value.y-value.grabY-box.top)/box.scale}px,0) scale(1.04)`;
    for(const [key,number] of Object.entries({x:value.node.offsetLeft,y:value.node.offsetTop,width:value.node.offsetWidth,height:value.node.offsetHeight}))
      value.grid.style.setProperty(`--island-drag-slot-${key}`,`${number}px`);
  };
  const frame=()=>{
    const value=drag;if(!value?.active)return;
    const scroller=value.picker.closest('.island-home__main');
    if(scroller){
      const box=scroller.getBoundingClientRect(),preview=scroller.querySelector('[data-island-preview-stage]')?.getBoundingClientRect();
      const top=Math.max(box.top,preview?.bottom??box.top)+18,bottom=box.bottom-18;
      if(value.y<top)scroller.scrollTop-=Math.min(12,(top-value.y)/3);
      else if(value.y>bottom)scroller.scrollTop+=Math.min(12,(value.y-bottom)/3);
    }
    position();if(drag===value)value.frame=view.requestAnimationFrame(frame);
  };
  const down=event=>{
    swallowClick=false;
    if(drag||event.button!==0||event.isPrimary===false||busy(getState()))return;
    const node=event.target.closest(chipSelector);
    if(!node||!node.hasAttribute('data-island-reorderable')||!current().includes(widgetId(node))||event.target.closest('.island-widget-enable,[data-island-expanded-move]'))return;
    const box=node.getBoundingClientRect(),picker=node.closest('[data-island-expanded-picker]');
    drag={id:event.pointerId,node,picker,grid:node.parentElement,x:event.clientX,y:event.clientY,startX:event.clientX,startY:event.clientY,
      grabX:event.clientX-box.left,grabY:event.clientY-box.top,frame:0,active:false,focus:node.querySelector('[data-island-expanded-toggle]')};
  };
  const pointerMove=event=>{
    if(!drag||event.pointerId!==drag.id)return;
    const value=drag;value.x=event.clientX;value.y=event.clientY;
    if(!value.active){
      if(Math.hypot(value.x-value.startX,value.y-value.startY)<5)return;
      if(busy(getState())||!value.node.isConnected){finish(false);return;}
      value.active=true;value.cursor=root.style.cursor;root.style.cursor='grabbing';
      root.setPointerCapture(value.id);
      reorderAnimations.get(value.node)?.cancel();
      value.picker.classList.add('is-drag-active');value.node.classList.add('is-dragging');
      value.picker.dataset.islandReorderLayout=String(getState().dynamicIsland.expandedLayout??1);
      const scale=value.node.getBoundingClientRect().width/value.node.offsetWidth||1;
      value.node.style.transformOrigin=`${value.grabX/scale}px ${value.grabY/scale}px`;
      value.observer=new view.MutationObserver(()=>{if(!value.node.isConnected||value.picker.closest('[hidden]'))finish(false);});
      value.observer.observe(root,{subtree:true,childList:true,attributes:true,attributeFilter:['hidden']});
      value.frame=view.requestAnimationFrame(frame);
    }
    event.preventDefault();position();
  };
  const up=event=>{if(drag&&event.pointerId===drag.id){drag.x=event.clientX;drag.y=event.clientY;if(drag.active)position();finish(true);}};
  const cancel=()=>finish(false);
  const visibility=()=>{if(document.hidden)cancel();};
  const nativeDrag=event=>{if(event.target.closest(chipSelector))event.preventDefault();};
  const key=event=>{
    if(drag&&['Escape','Tab'].includes(event.key)){if(event.key==='Escape')event.preventDefault();finish(false);return;}
    const node=event.target.closest('[data-island-expanded-chip]');
    if(node&&event.altKey&&['ArrowLeft','ArrowRight'].includes(event.key)){
      event.preventDefault();move(Number(node.dataset.islandExpandedChip),event.key==='ArrowLeft'?-1:1);
    }
  };
  const handlers={pointerdown:down,dragstart:nativeDrag,'island-reorder-cancel':cancel};
  for(const [name,handler] of Object.entries(handlers))root.addEventListener(name,handler);
  root.addEventListener('click',click,true);
  document.addEventListener('keydown',key,true);
  document.addEventListener('pointermove',pointerMove,{passive:false});document.addEventListener('pointerup',up);
  root.addEventListener('lostpointercapture',cancel);document.addEventListener('pointercancel',cancel);
  document.addEventListener('visibilitychange',visibility);view.addEventListener('blur',cancel);view.addEventListener('resize',cancel);
  return ()=>{
    finish(false);
    for(const [name,handler] of Object.entries(handlers))root.removeEventListener(name,handler);
    root.removeEventListener('click',click,true);
    document.removeEventListener('keydown',key,true);
    document.removeEventListener('pointermove',pointerMove);document.removeEventListener('pointerup',up);
    root.removeEventListener('lostpointercapture',cancel);document.removeEventListener('pointercancel',cancel);
    document.removeEventListener('visibilitychange',visibility);view.removeEventListener('blur',cancel);view.removeEventListener('resize',cancel);
  };
}
