import {validWallpaperMediaUrl} from "./bridge.js";
import {t} from "./localization.js";

export const wallpaperPageSize = 36;

export const REFERENCE_HOME_WALLPAPER_ORDER = Object.freeze([
  "forest-2",
  "floating-in-space-by-visualdon",
  "gargantua-bgm",
  "the-dock",
  "mountain-landscape-and-house-art-wallpaper",
  "wavy-grass-4k60fps",
  "atlantic-ocean-4k",
  "girl-under-rain-4k",
  "ender-miles-morales-snow-time-wuhd-4k",
  "eb-sunflower-girl",
]);

const STATIC_ORIGIN = "https://static.lumedesktop.com";
const COS_ORIGIN = "https://lume-12345-1444884137.cos.ap-nanjing.myqcloud.com";
const MAX_WALLPAPER_ASSET_STATES = 256;
export const WALLPAPER_INITIAL_PRELOAD_COUNT = 8;
const wallpaperAssetStates = new Map();

function wallpaperAssetUrl(value) {
  if (!validWallpaperMediaUrl(value)) return "";
  try {
    return new URL(value).href;
  } catch {
    return "";
  }
}

function wallpaperAssetKey(value) {
  const href=wallpaperAssetUrl(value);
  if (!href) return "";
  const url=new URL(href);
  if ([STATIC_ORIGIN,COS_ORIGIN].includes(url.origin))
    return `${STATIC_ORIGIN}${url.pathname}${url.search}`;
  return href;
}

function wallpaperAssetBaseCandidates(value) {
  const href=wallpaperAssetUrl(value);
  if (!href) return [];
  const url=new URL(href);
  if (![STATIC_ORIGIN,COS_ORIGIN].includes(url.origin)) return [href];
  // CDN only: reading the COS bucket directly is billed as COS egress.
  return [`${STATIC_ORIGIN}${url.pathname}${url.search}`];
}

function wallpaperAssetState(key) {
  let state = wallpaperAssetStates.get(key);
  if (state) return state;
  if (wallpaperAssetStates.size >= MAX_WALLPAPER_ASSET_STATES)
    wallpaperAssetStates.delete(wallpaperAssetStates.keys().next().value);
  state = {preferred:"",failed:new Set()};
  wallpaperAssetStates.set(key, state);
  return state;
}

export function wallpaperAssetCandidates(value) {
  const key = wallpaperAssetKey(value);
  if (!key) return [];
  const state = wallpaperAssetStates.get(key);
  // A transient CDN failure must not blacklist a wallpaper for the session.
  // Individual surfaces own their bounded retries; reopening can try again.
  const candidates = wallpaperAssetBaseCandidates(key);
  if (state?.preferred && candidates.includes(state.preferred))
    return [state.preferred, ...candidates.filter(candidate => candidate !== state.preferred)];
  return candidates;
}

export function wallpaperAssetPreferredUrl(value) {
  return wallpaperAssetCandidates(value)[0] || "";
}

export function rememberWallpaperAssetSuccess(source, resolved) {
  const key = wallpaperAssetKey(source);
  const candidate = wallpaperAssetUrl(resolved);
  if (!key || !wallpaperAssetBaseCandidates(key).includes(candidate)) return;
  const state = wallpaperAssetState(key);
  state.preferred = candidate;
  state.failed.delete(candidate);
}

export function rememberWallpaperAssetFailure(source, resolved) {
  const key = wallpaperAssetKey(source);
  const candidate = wallpaperAssetUrl(resolved);
  if (!key || !wallpaperAssetBaseCandidates(key).includes(candidate)) return;
  const state = wallpaperAssetState(key);
  state.failed.add(candidate);
  if (state.preferred === candidate) state.preferred = "";
}

export function wallpaperAssetFallbackUrl(value) {
  return wallpaperAssetCandidates(value)[1] || "";
}

export function wallpaperHeroImageUrl(item) {
  if (!item) return "";
  const explicit=item.heroThumbnail || item.heroThumbnailUrl;
  if (validWallpaperMediaUrl(explicit)) return explicit;
  const thumbnail=validWallpaperMediaUrl(item.thumbnail) ? item.thumbnail : "";
  if (thumbnail) {
    const url=new URL(thumbnail);
    if (url.pathname.includes("/wallpapers/official/thumbnails/")) {
      url.pathname=url.pathname.replace("/wallpapers/official/thumbnails/","/wallpapers/official/hero-thumbnails/");
      return url.href;
    }
    return thumbnail;
  }
  return item.kind==="image" && validWallpaperMediaUrl(item.url) ? item.url : "";
}

