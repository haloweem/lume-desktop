import {LiquidGlassSceneComposition,drawText} from './reference/material.js';
import {musicMaterialSceneSource} from './registry.js';

const TEXT_ROOTS='[data-lumenic-liquid-glass-scene], [data-gallery-visualizer]';
const CANVASES='canvas[data-lumenic-liquid-glass-scene-effect], canvas[data-lumenic-liquid-glass-scene-lyrics], canvas[data-visualizer-effect], canvas[data-visualizer-canvas="mineradio-3d"], [data-gallery-artwork] canvas';
const EXCLUDED='[data-music-material], [data-lumenic-liquid-glass-controls], [data-lumenic-liquid-glass-scene-exclude]';
const intersects=(a,b)=>a.right>b.left&&a.left<b.right&&a.bottom>b.top&&a.top<b.bottom;
const intersect=(a,b)=>({left:Math.max(a.left,b.left),top:Math.max(a.top,b.top),
  right:Math.min(a.right,b.right),bottom:Math.min(a.bottom,b.bottom)});
const within=(root,selector)=>[...(root.matches?.(selector)?[root]:[]),...root.querySelectorAll(selector)];

// Scene leaves retain the historical text/opacity/overflow painter. This is not
// a screenshot engine: CSS masks, filters and rotated text remain its limitations.
export function createMusicMaterialScene(document) {
  const canvas=document.createElement('canvas');
  let context=canvas.getContext('2d',{alpha:true});
  const compositions=new Map(),frameBuffers=new Map();
  const compositionFor=doc=>{
    if(!compositions.has(doc))compositions.set(doc,new LiquidGlassSceneComposition());
    return compositions.get(doc);
  };

  function capture(surface,root,background) {
    if(!context)throw new Error('MUSIC_MATERIAL_SCENE_CANVAS_UNAVAILABLE');
    const win=document.defaultView,rect=surface.getBoundingClientRect();
    const bounds={left:Math.max(0,rect.left-96),top:Math.max(0,rect.top-96),
      right:Math.min(win.innerWidth,rect.right+96),bottom:Math.min(win.innerHeight,rect.bottom+96)};
    const scale=Math.max(1,win.devicePixelRatio||1);
    const width=Math.max(1,Math.round((bounds.right-bounds.left)*scale));
    const height=Math.max(1,Math.round((bounds.bottom-bounds.top)*scale));
    if(canvas.width!==width)canvas.width=width;
    if(canvas.height!==height)canvas.height=height;
    context.setTransform(1,0,0,1,0,0);
    context.clearRect(0,0,width,height);
    context.setTransform(width/Math.max(1,bounds.right-bounds.left),0,0,
      height/Math.max(1,bounds.bottom-bounds.top),0,0);
    context.translate(-bounds.left,-bounds.top);
    const stats={iframes:0,canvases:0,text:0,issues:[],needsPolling:false};
    const visited=new Set(),visitedFrames=new Set();
    const issue=code=>{if(!stats.issues.includes(code))stats.issues.push(code);};
    // The compositor has already cropped/crossfaded media. Never decode it twice
    // or sample mediaSource (which would lose the outgoing image during a fade).
    if(background?.width>0&&background.height>0){
      const r=background.getBoundingClientRect();
      if(r.width>0&&r.height>0)context.drawImage(background,r.left,r.top,r.width,r.height);
    }

    function paintRealm(target,realmRoot,clip,depth=0) {
      const doc=realmRoot.ownerDocument??realmRoot;
      const realm=doc.defaultView;
      if(!realm||depth>8){issue('frame-depth');return false;}
      visited.add(doc);
      const leaves=[];
      const excluded=element=>surface.contains(element)||!!element.closest(EXCLUDED);
      for(const source of within(realmRoot,CANVASES)){
        if(excluded(source)||source===background)continue;
        const r=source.getBoundingClientRect();
        if(!intersects(r,clip)||!source.width||!source.height)continue;
        leaves.push({element:source,paint:ctx=>{
          stats.needsPolling=true;
          const provider=musicMaterialSceneSource(source);
          if(source.dataset.visualizerCanvas==='mineradio-3d'&&!provider){
            // Three's current stage discards its backbuffer after presentation.
            // Sampling it on a later RAF can silently yield an empty texture.
            const gl=source.getContext('webgl2')||source.getContext('webgl');
            if(gl&&!gl.getContextAttributes()?.preserveDrawingBuffer){
              issue('3d-snapshot-provider-required');return false;
            }
          }
          try{
            const result=provider?.refresh();
            if(result?.then){issue('async-scene-provider');return false;}
            ctx.drawImage(source,r.left,r.top,r.width,r.height);
            stats.canvases++;return true;
          }catch{issue('canvas-capture-failed');return false;}
        }});
      }
      const textElements=new Set();
      for(const textRoot of within(realmRoot,TEXT_ROOTS)){
        for(const element of [textRoot,...textRoot.querySelectorAll('*')]){
          if(excluded(element)||textElements.has(element))continue;
          if(![...element.childNodes].some(node=>node.nodeType===3&&node.textContent?.trim()))continue;
          const r=element.getBoundingClientRect();
          if(!intersects(r,clip))continue;
          textElements.add(element);
          leaves.push({element,paint:ctx=>{
            stats.needsPolling=true;
            if(drawText(ctx,element,r)){stats.text++;return true;}
            return false;
          }});
        }
      }
      for(const frame of realmRoot.querySelectorAll('iframe')){
        if(excluded(frame))continue;
        const r=frame.getBoundingClientRect();
        if(!intersects(r,clip)||!r.width||!r.height)continue;
        let child;
        try{child=frame.contentDocument;if(!child?.documentElement)throw new Error('inaccessible');}
        catch{issue('inaccessible-iframe');continue;}
        // Translation and axis-aligned CSS scaling cover the production stage.
        // Reject rotated/skewed frames explicitly instead of painting a bad crop.
        let supported=true;
        for(let node=frame;node;node=node.parentElement){
          const transform=realm.getComputedStyle(node).transform;
          if(transform!=='none'){
            const matrix=new realm.DOMMatrixReadOnly(transform);
            if(!matrix.is2D||matrix.b!==0||matrix.c!==0||matrix.a<=0||matrix.d<=0)supported=false;
          }
        }
        if(!supported){issue('unsupported-iframe-transform');continue;}
        const sx=r.width/Math.max(1,frame.offsetWidth),sy=r.height/Math.max(1,frame.offsetHeight);
        const left=r.left+frame.clientLeft*sx,top=r.top+frame.clientTop*sy;
        const frameClip=intersect(clip,{left,top,right:left+frame.clientWidth*sx,bottom:top+frame.clientHeight*sy});
        if(frameClip.right<=frameClip.left||frameClip.bottom<=frameClip.top)continue;
        leaves.push({element:frame,paint:ctx=>{
          stats.iframes++;stats.needsPolling=true;
          visitedFrames.add(frame);
          const local={left:(frameClip.left-left)/sx,top:(frameClip.top-top)/sy,
            right:(frameClip.right-left)/sx,bottom:(frameClip.bottom-top)/sy};
          let buffer=frameBuffers.get(frame);
          if(!buffer){
            const canvas=document.createElement('canvas');
            buffer={canvas,context:canvas.getContext('2d',{alpha:true})};
            frameBuffers.set(frame,buffer);
          }
          const output=buffer.context;
          if(!output){issue('iframe-canvas-unavailable');return false;}
          if(buffer.canvas.width!==width)buffer.canvas.width=width;
          if(buffer.canvas.height!==height)buffer.canvas.height=height;
          output.setTransform(1,0,0,1,0,0);output.clearRect(0,0,width,height);
          output.setTransform(ctx.getTransform());
          output.save();
          output.beginPath();output.rect(frameClip.left,frameClip.top,frameClip.right-frameClip.left,frameClip.bottom-frameClip.top);output.clip();
          output.translate(left,top);output.scale(sx,sy);
          const painted=paintRealm(output,child.documentElement,local,depth+1);
          output.restore();
          // Composite this document once with the iframe's own opacity/blend.
          // Buffer dimensions are the sampled bar strip, not the iframe viewport.
          ctx.save();
          ctx.setTransform(1,0,0,1,0,0);
          ctx.drawImage(buffer.canvas,0,0);
          ctx.restore();return painted;
        }});
      }
      return compositionFor(doc).paint(target,leaves,clip);
    }
    const hasContent=paintRealm(context,root,bounds);
    context.setTransform(1,0,0,1,0,0);
    // Fail visibly on a tainted scene, instead of letting WebGL silently drop it.
    try{context.getImageData(0,0,1,1);}
    catch(error){canvas.width=1;throw new Error('MUSIC_MATERIAL_TAINTED_SCENE',{cause:error});}
    for(const doc of compositions.keys())if(!visited.has(doc))compositions.delete(doc);
    for(const [frame,buffer] of frameBuffers)if(!visitedFrames.has(frame)){
      buffer.canvas.width=buffer.canvas.height=1;frameBuffers.delete(frame);
    }
    return {canvas,bounds,hasContent,...stats};
  }
  return {
    capture,
    dispose(){
      canvas.width=canvas.height=1;compositions.clear();context=null;
      for(const {canvas} of frameBuffers.values())canvas.width=canvas.height=1;
      frameBuffers.clear();
    },
  };
}
