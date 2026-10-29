import {validMusicBackgroundMedia} from '../music-background.js';

const DEFAULT_IMAGE='assets/music/forest.webp';

// Original galleryLyricsModel gives custom media precedence over cover art.
// Video artwork borrows the background decoder; these small canvases own only presentation pixels.
export function mountGalleryBackground({root,backdrop,artwork,sleeveImage,discImage}) {
  const document=root.ownerDocument,window=document.defaultView;
  const images=[sleeveImage,discImage],canvases=images.map(image=>{
    const canvas=document.createElement('canvas');canvas.hidden=true;canvas.setAttribute('aria-hidden','true');
    image.after(canvas);return canvas;
  });
  let scene=null,source=null,visible=false,disposed=false;
  function draw() {
    if(disposed || !visible || !source || scene?.media?.kind!=='video')return;
    const width=source.videoWidth||source.naturalWidth||source.width;
    const height=source.videoHeight||source.naturalHeight||source.height;
    if(!width||!height)return;
    for(const canvas of canvases) {
      const bounds=canvas.getBoundingClientRect(),dpr=window.devicePixelRatio||1;
      if(!bounds.width||!bounds.height)continue;
      const w=Math.max(1,Math.round(bounds.width*dpr)),h=Math.max(1,Math.round(bounds.height*dpr));
      if(canvas.width!==w)canvas.width=w;if(canvas.height!==h)canvas.height=h;
      const scale=Math.max(w/width,h/height);
      try{canvas.getContext('2d')?.drawImage(source,(w-width*scale)/2,(h-height*scale)/2,width*scale,height*scale);}catch{}
    }
  }
  const observer=window.ResizeObserver?new window.ResizeObserver(draw):null;
  canvases.forEach(canvas=>observer?.observe(canvas));
  function update(next,shown) {
    if(disposed)return;
    const nextScene=next?.canvas && (next.media===null || validMusicBackgroundMedia(next.media) ||
      next.media?.id==='default-forest') ? next : null;
    if(scene?.canvas!==nextScene?.canvas) {
      scene?.canvas.removeEventListener('lume-glass-media-frame',draw);
      nextScene?.canvas.addEventListener('lume-glass-media-frame',draw);
    }
    scene=nextScene;source=scene?.mediaSource||null;visible=shown;
    const video=scene?.media?.kind==='video';
    root.classList.toggle('MusicGallery--shared-background',!!scene);
    backdrop.hidden=!!scene;
    if(scene)backdrop.removeAttribute('src');else if(!backdrop.getAttribute('src'))backdrop.src=DEFAULT_IMAGE;
    const url=scene?.media?.kind==='image'?scene.media.url:DEFAULT_IMAGE;
    artwork.dataset.galleryArtworkSource=video?'video':'image';
    images.forEach(image=>{
      image.hidden=video;
      if(video)image.removeAttribute('src');else if(image.getAttribute('src')!==url)image.src=url;
    });
    canvases.forEach(canvas=>{canvas.hidden=!video;});
    if(video)draw();
  }
  return {update,dispose(){
    if(disposed)return;disposed=true;
    scene?.canvas.removeEventListener('lume-glass-media-frame',draw);observer?.disconnect();
    canvases.forEach(canvas=>{canvas.width=canvas.height=1;canvas.remove();});
    images.forEach(image=>image.removeAttribute('src'));backdrop.removeAttribute('src');
    scene=source=null;
  }};
}
