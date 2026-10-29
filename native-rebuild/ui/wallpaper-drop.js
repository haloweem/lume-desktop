export function mountWallpaperDrop(root,{isEnabled,onFiles,onError}) {
  let highlighted=null;
  const highlight=zone=>{
    if (highlighted===zone) return;
    highlighted?.classList.toggle("wallpaper-drop-active",false);
    highlighted=zone;
    highlighted?.classList.toggle("wallpaper-drop-active",true);
  };
  const hasFiles=event=>Array.from(event.dataTransfer?.types || []).includes("Files");
  const isActive=()=>isEnabled() && !(root.ownerDocument || root).hidden;
  const zoneFor=event=>isActive() ?
    event.target?.closest?.('[data-wallpaper-drop-zone="local"]') : null;
  const handlers={
    dragover(event) {
      if (!hasFiles(event)) return;
      const claimed=event.defaultPrevented;
      event.preventDefault();
      if (!isActive()) {
        if (!claimed) event.dataTransfer.dropEffect="none";
        highlight(null);
        return;
      }
      const zone=zoneFor(event);
      event.dataTransfer.dropEffect=zone ? "copy" : "none";
      highlight(zone);
    },
    dragleave(event) {
      if (!event.relatedTarget || !event.relatedTarget.closest?.('[data-wallpaper-drop-zone="local"]'))
        highlight(null);
    },
    drop(event) {
      highlight(null);
      if (!hasFiles(event)) return;
      event.preventDefault();
      if (!zoneFor(event)) return;
      const files=Array.from(event.dataTransfer.files);
      if (!files.length) return;
      if (files.length>32) {onError("一次最多导入 32 个壁纸文件");return;}
      onFiles(files);
    },
    dragend() {highlight(null);},
  };
  for (const [name,handler] of Object.entries(handlers)) root.addEventListener(name,handler);
  return ()=>{
    highlight(null);
    for (const [name,handler] of Object.entries(handlers)) root.removeEventListener(name,handler);
  };
}
