import {createGalleryCore,galleryFonts,createGalleryScroll,galleryAnchor,galleryStatus,galleryToneAt,
  galleryPositions,createGalleryFocus,gallerySweep,galleryGlow,splitGalleryToken,
  GALLERY_SCROLL_RESET_MS,GALLERY_FONT_STACK,GALLERY_WINDOW} from './gallery-model.js';
import {preferenceSnapshot} from './reference/preference-protocol.js';
import {resolvePreferenceTheme,resolveThemeFontStack} from './reference/preference-theme.js';
import {mountGalleryBackground} from './gallery-background.js';

// Owns one Gallery stage. The caller owns playback, footer, state validation and CSS loading.
const BACKGROUND='assets/music/forest.webp';
const EMPTY_LINES=Object.freeze([]);
const rgb=hex=>[1,3,5].map(offset=>parseInt(hex.slice(offset,offset+2),16)).join(',');
const segmenter=new Intl.Segmenter(undefined,{granularity:'grapheme'});
function sameLyrics(a,b) {
  if(a===b)return true;
  if(a.length!==b.length)return false;
  return a.every((line,i)=>{
    const other=b[i];
    if(line===other)return true;
    if(line.startTime!==other.startTime||line.endTime!==other.endTime||line.fullText!==other.fullText||line.translation!==other.translation||line.words.length!==other.words.length)return false;
    return line.words.every((word,j)=>{const otherWord=other.words[j];return word.text===otherWord.text&&word.startTime===otherWord.startTime&&word.endTime===otherWord.endTime;});
  });
}

