import {escapeHTML as h,toggle} from './dom.js';
import {t} from './localization.js';
import {syncSettingsControlBusy} from './settings-frame.js';

// Order is the native MiniWidget protocol range. Persisted values use ids.
export const islandMiniWidgets=[
 ['none','不显示','None','—','time'],['date','日期','Date','11/1','time'],
 ['weekday','星期','Weekday','周日','time'],['lunar','农历','Lunar date','九月','time'],
 ['clock','时钟','Clock','◷','time'],['dayProgress','日进度','Day progress','64%','time'],
 ['weekProgress','周进度','Week progress','42%','time'],['monthProgress','月进度','Month progress','16%','time'],
 ['yearProgress','年进度','Year progress','83%','time'],['battery','电池','Battery','73%','system'],
 ['cpu','处理器','CPU','14%','system'],['memory','内存','Memory','52%','system'],
 ['disk','磁盘','Disk','62%','system'],['weather','天气','Weather','☀','weather'],
 ['temperature','温度','Temperature','23°','weather'],['humidity','湿度','Humidity','65%','weather'],
 ['wind','风速','Wind','3.2','weather'],['network','网速','Network','2.4M','system'],
 ['music','音乐波形','Music','▂▆▃▇','media'],['focus','专注计时','Focus','25:00','media'],
 ['planet','星球','Planet','🪐','fun'],['flame','篝火','Campfire','🔥','fun'],
 ['heart','爱心','Heart','♥','fun'],['gift','礼物','Gift','🎁','fun'],
 ['butterfly','蝴蝶','Butterfly','🦋','fun'],['moon','月亮','Moon','☾','fun'],
 ['snow','雪花','Snow','❄','fun'],['orbit','轨道','Orbit','✧','fun'],
 ['coffee','咖啡','Coffee','☕','fun'],['cat','小猫','Cat','🐈','fun'],['tree','小树','Tree','🌲','fun'],
];
export const islandExpandedWidgets=[
 ['music','音乐歌词','Music & lyrics','播放控制与滚动歌词','Playback controls and lyrics'],
 ['calendar','日历','Calendar','月历、节日与日程','Month, holidays and agenda'],
 ['weather','天气','Weather','当前天气与小时预报','Current conditions and hourly forecast'],
 ['performance','系统信息','System information','CPU、内存与网速','CPU, memory and network'],
 ['todo','待办事项','To-dos','与日历共用待办数据','Shared calendar to-dos'],
 ['reminder','提醒','Reminders','查看收到的提醒','Review delivered reminders'],
 ['focus','番茄钟','Focus timer','25 分钟专注与结束提示','25-minute focus timer'],
 ['launchpad','快捷启动','Quick launch','打开 Dock 启动台','Open the Dock launchpad'],
 ['actions','快捷操作','Quick actions','常用入口与操作','Frequently used actions'],
];
export const defaultIslandCustomization=()=>({
 version:1,customIcons:false,overview:false,left:{widget:'date',style:'circle'},right:{widget:'battery',style:'circle'},
 enabled:[true,true,true,true,true,true,false,false,false],order:islandExpandedWidgets.map(row=>row[0]),
});
export function validIslandCustomization(c){
 const ids=islandMiniWidgets.map(row=>row[0]),cards=islandExpandedWidgets.map(row=>row[0]);
 const side=s=>!!s&&typeof s==='object'&&!Array.isArray(s)&&Object.keys(s).length===2&&
   ids.includes(s.widget)&&['circle','capsule'].includes(s.style);
 return !!c&&typeof c==='object'&&!Array.isArray(c)&&Object.keys(c).length===7&&c.version===1&&
   typeof c.customIcons==='boolean'&&typeof c.overview==='boolean'&&side(c.left)&&side(c.right)&&
   Array.isArray(c.enabled)&&c.enabled.length===cards.length&&c.enabled.every(x=>typeof x==='boolean')&&
   Array.isArray(c.order)&&c.order.length===cards.length&&new Set(c.order).size===cards.length&&c.order.every(id=>cards.includes(id));
}
export function validIslandCustomizationCommand(action,value){
 return (['customIcons','overview'].includes(action)&&typeof value==='boolean')||
   (['leftWidget','rightWidget'].includes(action)&&Number.isInteger(value)&&value>=0&&value<islandMiniWidgets.length)||
   (['leftStyle','rightStyle'].includes(action)&&['circle','capsule'].includes(value))||
   (action==='card'&&Number.isInteger(value)&&value>=0&&value<islandExpandedWidgets.length*4)||
   (action==='cardMove'&&Number.isInteger(value)&&value>=0&&value<islandExpandedWidgets.length*islandExpandedWidgets.length);
}
const preferences=state=>state.dynamicIsland.customization||defaultIslandCustomization();
const title=row=>t(row[1],row[2]);
const preview=(row,style)=>'<span aria-hidden="true" class="island-mini-preview '+style+' mini-'+row[4]+
  '" data-mini="'+row[0]+'"><span>'+h(row[3])+'</span></span>';
