import {LiquidGlassRenderer,resolveLiquidGlassRenderScale} from './music-material/reference/material.js';
import {createMusicMaterialScene} from './music-material/scene.js';
export {registerMusicMaterialSceneSource} from './music-material/registry.js';

const mounts=new WeakMap();

/**
 * Mount on the WHOLE player bar. Caller owns position, size, transparent chrome,
 * and controls above z-index 0. root includes the stage and parent effect layers.
 * getScene returns backgroundRenderer.getScene(); this module borrows only canvas.
 */
export function mountMusicMaterial(surface,{getScene=()=>null,root,visible=true,onError,onIssue}={}) {
  if(!surface?.ownerDocument||typeof getScene!=='function')throw new TypeError('Material surface and getScene required');
  mounts.get(surface)?.dispose();
  const document=surface.ownerDocument,win=document.defaultView;
  root??=surface.closest('[data-music-root]')??document.documentElement;
  if(!root?.querySelectorAll||root.ownerDocument!==document)throw new TypeError('root must belong to surface document');
  const canvas=document.createElement('canvas');
  canvas.dataset.musicMaterial='';
  canvas.setAttribute('aria-hidden','true');
  Object.assign(canvas.style,{position:'absolute',inset:'0',width:'100%',height:'100%',
    pointerEvents:'none',borderRadius:'inherit',zIndex:'0'});
  surface.prepend(canvas);
  let requested=visible===true,disposed=false,lost=false,failed=false;
  let renderer=null,sceneCapture=null,frame=null,background=null,frames=0;
  let state='initializing',sceneState={iframes:0,canvases:0,text:0,issues:[],needsPolling:false};
  let lastIssues='';
  const motion=win.matchMedia('(prefers-reduced-motion: reduce)');
  const setState=next=>{
    state=next;
    if(canvas.dataset.musicMaterialState!==next)canvas.dataset.musicMaterialState=next;
  };
  const immersiveBar=()=>surface.classList.contains('MusicPlayerBar')&&root.classList.contains('is-immersive');
  const immersiveHidden=()=>immersiveBar()&&!root.classList.contains('is-immersive-revealed');
  const active=()=>{
    if(disposed||!requested||document.hidden||!surface.isConnected)return false;
    for(let node=surface;node;node=node.parentElement){
      const css=win.getComputedStyle(node);
      if(css.display==='none')return false;
      // Hover changes the reveal class before the opacity transition leaves 0.
      // Neither end of that transition means the player page was hidden.
      // Ignore only this bar's presentation; still release for hidden ancestors.
      if(node!==surface||!immersiveBar())
        if(css.visibility==='hidden'||Number(css.opacity)===0)return false;
    }
    return surface.getClientRects().length>0;
  };
  function cancel(){if(frame!==null){win.cancelAnimationFrame(frame);frame=null;}}
  function release(){
    cancel();renderer?.dispose();renderer=null;
    sceneCapture?.dispose();sceneCapture=null;
    canvas.width=canvas.height=1;
  }
  function bindBackground(next){
    if(background===next)return;
    background?.removeEventListener('lume-glass-media-frame',schedule);
    background=next;
    background?.addEventListener('lume-glass-media-frame',schedule);
  }
  function draw(){
    frame=null;
    if(!active()){release();bindBackground(null);setState('hidden');return;}
    if(lost||failed)return;
    try{
      const descriptor=getScene();
      bindBackground(descriptor?.canvas??null);
      if(!renderer){
        renderer=LiquidGlassRenderer.create(canvas);
        if(!renderer)throw new Error('MUSIC_MATERIAL_WEBGL2_UNAVAILABLE');
      }
      sceneCapture??=createMusicMaterialScene(document);
      const rect=surface.getBoundingClientRect();
      if(rect.width<=0||rect.height<=0)return;
      const scene=sceneCapture.capture(surface,root,background);
      const {canvas:sceneCanvas,bounds,hasContent,...diagnostics}=scene;
      sceneState=diagnostics;
      renderer.render({
        width:rect.width,height:rect.height,dpr:resolveLiquidGlassRenderScale(win.devicePixelRatio),
        viewportWidth:win.innerWidth,viewportHeight:win.innerHeight,viewportX:rect.left,viewportY:rect.top,
        expanded:true,reducedMotion:motion.matches,media:background,
        mediaRatio:background?.width/Math.max(1,background?.height)||1,scene:sceneCanvas,sceneBounds:bounds,
      });
      const gl=canvas.getContext('webgl2');
      if(gl.isContextLost()){lost=true;release();setState('context-lost');return;}
      // WebGL draw/upload errors do not throw; never clear fallback chrome for a failed frame.
      if(gl.getError()!==gl.NO_ERROR)throw new Error('MUSIC_MATERIAL_WEBGL_RENDER_FAILED');
      frames++;setState('active');
      const complete=String(scene.issues.length===0);
      if(canvas.dataset.musicMaterialSceneComplete!==complete)canvas.dataset.musicMaterialSceneComplete=complete;
      const key=scene.issues.join(',');
      if(key!==lastIssues){lastIssues=key;onIssue?.([...scene.issues]);}
      if(scene.needsPolling&&!immersiveHidden())schedule();
    }catch(error){
      failed=true;release();setState('fallback');onError?.(error);
    }
  }
  function schedule(){
    if(disposed)return;
    if(!active()){release();bindBackground(null);setState('hidden');return;}
    if(!failed&&!lost&&frame===null)frame=win.requestAnimationFrame(draw);
  }
  const contextLost=event=>{
    event.preventDefault();lost=true;release();setState('context-lost');
  };
  const contextRestored=()=>{
    if(disposed)return;
    lost=false;failed=false;setState('initializing');schedule();
  };
  const resize=new win.ResizeObserver(schedule);resize.observe(surface);
  const observer=new win.MutationObserver(records=>{
    if(records.some(record=>record.target===surface||!surface.contains(record.target)))schedule();
  });
  function observe(){
    observer.disconnect();
    observer.observe(root,{subtree:true,childList:true,attributes:true,characterData:true,
      attributeFilter:['style','class','hidden']});
  }
  observe();
  const sceneLoaded=event=>{
    if(event.target?.tagName==='IFRAME' && root.contains(event.target))schedule();
  };
  document.addEventListener('load',sceneLoaded,true);
  document.addEventListener('visibilitychange',schedule);
  win.addEventListener('resize',schedule);
  // Capture scroll in any stage ancestor, including clipped lyric rails.
  document.addEventListener('scroll',schedule,true);
  motion.addEventListener('change',schedule);
  canvas.addEventListener('webglcontextlost',contextLost);
  canvas.addEventListener('webglcontextrestored',contextRestored);
  const api={
    canvas,
    update(next={}){
      if(disposed)return;
      if(Object.hasOwn(next,'getScene')){
        if(typeof next.getScene!=='function')throw new TypeError('getScene must be a function');
        getScene=next.getScene;
      }
      if(Object.hasOwn(next,'root')){
        if(!next.root?.querySelectorAll||next.root.ownerDocument!==document)throw new TypeError('root must belong to surface document');
        root=next.root;observe();
      }
      if(Object.hasOwn(next,'visible'))requested=next.visible===true;
      schedule();
    },
    snapshot:()=>({state,frames,pendingFrame:frame!==null,scene:{...sceneState,issues:[...sceneState.issues]}}),
    dispose(){
      if(disposed)return;
      disposed=true;release();bindBackground(null);
      resize.disconnect();observer.disconnect();
      document.removeEventListener('load',sceneLoaded,true);
      document.removeEventListener('visibilitychange',schedule);
      document.removeEventListener('scroll',schedule,true);
      win.removeEventListener('resize',schedule);
      motion.removeEventListener('change',schedule);
      canvas.removeEventListener('webglcontextlost',contextLost);
      canvas.removeEventListener('webglcontextrestored',contextRestored);
      setState('disposed');canvas.remove();
      if(mounts.get(surface)===api)mounts.delete(surface);
    },
  };
  mounts.set(surface,api);setState('initializing');schedule();
  return api;
}
