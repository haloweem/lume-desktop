// Prepare the first page while the native parent remains hidden. Carousel
// changes made after startup keep their normal entrance animation.
export async function prepareInitialView(root,{remoteTimeoutMs=800}={}) {
  const document = root.ownerDocument;
  const view = document.defaultView;
  const previews = [...root.querySelectorAll(".preview.current")];
  const decode=image=>{
    const source=image.currentSrc||image.src||'';
    if(!/^https:\/\/(static\.lumedesktop\.com|lume-12345-1444884137\.cos\.ap-nanjing\.myqcloud\.com)\//.test(source))return image.decode();
    // An unavailable CDN cover must not keep the entire manager hidden.
    // The visible page owns retries and can finish the image after first show.
    let timer;
    return Promise.race([image.decode().catch(()=>{}),new Promise(resolve=>{
      timer=setTimeout(resolve,remoteTimeoutMs);
    })]).finally(()=>clearTimeout(timer));
  };
  const pending = [...root.querySelectorAll("img")].map(decode);
  const sources = new Set();
  for (const preview of previews) {
    const value = view.getComputedStyle(preview).getPropertyValue("--image").trim();
    const match = /^url\((?:"([^"]*)"|'([^']*)'|([^)]*))\)$/.exec(value);
    const source = match && (match[1] ?? match[2] ?? match[3]).trim();
    if (source && !sources.has(source)) {
      sources.add(source);
      const image = new view.Image();
      image.src = source;
      pending.push(decode(image));
    }
    for (const animation of preview.getAnimations()) animation.finish();
    preview.style.animation = "none";
  }
  await Promise.all([document.fonts.ready, ...pending]);
  // Let the decoded image and final styles reach the renderer before the
  // trusted native ready command permits the top-level window to be shown.
  await new Promise(resolve => view.requestAnimationFrame(resolve));
  await new Promise(resolve => view.requestAnimationFrame(resolve));
}