export function mountGallery(host,initialState) {
  preferenceSnapshot(undefined,initialState?.preferences);
  const doc=host.ownerDocument,win=doc.defaultView;
  let core=createGalleryCore(doc),disposed=false,state=null,raf=null,resetTimer=null;
  let resetDue=0,resetRemaining=null,visible=false,lastTouchY=null;
  let layoutDirty=true,initialized=false;
  let dragged=false;
  const focus=createGalleryFocus(0);
  const clamp=(value,min,max)=>Math.min(max,Math.max(min,value));
  let preferences,fontStack=GALLERY_FONT_STACK,primaryRgb='255,255,255',fillRgb='255,255,255';
  const color=alpha=>`rgba(${primaryRgb},${alpha})`;
  const fillColor=alpha=>`rgba(${fillRgb},${alpha})`;
  let fonts=galleryFonts(win.innerWidth),size={width:0,height:0};
  let measurementCount=0,measurementVersion=0;
  const layouts=new Map(),rows=new Map(),scroll=createGalleryScroll(),removers=[];
  const media=win.matchMedia?.('(prefers-reduced-motion: reduce)');
  const element=(tag,className,parent,attribute)=>{
    const node=doc.createElement(tag);node.className=className;
    if(attribute)node.setAttribute(attribute,'true');
    parent?.append(node);return node;
  };
  const root=element('div','MusicGallery',host,'data-gallery-visualizer');
  const backdrop=element('img','MusicGalleryBackdrop',root,'data-gallery-backdrop');
  backdrop.alt='';backdrop.src=BACKGROUND;backdrop.draggable=false;
  const info=element('header','MusicGalleryInfo',root,'data-gallery-song-info');
  const title=element('h2','MusicGalleryTitle',info),artist=element('p','MusicGalleryArtist',info);
  const main=element('div','MusicGalleryMain',root);
  const artSection=element('section','MusicGalleryArtSection',main);
  const artwork=element('div','MusicGalleryArtwork',artSection,'data-gallery-artwork');
  artwork.dataset.galleryArtworkSource='image';
  const sleeve=element('div','MusicGallerySleeve',artwork);
  const sleeveImage=element('img','',sleeve);sleeveImage.alt='';sleeveImage.draggable=false;
  const disc=element('div','MusicGalleryDisc',artwork,'data-gallery-disc');
  const discImage=element('img','',disc);discImage.alt='';discImage.draggable=false;
  const background=mountGalleryBackground({root,backdrop,artwork,sleeveImage,discImage});
  element('div','MusicGalleryGrooves',disc);element('div','MusicGalleryDiscLabel',disc);element('div','MusicGalleryDiscHole',disc);
  const boundary=element('section','MusicGalleryBoundary',main,'data-gallery-lyrics-boundary');
  boundary.dataset.galleryLyrics='true';boundary.setAttribute('aria-label','歌词');
  const rail=element('div','MusicGalleryRail',boundary,'data-monet-lyrics-viewport');
  const empty=element('p','MusicGalleryEmpty',rail,'data-gallery-empty');empty.textContent='暂无歌词';empty.setAttribute('role','status');

  function listen(target,type,fn,options) {
    target?.addEventListener?.(type,fn,options);
    removers.push(()=>target?.removeEventListener?.(type,fn,options));
  }
  function clearTimer() {if(resetTimer!==null)win.clearTimeout(resetTimer);resetTimer=null;}
  function clearMeasurements() {layouts.clear();core?.clearGalleryMeasurements();measurementCount=0;measurementVersion++;}
  function resetBrowsing() {clearTimer();resetRemaining=null;scroll.reset();lastTouchY=null;}
  function scheduleReset(delay=GALLERY_SCROLL_RESET_MS) {
    clearTimer();resetRemaining=delay;
    if(!visible||disposed)return;
    resetDue=win.performance.now()+delay;
    resetTimer=win.setTimeout(()=>{resetTimer=null;resetRemaining=null;scroll.reset();lastTouchY=null;render();},delay);
  }
  function removeRow(row) {row.element.remove();row.words.length=0;rows.delete(row.index);}
  // Full height of a line at the focused size: every line keeps that room so a
  // line growing into focus never presses on its neighbours, as in Pendolo.
  function lineHeight(index) {return measure(index).heightPx;}
  function measure(index) {
    const line=state.lines[index],glow=Math.round(fonts.lyric*1.2),width=Math.max(1,(size.width||680)-glow*2-8);
    const key=JSON.stringify([index,line.fullText,fonts.lyric,width,fontStack]);
    if(layouts.has(key))return layouts.get(key);
    // Pretext's internal caches are also bounded over long sessions, not only this layout map.
    if(++measurementCount>=240){core.clearGalleryMeasurements();measurementCount=0;}
    let layout;
    try {
      layout=core.measureMonetLineLayout({line,status:'active',fontPx:fonts.lyric,
        translationFontPx:fonts.translation,fontStack,fontWeight:600,
        translationFontWeight:500,maxWidthPx:width,showSubtitleTranslation:false});
    } catch {
      const textLineCount=Math.max(1,Math.ceil(Array.from(segmenter.segment(line.fullText)).length/Math.max(1,Math.floor(width/(fonts.lyric*.9)))));
      layout={textLineCount,lineHeightPx:fonts.lyric*1.18,textPaddingTopPx:Math.max(fonts.lyric*.16,8),
        textPaddingBottomPx:Math.max(fonts.lyric*.34,14)};
    }
    // Every wrapped row is shown: no clipping of long lines.
    const lines=Math.max(1,layout.textLineCount||1);
    const result={lineHeightPx:layout.lineHeightPx,textPaddingTopPx:layout.textPaddingTopPx,
      textPaddingBottomPx:layout.textPaddingBottomPx,contentWidth:width,
      heightPx:lines*layout.lineHeightPx+layout.textPaddingTopPx+layout.textPaddingBottomPx};
    if(layouts.size>=240)layouts.delete(layouts.keys().next().value);
    layouts.set(key,result);return result;
  }
  function makeWords(row) {
    row.words.length=0;row.tokens.replaceChildren();
    const line=state.lines[row.index],layout=measure(row.index),spec=`600 ${fonts.lyric}px ${fontStack}`;
    for(const token of core.buildMonetDisplayTokens(line)) {
      if(!token.timed){const node=element('span','',row.tokens);node.textContent=token.text;row.words.push({token,base:node});continue;}
      let offsets;
      try {offsets=core.measureMonetGraphemeOffsets(token.text,fonts.lyric,spec);}
      catch {offsets=[0];for(const {segment} of segmenter.segment(token.text))offsets.push(offsets.at(-1)+Math.max(fonts.lyric*.55,segment.trim()?fonts.lyric*.9:fonts.lyric*.45));}
      for(const part of splitGalleryToken(token,offsets,Math.max(1,layout.contentWidth))) {
        const span=element('span','MusicGalleryWord',row.tokens);
        const base=element('span','',span);base.textContent=part.token.text;
        // Every row owns its fill once; a status change only repaints it.
        const fill=element('span','MusicGalleryFill',span);fill.setAttribute('aria-hidden','true');
        const fillText=element('span','MusicGalleryFillText',fill);fillText.textContent=part.token.text;
        row.words.push({token:part.token,base,fill,fillText,offsets:part.offsets});
      }
    }
    paintFillColor(row);
  }
  function paintFillColor(row) {
    for(const word of row.words)if(word.fillText)
      word.fillText.style.backgroundImage=`linear-gradient(90deg,${fillColor(1)} 0%,${fillColor(.92)} 68%,${fillColor(.72)} 100%)`;
  }
  function paintWords(row,time) {
    const status=row.status,active=status==='active',passed=status==='passed';
    const browsing=scroll.anchor!==null;
    const baseAlpha=active?.34:passed?.36:.46;
    for(const word of row.words) {
      const {token,base,fill}=word;
      if(!token.timed){base.style.color=color(baseAlpha);continue;}
      base.style.color=passed||(active&&time>token.endTime)?fillColor(.98):color(baseAlpha);
      const glow=browsing||(!active&&!passed)?0:galleryGlow(token.glowToken??token,time,row.renderEndTime);
      const alpha=glow*.88;
      base.style.textShadow=glow>0?`0 0 ${Math.round(fonts.lyric*.28)}px ${fillColor(alpha)}, 0 0 ${Math.round(fonts.lyric*.65)}px ${fillColor(alpha)}`:'none';
      fill.hidden=!active;
      if(active) {
        const sweep=gallerySweep(token,word.offsets,time,fonts.lyric);
        fill.style.maskImage=sweep.mask;fill.style.webkitMaskImage=sweep.mask;
      }
    }
  }
  // Places every row from the one focus value: a continuous position and tone.
  function place(value) {
    const count=state.lines.length,base=Math.round(clamp(value,0,count-1));
    const from=base-GALLERY_WINDOW-1,to=base+GALLERY_WINDOW+1,glow=Math.round(fonts.lyric*1.2);
    const fallback=fonts.lyric*1.18+Math.max(fonts.lyric*.5,22);
    const positions=galleryPositions(base,Math.min(from,Math.floor(value)),Math.max(to,Math.ceil(value)),count,lineHeight,fallback);
    const offset=(size.height||340)*.46-positions.at(value);
    const inactiveScale=clamp(fonts.inactive/Math.max(fonts.lyric,1),.72,.92);
    for(const row of rows.values()) {
      const distance=Math.abs(row.index-value);
      const tone=galleryToneAt(distance,inactiveScale,row.index<value);
      row.element.style.transform=`translateY(${offset+positions.top(row.index)}px) scale(${tone.scale})`;
      row.element.style.opacity=String(tone.opacity);row.element.style.filter=tone.blurPx>.05?`blur(${tone.blurPx.toFixed(2)}px)`:'';
      row.element.style.zIndex=String(Math.max(0,10-Math.round(distance)));
      row.text.style.textShadow=distance<.5?'0 14px 34px rgba(10,10,12,.22)':'none';
    }
    root.style.setProperty('--gallery-glow',`${glow}px`);
  }
  function needsFrame() {return visible&&!disposed&&focus.moving;}
  function requestFrame() {if(raf===null&&needsFrame())raf=win.requestAnimationFrame(tick);}
  function tick(now) {
    raf=null;if(!visible||disposed||!state)return;
    place(focus.sample(now).value);requestFrame();
  }
  function render() {
    if(disposed||!state)return;
    if(scroll.anchor===null)delete rail.dataset.galleryScrollAnchor;
    else rail.dataset.galleryScrollAnchor=String(scroll.anchor);
    if(!visible){layoutDirty=true;return;}
    const lines=state.lines,count=lines.length;
    empty.hidden=count>0;
    root.style.setProperty('--gallery-font',`${fonts.lyric}px`);
    if(!count){for(const row of [...rows.values()])removeRow(row);return;}
    const playing=galleryAnchor(lines,state.activeLineIndex,state.currentTime);
    const target=scroll.anchor??playing,now=win.performance.now();
    focus.to(target,now,!initialized||!!media?.matches);
    const value=focus.sample(now).value;
    // Rows exist for every line the focus passes through on its way to the target.
    const low=Math.max(0,Math.floor(Math.min(value,target))-GALLERY_WINDOW);
    const high=Math.min(count-1,Math.ceil(Math.max(value,target))+GALLERY_WINDOW);
    for(const row of [...rows.values()])if(row.index<low||row.index>high)removeRow(row);
    // Font, width or font loading changes re-measure rows in place.
    const relayout=layoutDirty;layoutDirty=false;
    for(let index=low;index<=high;index++) {
      let row=rows.get(index);
      const created=!row;
      if(created) {
        const node=element('div','MusicGalleryLine',rail);
        const text=element('div','MusicGalleryText',node),tokens=element('span','MusicGalleryTokens',text);
        row={index,element:node,text,tokens,words:[],status:''};rows.set(index,row);
        node.dataset.galleryLyricLine=String(index);
        row.renderEndTime=core.getLineRenderEndTime(lines[index]);
      }
      if(created||relayout) {
        const layout=measure(index),style=row.text.style,glow=Math.round(fonts.lyric*1.2);
        row.element.style.height=`${layout.heightPx}px`;
        style.paddingTop=`${layout.textPaddingTopPx+glow}px`;
        style.paddingBottom=`${layout.textPaddingBottomPx+glow}px`;
        style.lineHeight=`${layout.lineHeightPx}px`;
        makeWords(row);
      }
      // Status follows playback, not browsing: browsing only moves the focus.
      const status=galleryStatus(index,playing);
      if(row.status!==status){row.status=status;row.element.dataset.galleryLineStatus=status;}
      paintWords(row,state.currentTime);
    }
    initialized=true;place(value);requestFrame();
  }
  function measureSize() {
    if(disposed)return;
    const next={width:Math.round(rail.clientWidth),height:Math.round(rail.clientHeight)};
    const nextFonts=galleryFonts(win.innerWidth);
    if(next.width===size.width&&next.height===size.height&&fonts.lyric===nextFonts.lyric)return;
    size=next;fonts=nextFonts;clearMeasurements();layoutDirty=true;render();
  }
  function applyVisibility() {
    const now=win.performance.now(),next=state.visible&&!doc.hidden;
    background.update(state.backgroundScene,next);
    root.hidden=!state.visible;
    disc.style.animationPlayState=next&&state.playing&&!media?.matches?'running':'paused';
    if(next===visible)return;
    if(!next) {
      visible=false;
      if(raf!==null)win.cancelAnimationFrame(raf);raf=null;
      if(resetTimer!==null)resetRemaining=Math.max(0,resetDue-now);
      clearTimer();
    } else {
      // The focus does not keep moving while hidden: it resumes settled.
      focus.to(focus.target,now,true);
      visible=true;
      if(resetRemaining!==null)scheduleReset(resetRemaining);
      measureSize();render();requestFrame();
    }
  }
  function fallbackAnchor() {
    return scroll.anchor??Math.round(clamp(galleryAnchor(state.lines,state.activeLineIndex,state.currentTime),0,Math.max(0,state.lines.length-1)));
  }
  function browse(kind,delta) {
    const previous=scroll.anchor;scroll.move(kind,delta,fallbackAnchor(),state.lines.length);
    scheduleReset();if(previous!==scroll.anchor)render();
  }
  const options={passive:false};
  listen(rail,'wheel',event=>{
    if(!visible||!state.lines.length)return;
    if(event.cancelable)event.preventDefault();event.stopPropagation();
    const unit=event.deltaMode===1?16:event.deltaMode===2?(size.height||340):1;
    browse('wheel',event.deltaY*unit);
  },options);
  listen(rail,'touchstart',event=>{
    if(!visible||!state.lines.length)return;
    event.stopPropagation();lastTouchY=event.touches[0]?.clientY??null;dragged=false;
    scroll.begin(fallbackAnchor(),state.lines.length);scheduleReset();render();
  },options);
  listen(rail,'touchmove',event=>{
    if(!visible||lastTouchY===null||!state.lines.length)return;
    if(event.cancelable)event.preventDefault();event.stopPropagation();
    const next=event.touches[0]?.clientY;if(typeof next!=='number')return;
    const delta=lastTouchY-next;lastTouchY=next;if(Math.abs(delta)>2)dragged=true;browse('touch',delta);
  },options);
  for(const type of ['touchend','touchcancel'])listen(rail,type,()=>{
    if(!visible)return;lastTouchY=null;scroll.endTouch();scheduleReset();
  },options);
  // A tap on a line seeks there; the music page owns playback and decides.
  listen(rail,'click',event=>{
    if(!visible||dragged||!state.lines.length){dragged=false;return;}
    const node=event.target?.closest?.('[data-gallery-lyric-line]');
    const line=node&&state.lines[Number(node.dataset.galleryLyricLine)];
    if(!line)return;
    resetBrowsing();render();
    host.dispatchEvent(new win.CustomEvent('lume-lyric-seek',{bubbles:true,detail:{time:line.startTime}}));
  });
  listen(doc,'visibilitychange',()=>{if(!disposed)applyVisibility();});
  listen(win,'resize',measureSize);
  listen(media,'change',()=>{applyVisibility();layoutDirty=true;render();});
  listen(doc.fonts,'loadingdone',()=>{if(!disposed){clearMeasurements();layoutDirty=true;render();}});
  const observer=win.ResizeObserver?new win.ResizeObserver(measureSize):null;observer?.observe(rail);

  function update(next={}) {
    if(disposed)return;
    const nextPreferences=preferenceSnapshot(preferences,next.preferences);
    if(nextPreferences!==preferences) {
      const theme=resolvePreferenceTheme('gallery',nextPreferences),nextStack=resolveThemeFontStack(theme);
      primaryRgb=rgb(theme.primaryColor);fillRgb=rgb(theme.karaokeFillColor);
      root.style.fontFamily=nextStack;
      boundary.style.color=theme.primaryColor;
      if(nextStack!==fontStack){fontStack=nextStack;clearMeasurements();layoutDirty=true;}
      for(const row of rows.values())paintFillColor(row);
      preferences=nextPreferences;
    }
    const previous=state;
    state={lines:Array.isArray(next.lines)?next.lines:EMPTY_LINES,activeLineIndex:Number.isInteger(next.activeLineIndex)?next.activeLineIndex:-1,
      currentTime:Number.isFinite(next.currentTime)?next.currentTime:0,playing:next.playing===true,visible:next.visible===true,current:next.current??null,
      backgroundScene:next.backgroundScene};
    if(previous&&sameLyrics(previous.lines,state.lines))state.lines=previous.lines;
    if(previous&&(previous.current?.title!==state.current?.title||previous.current?.artist!==state.current?.artist))resetBrowsing();
    if(!previous||previous.lines!==state.lines) {
      resetBrowsing();clearMeasurements();layoutDirty=true;
      for(const row of [...rows.values()])removeRow(row);
      initialized=false;
    }
    const titleText=state.current?.title||'Lume',artistText=state.current?.artist||'';
    if(title.textContent!==titleText)title.textContent=titleText;
    if(artist.textContent!==artistText)artist.textContent=artistText;
    artist.hidden=!artistText;
    applyVisibility();render();
    if(!needsFrame()&&raf!==null){win.cancelAnimationFrame(raf);raf=null;}
  }
  update(initialState);
  return {update,dispose(){
    if(disposed)return;
    disposed=true;visible=false;if(raf!==null)win.cancelAnimationFrame(raf);raf=null;clearTimer();
    observer?.disconnect();background.dispose();removers.splice(0).forEach(remove=>remove());
    for(const row of [...rows.values()])removeRow(row);
    root.getAnimations?.({subtree:true}).forEach(animation=>animation.cancel());
    disc.style.animationPlayState='paused';clearMeasurements();core=null;scroll.reset();
    state=null;preferences=undefined;root.remove();
  }};
}