export function wallpaperThumbnailUrl(item) {
  if (!item) return "";
  if (validWallpaperMediaUrl(item.thumbnail)) return item.thumbnail;
  return item.kind==="image" && validWallpaperMediaUrl(item.url) ? item.url : "";
}

// The home carousel is backed by the same official catalog as the library.
// Keep the official carousel order, then fill absent entries from the current
// directory. A static-only directory remains a valid offline fallback.
export function wallpaperHomeItems(state) {
  const catalog = Array.isArray(state?.wallpaper?.catalog) ? state.wallpaper.catalog : [];
  const videos = catalog.filter(item => item.kind === "video");
  const candidates=videos.length ? videos : catalog;
  const byId=new Map(candidates.map(item=>[item.id,item]));
  const selected=[];
  const selectedIds=new Set();
  const addById=id=>{
    const item=byId.get(id);
    if (!item || selectedIds.has(item.id)) return false;
    selected.push(item);
    selectedIds.add(item.id);
    return selected.length===10;
  };
  for (const id of REFERENCE_HOME_WALLPAPER_ORDER)
    if (addById(id)) return selected;
  const ranked=candidates.filter(item=>Number.isInteger(item.homeOrder)).sort((a,b)=>a.homeOrder-b.homeOrder);
  for (const item of ranked)
    if (addById(item.id)) return selected;
  for (const item of candidates)
    if (addById(item.id)) return selected;
  return selected;
}

export function wallpaperPreloadTargets(state) {
  const targets=[];
  const seen=new Set();
  const add=(item,kind,source)=>{
    if (!item || !source) return;
    const url=wallpaperAssetPreferredUrl(source);
    if (!url) return;
    const key=`${kind}:${source}`;
    if (seen.has(key)) return;
    seen.add(key);
    targets.push({kind,url,source});
  };
  if (state?.view==="home") {
    const items=wallpaperHomeItems(state);
    if (items.length) {
      const index=((state.slide % items.length)+items.length)%items.length;
      add(items[index],"hero",wallpaperHeroImageUrl(items[index]));
      if (items.length>1) add(items[(index+1)%items.length],"hero",
        wallpaperHeroImageUrl(items[(index+1)%items.length]));
    }
  } else if (state?.view==="wallpaperOfficial") {
    const collection=wallpaperCollection(state);
    for (const item of collection.items.slice(0,WALLPAPER_INITIAL_PRELOAD_COUNT))
      add(item,"thumbnail",wallpaperThumbnailUrl(item));
  }
  return targets;
}

export function wallpaperCollection(state) {
  const source=state.view === "wallpaperOfficial" ? "official" : "local";
  const filter=state.wallpaperBrowse[source];
  const all=source === "official" ? state.wallpaper.catalog : state.wallpaper.library;
  const query=filter.query.trim().toLocaleLowerCase();
  const filtered=all.filter(item=>(filter.kind === "all" || item.kind === filter.kind) &&
    item.title.toLocaleLowerCase().includes(query));
  const pages=Math.max(1,Math.ceil(filtered.length/wallpaperPageSize));
  const page=Math.min(filter.page,pages);
  return {source,...filter,page,pages,total:filtered.length,
    items:filtered.slice((page-1)*wallpaperPageSize,page*wallpaperPageSize)};
}

export function selectedWallpaper(state) {
  const selection=state.wallpaperSelection;
  if (!selection || !["official","local"].includes(selection.source)) return null;
  const all=selection.source === "official" ? state.wallpaper.catalog : state.wallpaper.library;
  const item=all.find(entry=>entry.id === selection.id);
  if (!item) return null;
  const libraryItem=selection.source === "local" ? item :
    selection.source === "official" ? state.wallpaper.library.find(entry=>entry.id === item.libraryId) : null;
  const officialItem=selection.source==="official" ? item : selection.source==="local" && item.official!==false ?
    state.wallpaper.catalog.find(entry=>entry.libraryId===item.id || entry.id===item.id) : null;
  return {...item,
    thumbnail:item.thumbnail || officialItem?.thumbnail || "",
    heroThumbnail:item.heroThumbnail || officialItem?.heroThumbnail || "",
    source:selection.source,libraryItem:libraryItem || null,
    officialId:officialItem?.id || (item.official===true?item.id:""),
    width:item.width || officialItem?.width || 0,height:item.height || officialItem?.height || 0,
    missing:!!(item.missing || libraryItem?.missing)};
}