const groups=()=>[['time',t('时间与进度','Time & progress')],['system',t('系统监控','System')],
 ['weather',t('天气','Weather')],['media',t('音乐与专注','Music & focus')],['fun',t('趣味动效','Animated accents')]];
function gallery(c,side,disabled){
 const p=c[side],label=side==='left'?t('左侧显示','Left display'):t('右侧显示','Right display');
 let result='<section class="island-slot" data-island-slot="'+side+'" data-style="'+p.style+'"><header><h3>'+label+
  '</h3><button class="reset island-clear" type="button" data-action="dynamicIsland-'+side+'Widget" data-value="0"'+
  (disabled?' disabled':'')+'>'+t('清除图标','Clear')+'</button></header><div class="island-style-picker" role="group" aria-label="'+label+'">';
 for(const style of ['circle','capsule'])result+='<button type="button" data-action="dynamicIsland-'+side+'Style" data-value="'+style+
  '" aria-pressed="'+(p.style===style)+'"'+(disabled?' disabled':'')+'>'+ (style==='circle'?t('圆形','Circle'):t('胶囊','Capsule'))+'</button>';
 result+='</div><div class="island-mini-gallery">';
 for(const [group,name] of groups()){
  result+='<h4>'+name+'</h4><div class="island-mini-grid">';
  islandMiniWidgets.forEach((row,index)=>{if(!index||row[4]!==group)return;
   result+='<button type="button" class="island-mini-choice" data-action="dynamicIsland-'+side+'Widget" data-value="'+index+
    '" aria-pressed="'+(p.widget===row[0])+'" aria-label="'+h(title(row))+'"'+(disabled?' disabled':'')+'>'+
    preview(row,p.style)+'<span class="island-mini-label">'+h(title(row))+'</span></button>';
  });result+='</div>';
 }
 return result+'</div></section>';
}
export function islandCustomizationSettings(state){
 const c=preferences(state),disabled=['loading','saving','unavailable'].includes(state.dynamicIslandStatus);
 let result='<div class="island-customization"><div class="setting-row island-option"><label>'+t('自选左右显示','Customize left and right displays')+
  '<small>'+t('左右独立选择；圆形、胶囊和趣味样式可自由组合','Choose each side independently and mix information with animated accents')+'</small></label>'+
  toggle(c.customIcons,t('自选左右显示','Customize left and right displays'),'dynamicIsland-customIcons',disabled)+'</div>'+
  '<div class="island-slots">'+gallery(c,'left',disabled)+gallery(c,'right',disabled)+'</div><p class="island-sample-note">'+
  t('图库为样式示例，实际数据由桌面胶囊显示。选择后自动启用；无电池、无天气或首个性能采样时显示横线。','Gallery values illustrate the styles. The desktop capsule uses real data; unavailable readings show a dash. Selecting a widget enables custom displays.')+
  '</p><div class="setting-row island-option"><label>'+t('多组件工作台','Multi-widget workspace')+'<small>'+
  t('展开时并排显示已选组件，点击进入详情','Show selected widgets together and click for details')+'</small></label>'+
  toggle(c.overview,t('多组件工作台','Multi-widget workspace'),'dynamicIsland-overview',disabled)+'</div>'+
  '<section class="island-expanded-library"><h3>'+t('展开面板小组件','Expanded panel widgets')+'</h3><p>'+
  t('拖动卡片排序，或使用前移、后移按钮；关闭只隐藏入口。','Drag cards to reorder, or use the move buttons. Turning a card off only hides its entry.')+
  '</p><div class="island-expanded-grid">';
 c.order.forEach((id,position)=>{
  const index=islandExpandedWidgets.findIndex(row=>row[0]===id),row=islandExpandedWidgets[index],enabled=c.enabled[index];
  const action=index*4+(enabled?1:0);
  result+='<article class="island-card-option" data-island-card="'+id+'" draggable="'+(!disabled)+'"><div class="island-card-heading"><span class="island-card-grip" aria-hidden="true">⠿</span><h4>'+
   h(title(row))+'</h4>'+toggle(enabled,title(row),'dynamicIsland-card',disabled).replace('<button','<button data-value="'+action+'"')+
   '</div><p>'+h(t(row[3],row[4]))+'</p><div class="island-card-order"><button type="button" data-action="dynamicIsland-card" data-value="'+(index*4+2)+
   '" aria-label="'+h(t('前移','Move earlier')+' '+title(row))+'"'+(disabled||!position?' disabled':'')+'>←</button><button type="button" data-action="dynamicIsland-card" data-value="'+(index*4+3)+
   '" aria-label="'+h(t('后移','Move later')+' '+title(row))+'"'+(disabled||position===c.order.length-1?' disabled':'')+'>→</button></div></article>';
 });
 return result+'</div></section></div>';
}
export function syncIslandCustomization(root,state,disabled){
 const c=preferences(state);
 for(const side of ['left','right']){
  const slot=root.querySelector('[data-island-slot="'+side+'"]');if(!slot)continue;
  slot.dataset.style=c[side].style;
  slot.querySelectorAll('[data-action="dynamicIsland-'+side+'Widget"]').forEach(button=>{
   const row=islandMiniWidgets[Number(button.dataset.value)];
   button.setAttribute('aria-pressed',String(c[side].widget===row[0]));syncSettingsControlBusy(button,disabled);
  });
  slot.querySelectorAll('[data-action="dynamicIsland-'+side+'Style"]').forEach(button=>{
   button.setAttribute('aria-pressed',String(c[side].style===button.dataset.value));syncSettingsControlBusy(button,disabled);
  });
  slot.querySelectorAll('.island-mini-preview').forEach(p=>{p.classList.toggle('circle',c[side].style==='circle');p.classList.toggle('capsule',c[side].style==='capsule');});
 }
 for(const key of ['customIcons','overview'])root.querySelectorAll('[data-action="dynamicIsland-'+key+'"]').forEach(button=>{
  button.classList.toggle('checked',c[key]);button.setAttribute('aria-checked',String(c[key]));syncSettingsControlBusy(button,disabled);
 });
 const grid=root.querySelector('.island-expanded-grid');
 if(grid)c.order.forEach((id,position)=>{
  const card=grid.querySelector('[data-island-card="'+id+'"]');if(!card)return;
  grid.appendChild(card);card.draggable=!disabled;const index=islandExpandedWidgets.findIndex(row=>row[0]===id);
  const buttons=card.querySelectorAll('[data-action="dynamicIsland-card"]');
  buttons.forEach(button=>{
   const op=Number(button.dataset.value)%4;
   if(button.getAttribute('role')==='switch'){button.dataset.value=String(index*4+(c.enabled[index]?1:0));button.classList.toggle('checked',c.enabled[index]);button.setAttribute('aria-checked',String(c.enabled[index]));}
   syncSettingsControlBusy(button,disabled||(op===2&&!position)||(op===3&&position===c.order.length-1));
  });
 });
}
export function mountIslandCustomization(root,send){
 let dragging='';
 const start=event=>{const card=event.target.closest?.('[data-island-card]');if(!card||!card.draggable)return;
  dragging=card.dataset.islandCard;event.dataTransfer?.setData('text/plain',dragging);if(event.dataTransfer)event.dataTransfer.effectAllowed='move';event.stopPropagation();};
 const over=event=>{if(dragging&&event.target.closest?.('[data-island-card]')){event.preventDefault();event.stopPropagation();}};
 const drop=event=>{
  const target=event.target.closest?.('[data-island-card]');if(!dragging||!target)return;
  event.preventDefault();event.stopPropagation();
  const order=[...root.querySelectorAll('[data-island-card]')].map(card=>card.dataset.islandCard);
  const from=order.indexOf(dragging),to=order.indexOf(target.dataset.islandCard);
  const index=islandExpandedWidgets.findIndex(row=>row[0]===dragging);
  dragging='';if(from<0||to<0||from===to||index<0)return;
  send('cardMove',index*islandExpandedWidgets.length+to);
 };
 const end=()=>{dragging='';};
 root.addEventListener('dragstart',start);root.addEventListener('dragover',over);root.addEventListener('drop',drop);root.addEventListener('dragend',end);
 return ()=>{root.removeEventListener('dragstart',start);root.removeEventListener('dragover',over);root.removeEventListener('drop',drop);root.removeEventListener('dragend',end);};
}
