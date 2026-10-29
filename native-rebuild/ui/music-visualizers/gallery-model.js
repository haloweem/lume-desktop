import {createGalleryCore} from './gallery-vendor/monet-core.js';
import {spring} from './gallery-vendor/motion.js';

// DOM-free adaptation of MonetLyricsRail typography with Pendolo-style focus: one spring moves the whole rail.
export {createGalleryCore};
export const GALLERY_SCROLL_RESET_MS=1800;
export const GALLERY_FONT_STACK='"Inter", "Noto Sans CJK SC", "Noto Sans JP", "Source Han Sans SC", "PingFang SC", "Hiragino Sans GB", "Microsoft YaHei", "Helvetica Neue", Arial, sans-serif';
const core=createGalleryCore();
const clamp=(value,min,max)=>Math.min(max,Math.max(min,value));

export function galleryFonts(width) {
  const font=(preferred,min,max)=>Number(clamp(width*preferred,min,max).toFixed(3));
  return {
    lyric:font(.0215,17.28,28.48),
    inactive:font(.0155,14.4,18.88),
    translation:font(.0102,12.48,15.36),
  };
}

export const GALLERY_WINDOW=6;
export const GALLERY_LINE_GAP=12;
// Where the rail is focused, as Pendolo resolves its wheel: the sung line, or
// halfway past the last sung line during a gap, -1 before the first line and
// past the end after the last one has finished.
export function galleryAnchor(lines,activeLineIndex,time) {
  if(!lines.length)return 0;
  const active=Number.isInteger(activeLineIndex)&&lines[activeLineIndex]?activeLineIndex:-1;
  if(active<0)return -1;
  if(time<=core.getLineRenderEndTime(lines[active]))return active;
  return active===lines.length-1?lines.length:active+.5;
}
export function galleryStatus(index,anchor) {
  return index===anchor?'active':index<anchor?'passed':'waiting';
}
// Continuous tone for a line `distance` lines away from the moving focus.
export function galleryToneAt(distance,inactiveScale,passed) {
  const near=clamp(distance,0,1),far=Math.max(0,distance-1);
  const scale=clamp((1-(1-inactiveScale)*near)*Math.pow(.9,far),.68,1);
  const edge=passed?.52:.72;
  const opacity=distance<1?1-(1-edge)*near:clamp(edge-far*(passed?.12:.18),.28,edge);
  const blurPx=Math.min(4,distance<1?.7*near:(passed?1.1:.7)+far*(passed?.7:.9));
  return {opacity,scale,blurPx};
}
// Rail coordinates around `base`: each line's top and centre from measured
// heights, with lines outside [0,count) given `fallback` height.
export function galleryPositions(base,from,to,count,height,fallback,gap=GALLERY_LINE_GAP) {
  const h=i=>i>=0&&i<count?height(i):fallback,tops=new Map();
  let y=0;tops.set(base,0);
  for(let i=base+1;i<=to;i++){y+=h(i-1)+gap;tops.set(i,y);}
  y=0;for(let i=base-1;i>=from;i--){y-=h(i)+gap;tops.set(i,y);}
  const center=i=>tops.get(i)+h(i)/2;
  return {top:i=>tops.get(i),center,at:value=>{
    const lo=Math.floor(value),hi=Math.ceil(value);
    return lo===hi?center(lo):center(lo)+(center(hi)-center(lo))*(value-lo);
  }};
}

export function createGalleryScroll() {
  let anchor=null;
  const sums={wheel:0,touch:0},directions={wheel:0,touch:0};
  return {
    get anchor(){return anchor;},
    begin(fallback,count){anchor=count?Math.round(clamp(fallback,0,count-1)):null;sums.touch=0;directions.touch=0;return anchor;},
    endTouch(){sums.touch=0;directions.touch=0;},
    reset(){anchor=null;sums.wheel=sums.touch=directions.wheel=directions.touch=0;},
    move(kind,delta,fallback,count){
      if(!count||!Number.isFinite(delta))return anchor;
      const direction=Math.sign(delta);
      if(direction&&directions[kind]&&direction!==directions[kind])sums[kind]=0;
      directions[kind]=direction||directions[kind];
      sums[kind]+=delta;
      const steps=clamp(Math.trunc(sums[kind]/(kind==='wheel'?72:52)),-1,1);
      if(steps){sums[kind]=0;anchor=Math.round(clamp((anchor??fallback)+steps,0,count-1));}
      return anchor;
    },
  };
}