export function selectedWallpaperEngagement(state,item=selectedWallpaper(state)) {
  const engagement=state.wallpaperEngagement;
  return item?.officialId && engagement?.id===item.officialId ? engagement : null;
}

export function canLikeWallpaper(state,item=selectedWallpaper(state)) {
  const engagement=selectedWallpaperEngagement(state,item);
  return !!(engagement?.status==="ready" && !engagement.error && engagement.canLike && !engagement.busy &&
    state.wallpaper.status!=="busy" && !["loading","saving","unavailable"].includes(state.wallpaperStatus));
}

export function selectedWallpaperDisplay(state) {
  return state.wallpaper.displays.find(display=>display.id === state.wallpaperDisplay) || null;
}

export function wallpaperPages(page, total) {
  if (total <= 7) return Array.from({length:total},(_,index)=>index+1);
  const chosen=new Set([1,total,page-1,page,page+1]);
  if (page <= 3) [2,3,4,5].forEach(value=>chosen.add(value));
  if (page >= total-2) [total-4,total-3,total-2,total-1].forEach(value=>chosen.add(value));
  const numbers=[...chosen].filter(value=>value>=1 && value<=total).sort((a,b)=>a-b);
  return numbers.flatMap((value,index)=>index && value-numbers[index-1]>1 ? ["…",value] : [value]);
}

export function wallpaperBytes(value) {
  if (!value) return "";
  if (value >= 1024*1024*1024) return `${(value/(1024*1024*1024)).toFixed(1)} GB`;
  if (value >= 1024*1024) return `${(value/(1024*1024)).toFixed(1)} MB`;
  return `${Math.max(1,Math.round(value/1024))} KB`;
}

export function wallpaperNotice(state) {
  const wallpaper=state.wallpaper;
  if (state.wallpaperStatus === "unavailable") return t("宿主未连接，壁纸操作不可用","Host disconnected. Wallpaper operations are unavailable.");
  if (wallpaper.loadStatus === "failed") return t("壁纸库读取失败，请重试","Could not load the wallpaper library. Please try again.");
  if (wallpaper.error) return wallpaperError(wallpaper.error);
  if (wallpaper.loadStatus === "recovered") return t("壁纸库已从备份恢复","Wallpaper library restored from backup.");
  return "";
}

export function wallpaperEngagementError(error) {
  if (!error) return "";
  const code=typeof error==="string" ? error.replace(/^WALLPAPER_/,"") : "";
  switch (code) {
    case "STATS_UNAVAILABLE":
      return t("统计暂不可用，请稍后重试","Statistics are unavailable. Please try again later.");
    case "LIKE_FAILED":
      return t("喜欢操作失败，请稍后重试","Like operation failed. Please try again later.");
    case "AUTH_REQUIRED":
      return t("登录 Lume 后可喜欢壁纸","Sign in to Lume to like wallpapers");
    case "DOWNLOAD_STATS_FAILED":
      return t("壁纸已下载，下载数量记录失败","Wallpaper downloaded, but recording the download count failed.");
    case "DOWNLOAD_STATS_QUOTA":
      return t("壁纸已下载，下载数量记录受服务端每日额度限制",
        "Wallpaper downloaded, but recording the download count is limited by the server's daily quota.");
    default:
      return t("互动信息暂不可用，请稍后重试","Engagement information is unavailable. Please try again later.");
  }
}

