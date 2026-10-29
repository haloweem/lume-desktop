import {getLineRenderEndTime,getRecentCompletedLine,getUpcomingLines,
  resolveVisualizerSubtitleOverlayContent,resolveSubtitleFontSizes,
  resolveThemeTranslationFontStack,resolveThemeFontWeight} from './lumiere/runtime.js';

// DOM adaptation of Folia's VisualizerSubtitleOverlay. Content selection, line
// tails, font stacks and sizes come from the original source in the bundle.
export function mountLumiereSubtitles(host){
  const document=host.ownerDocument,win=document.defaultView;
  const root=host.closest('[data-music-root]');
  const layer=document.createElement('div');layer.dataset.lumiereSubtitles='';
  layer.dataset.lumenicLiquidGlassScene='';
  Object.assign(layer.style,{position:'absolute',left:'0',right:'0',bottom:'112px',
    textAlign:'center',padding:'0 16px',zIndex:'20',pointerEvents:'none',opacity:'.6',
    transition:'bottom .24s ease-out'});
  host.append(layer);let key='',animation=null,disposed=false;
  function position(){
    const hidden=root?.classList.contains('is-immersive')&&!root.classList.contains('is-immersive-revealed');
    const bar=root?.querySelector('.MusicPlayerBar');
    const gap=bar?host.getBoundingClientRect().bottom-bar.getBoundingClientRect().top+16:112;
    layer.style.bottom=`${hidden?32:Math.max(112,gap)}px`;
  }
  const observer=root?new win.MutationObserver(position):null;
  observer?.observe(root,{attributes:true,attributeFilter:['class']});
  function hide(){animation?.cancel();animation=null;layer.hidden=true;key='';}
  return {
    position,hide,
    update(lines,time,theme){
      if(disposed)return;
      let index=-1;
      for(let i=0;i<lines.length&&lines[i].startTime<=time;i++)
        if(time<=getLineRenderEndTime(lines[i]))index=i;
      const activeLine=lines[index]||null;
      const recent=getRecentCompletedLine({lines,currentLineIndex:index,currentTime:time,getLineEndTime:getLineRenderEndTime});
      const content=resolveVisualizerSubtitleOverlayContent({showText:true,activeLine,
        recentCompletedLine:recent===lines.at(-1)?null:recent,
        nextLines:getUpcomingLines(lines,index,2)});
      // Lume shows translations only, never the upcoming-line strip.
      const rows=content.subtitleText?content.subtitleTracks.map(track=>track.text):[];
      if(!rows.length){hide();return;}
      position();
      const translation=!!content.subtitleText;
      const nextKey=JSON.stringify([rows,translation,theme.fontFamily,activeLine?.startTime,recent?.startTime]);
      if(nextKey===key)return;key=nextKey;
      animation?.cancel();layer.replaceChildren();layer.hidden=false;
      const wrap=document.createElement('div');
      Object.assign(wrap.style,{position:'relative',isolation:'isolate',display:'inline-block',padding:'2px 6px',maxWidth:'100%'});
      const glow=document.createElement('div');glow.setAttribute('aria-hidden','true');
      Object.assign(glow.style,{position:'absolute',inset:'-24px -40px',zIndex:'0',filter:'blur(24px)',
        background:'radial-gradient(ellipse 115% 130% at center,rgba(9,9,11,.96) 0%,rgba(9,9,11,.78) 62%,transparent 100%)',transform:'translateZ(0)'});
      wrap.append(glow);
      const sizes=resolveSubtitleFontSizes(1);
      for(const [i,text] of rows.entries()){
        const row=document.createElement('div');row.textContent=text;
        row.dataset.lumiereSubtitle=translation?'translation':'upcoming';
        Object.assign(row.style,{position:'relative',zIndex:'1',color:theme.secondaryColor,
          fontFamily:resolveThemeTranslationFontStack(theme),fontWeight:String(resolveThemeFontWeight(theme,translation?500:400)),
          fontSize:translation?sizes.translationFontSize:sizes.upcomingFontSize,
          maxWidth:translation?'56rem':'42rem',margin:`${i?8:0}px auto 0`,
          textShadow:'0 1px 2px rgba(9,9,11,.24)',filter:translation?'none':'blur(1px)'});
        if(!translation)Object.assign(row.style,{whiteSpace:'nowrap',overflow:'hidden',textOverflow:'ellipsis'});
        wrap.append(row);
      }
      layer.append(wrap);
      animation=wrap.animate?.([{opacity:0,transform:'translateY(10px)'},{opacity:1,transform:'translateY(0)'}],{duration:240,easing:'ease-out'});
    },
    dispose(){if(disposed)return;disposed=true;hide();observer?.disconnect();layer.remove();}
  };
}