export function gallerySweep(token,offsets,time,fontPx) {
  const full=offsets.at(-1)??0,start=token.startTime,end=token.endTime;
  const progress=time<=start?0:time>=end?1:clamp((time-start)/Math.max(.001,end-start),0,1);
  let fillWidth=0;
  if(time>start) {
    if(time>=end)fillWidth=full;
    else {
      const count=Math.min(token.graphemeTimings.length,offsets.length-1);
      if(count) {
        fillWidth=offsets[count]??full;
        for(let i=0;i<count;i++) {
          const timing=token.graphemeTimings[i],from=Math.max(start,timing.startTime),to=Math.max(from,timing.endTime);
          if(time<from){fillWidth=offsets[i];break;}
          if(time<=to){fillWidth=offsets[i]+(offsets[i+1]-offsets[i])*(time-from)/Math.max(.001,to-from);break;}
        }
      } else {
        const index=progress*(offsets.length-1),whole=Math.floor(index);
        fillWidth=(offsets[whole]??0)+((offsets[whole+1]??full)-(offsets[whole]??0))*(index-whole);
      }
    }
  }
  const softness=core.resolveMonetSweepEdgeSoftness(fontPx);
  const sweepEnd=core.resolveMonetSweepEnd(fillWidth,full,softness);
  const mask=`linear-gradient(90deg, rgba(0,0,0,1) 0px, rgba(0,0,0,1) ${Math.max(0,sweepEnd-softness)}px, rgba(0,0,0,.92) ${Math.max(0,sweepEnd-softness*.55)}px, rgba(0,0,0,0) ${Math.max(0,sweepEnd)}px, rgba(0,0,0,0) 100%)`;
  return {fillWidth,sweepEnd,progress,mask};
}

export function galleryGlow(token,time,renderEndTime) {
  if(time<=token.startTime)return 0;
  const rise=Math.max(.001,token.endTime-token.startTime)*1.18,peak=token.startTime+rise;
  const tail=Math.max(renderEndTime,token.endTime+1.05);
  const amount=time<=peak?clamp((time-token.startTime)/rise,0,1):1-clamp((time-peak)/Math.max(.18,tail-peak),0,1);
  return amount*amount*(3-2*amount);
}

export function splitGalleryToken(token,offsets,maxWidth) {
  if((offsets.at(-1)??0)<=maxWidth||!token.graphemeTimings.length)return [{token,offsets}];
  const timings=token.graphemeTimings,chunks=[];
  for(let start=0;start<timings.length;) {
    let end=start+1,lastSpace=-1;
    while(end<timings.length&&offsets[end+1]-offsets[start]<=maxWidth) {
      if(/\s/u.test(timings[end-1].char))lastSpace=end;
      end++;
    }
    if(end<timings.length&&lastSpace>start)end=lastSpace;
    const slice=timings.slice(start,end);
    chunks.push({token:{...token,key:`${token.key}-${start}`,text:slice.map(t=>t.char).join(''),
      startTime:slice[0].startTime,endTime:slice.at(-1).endTime,graphemeTimings:slice,glowToken:token},
      offsets:offsets.slice(start,end+1).map(value=>value-offsets[start])});
    start=end;
  }
  return chunks;
}


// One spring for the whole rail's focus, retargeted with its current velocity.
export function createGalleryFocus(value) {
  let motion=null,start=0,current=value,target=value;
  const sample=now=>{
    if(!motion)return {value:current,velocity:0,done:true};
    const t=Math.max(0,now-start),step=motion.next(t);
    return {value:step.value,velocity:step.done?0:motion.velocity(t),done:step.done};
  };
  return {
    get target(){return target;},
    sample(now){const s=sample(now);current=s.value;if(s.done){motion=null;current=target;}return s;},
    to(next,now,snap=false){
      if(next===target&&!snap)return;
      const s=sample(now);current=s.value;target=next;
      if(snap||Math.abs(next-current)>6){motion=null;current=next;return;}
      motion=spring({keyframes:[current,next],stiffness:142,damping:28,mass:.82,velocity:s.velocity});start=now;
    },
    get moving(){return !!motion;},
  };
}