export function wallpaperError(error) {
  if (error==="WALLPAPER_CATALOG_CACHED") return "";
  const category=/^(video|image):/.exec(error);
  if (category) return `${category[1]==="video"?t("动态壁纸：","Dynamic wallpapers: "):t("静态壁纸：","Static wallpapers: ")}${wallpaperError(error.slice(category[0].length))}`;
  const messages={
    "WALLPAPER_DOWNLOAD_QUOTA":t("今日下载额度已用尽","Today's download quota is used up"),
    "WALLPAPER_DOWNLOAD_AUTHORIZATION":t("下载额度校验失败，请检查网络后重试","Could not verify the download allowance. Check your connection and retry."),
    "WALLPAPER_CATALOG_OFFLINE":t("暂时无法更新官方目录，正在显示已缓存的壁纸","Cannot update the official catalog. Showing cached wallpapers."),
    "WALLPAPER_CATALOG_CACHE_SAVE":t("官方目录已更新，但本地缓存保存失败","The official catalog was updated, but its local cache could not be saved."),
    "WALLPAPER_PLAYER_EXITED":t("壁纸播放器意外退出，请点击“应用”重试",'The wallpaper player exited unexpectedly. Click "Apply" to try again.'),
    "WALLPAPER_RUNTIME_FAILED":t("壁纸服务异常，请重新启动 Lume","The wallpaper service failed. Restart Lume."),
    "WALLPAPER_BUSY":t("正在处理壁纸，请稍候","Processing wallpaper. Please wait."),
    "WALLPAPER_SAVE_FAILED":t("壁纸设置保存失败，请重试","Could not save wallpaper settings. Please try again."),
    "WALLPAPER_FAVORITES_LOAD":t("本地喜欢记录读取失败，请重试","Could not load local likes. Please try again."),
    "WALLPAPER_FAVORITES_SAVE":t("本地喜欢记录保存失败，请重试","Could not save local likes. Please try again."),
    "WALLPAPER_CATALOG_CONFLICT":t("官方壁纸信息已更改，请刷新目录后重试","Official wallpaper information has changed. Refresh the catalog and try again."),
    "WALLPAPER_MEDIA_IN_USE":t("这张壁纸正在使用，请先停止壁纸和预览","This wallpaper is in use. Stop wallpaper playback and its preview first."),
    "WALLPAPER_FILE_BUSY":t("壁纸文件正在使用，请停止播放后重试","The wallpaper file is in use. Stop playback and try again."),
    "WALLPAPER_DISPLAY_MISSING":t("所选显示器已断开，请重新选择显示器","The selected display was disconnected. Select a display again."),
    "WALLPAPER_PLAYER_MISSING":t("播放器不可用，请检查安装文件","Player unavailable. Check the installation files."),
    "MPV_EXECUTABLE_NOT_FOUND":t("播放器不可用，请检查安装文件","Player unavailable. Check the installation files."),
    "WALLPAPER_FILE_MISSING":t("文件已丢失，请重新导入或下载","File missing. Import or download it again."),
    "WALLPAPER_IMPORT_MISSING":t("文件已丢失，请重新选择文件","File missing. Select the file again."),
    "WALLPAPER_NOT_PLAYING":t("所选显示器尚未播放壁纸","No wallpaper is playing on the selected display."),
    "WALLPAPER_IMPORT_FORMAT":t("不支持此壁纸格式","This wallpaper format is not supported."),
    "WALLPAPER_IMPORT_LIMIT":t("一次最多导入 32 个壁纸文件","You can import up to 32 wallpaper files at a time."),
    "WALLPAPER_CANCELLED":t("操作已取消","Operation cancelled."),
    "WALLPAPER_MEDIA_SERVER":t("壁纸预览暂时不可用，请重试","Wallpaper preview is temporarily unavailable. Please try again."),
    "WALLPAPER_MEDIA_CAPABILITY":t("壁纸媒体暂时不可用，请重新应用","Wallpaper media is temporarily unavailable. Apply it again."),
    "WALLPAPER_SYSTEM_EFFECTS_DISABLED":t("此验证环境未启用桌面壁纸","Desktop wallpapers are disabled in this validation environment."),
    "player-unavailable":t("播放器不可用，请检查安装文件","Player unavailable. Check the installation files."),
    "missing-player":t("播放器不可用，请检查安装文件","Player unavailable. Check the installation files."),
    "player-start":t("壁纸启动失败，请重试","Could not start the wallpaper. Please try again."),
    "missing":t("文件已丢失，请重新导入","File missing. Please import it again."),
    "missing-file":t("文件已丢失，请重新导入","File missing. Please import it again."),
    "download":t("下载失败，请重试","Download failed. Please try again."),
    "network":t("网络连接失败，请重试","Network connection failed. Please try again."),
    "save":t("壁纸设置保存失败，请重试","Could not save wallpaper settings. Please try again."),
    "load":t("壁纸库读取失败，请重试","Could not load the wallpaper library. Please try again."),
    "busy":t("正在处理壁纸，请稍候","Processing wallpaper. Please wait."),
    "unsupported":t("不支持此壁纸格式","This wallpaper format is not supported."),
    "import":t("导入失败，请检查文件后重试","Import failed. Check the file and try again."),
    "cancelled":t("下载已取消","Download cancelled."),
  };
  const code=error?.split(":",1)[0];
  if (messages[code]) return messages[code];
  if (/^WALLPAPER_HTTP_/.test(error)) return t("官方壁纸暂时无法访问，请稍后重试","Official wallpapers are temporarily unavailable. Please try again later.");
  if (/^WALLPAPER_(PROTECTION_|PROTECTED_FILE_)/.test(error)) return t("壁纸文件读取失败，请重新下载","Could not read the wallpaper file. Download it again.");
  if (/^WALLPAPER_IMPORT_/.test(error)) return t("导入失败，请检查文件后重试","Import failed. Check the file and try again.");
  if (/^WALLPAPER_LIBRARY_/.test(error)) return t("壁纸库操作失败，请重试","Wallpaper library operation failed. Please try again.");
  if (/^WALLPAPER_(CATALOG_|DOWNLOAD_|CONTENT_)/.test(error)) return t("官方壁纸读取失败，请刷新后重试","Could not load official wallpapers. Refresh and try again.");
  if (/^MPV_/.test(error)) return t("壁纸播放失败，请重新应用壁纸","Wallpaper playback failed. Apply the wallpaper again.");
  return messages[error] || (error ? t(`壁纸操作失败：${error}`,`Wallpaper operation failed: ${error}`) : "");
}

// Playback intent belongs to the page. This controller owns only the current
// media source and decoder, which are released whenever the surface is hidden.
export function createWallpaperPreviewController({getVideo,getImage=()=>null,onChange=()=>{},onError=()=>{},timeoutMs=12000}) {
  let active=null,generation=0;
  const visible=surface=>surface.nativeVisible && surface.documentVisible && surface.view === "wallpaperDetail";
  function stop() {
    generation++;
    if (!active) return;
    const {element,kind,previousSrc,previousAssetSource,cancel}=active;
    active=null;
    cancel?.();
    if (kind==="video") {
      if(element.dataset)delete element.dataset.ready;
      element.pause();
      element.removeAttribute("src");
      element.load();
    } else {
      element.removeAttribute("src");
      if (previousSrc) element.setAttribute("src",previousSrc);
      if (element.dataset) {
        if (previousAssetSource) element.dataset.wallpaperAssetSource=previousAssetSource;
        else delete element.dataset.wallpaperAssetSource;
      }
    }
    onChange(false);
  }
  async function decodeImage(image) {
    if (typeof image.decode==="function") {
      await image.decode();
      return;
    }
    if (image.complete) return;
    await new Promise((resolve,reject)=>{
      const loaded=()=>{cleanup();resolve();};
      const failed=()=>{cleanup();reject(Error("image decode failed"));};
      const cleanup=()=>{
        image.removeEventListener?.("load",loaded);
        image.removeEventListener?.("error",failed);
      };
      image.addEventListener?.("load",loaded,{once:true});
      image.addEventListener?.("error",failed,{once:true});
    });
  }
  return {
    get playing() { return active?.playing===true; },
    async play(item,surface,{reportError=true}={}) {
      if (!visible(surface) || !["video","image"].includes(item?.kind) || item.missing ||
        !validWallpaperMediaUrl(item.url)) return false;
      stop();
      const kind=item.kind,element=kind==="video" ? getVideo() : getImage();
      if (!element) return false;
      const token=++generation;
      active={element,kind,id:item.id,previousSrc:element.getAttribute?.("src") || "",
        previousAssetSource:element.dataset?.wallpaperAssetSource || ""};
      if (kind==="image" && element.dataset) {
        if (item.assetSource) element.dataset.wallpaperAssetSource=item.assetSource;
        else delete element.dataset.wallpaperAssetSource;
      }
      element.src=item.url;
      if (kind==="video") {
        element.muted=true;
      }
      let timer;
      try {
        const stopped=new Promise((_,reject)=>{
          active.cancel=()=>reject(Error("preview stopped"));
          // Video play() resolves once buffering permits playback. A slow CDN
          // must not turn that pending playback into a permanent preview error.
          // Navigation still cancels immediately, and media errors reject play().
          if(kind!=="video")timer=setTimeout(()=>reject(Error("preview timeout")),timeoutMs);
        });
        await Promise.race([kind==="video"?element.play():decodeImage(element),stopped]);
        if(token!==generation || active?.element!==element)return false;
        active.playing=true;
        if(kind==="video"&&element.dataset)element.dataset.ready="true";
        onChange(true);
        return true;
      } catch {
        if (token===generation) {
          stop();
          if (reportError) onError(t("预览播放失败，请检查网络或媒体文件","Preview playback failed. Check the network or media file."));
        }
        return false;
      } finally {
        clearTimeout(timer);
        if(token===generation&&active)active.cancel=null;
      }
    },
    update(surface) {
      if (active && (!visible(surface) || active.id !== surface.id)) stop();
    },
    stop,
    dispose:stop,
  };
}
