import { escapeHTML as h, back, toggle } from "./dom.js";
import { modules, widgets, localizedItem } from "./catalog.js";
import { t } from "./localization.js";
import { wallpaperCollection, selectedWallpaper,
  wallpaperPages, wallpaperPageSize, wallpaperBytes, wallpaperNotice, wallpaperError, wallpaperEngagementError,
  wallpaperAssetFallbackUrl, wallpaperAssetPreferredUrl, wallpaperHomeItems,
  wallpaperHeroImageUrl, wallpaperThumbnailUrl, selectedWallpaperEngagement, canLikeWallpaper } from "./wallpapers.js";
import { searchCatalog, taskbarDisplayedPreferences, organizerDisplayedEnabled } from "./state.js";
import { organizerStatus } from "./organizer.js";
// The first home paint must not wait for the native catalog snapshot. The last
// hero is remembered as a small URL record and rendered from the WebView disk
// cache until the real state arrives: no extra request, no extra memory.
const HOME_HERO_KEY = "lume.home.hero";
function readHomeHero() {
  try {
    const raw = globalThis.localStorage?.getItem(HOME_HERO_KEY) || "";
    if (!raw) return null;
    const value = JSON.parse(raw);
    return value && typeof value === "object" && typeof value.url === "string" && value.url &&
      typeof value.id === "string" ? value : null;
  } catch { return null; }
}
function writeHomeHero(value) {
  try {
    const raw = JSON.stringify(value);
    if (globalThis.localStorage?.getItem(HOME_HERO_KEY) === raw) return;
    globalThis.localStorage?.setItem(HOME_HERO_KEY, raw);
  } catch { /* optional storage */ }
}
function homeHeroRecord(item, source, url, tone) {
  return { id: item.id, title: item.title || "", width: item.width || 0, height: item.height || 0,
    kind: item.kind || "video", source: source || "", url, toneA: tone.toneA, toneB: tone.toneB };
}
export function toolbar(active, query = "", kind = "video") {
  const views = [
    "home",
    "wallpaperLocal",
    "wallpaperOfficial",
    "desktopBeauty",
  ];
  const wallpaperView=["wallpaperOfficial","wallpaperLocal"].includes(active);
  const kindOptions=active==="wallpaperOfficial" ? [["video",t("动态壁纸","Dynamic wallpapers")],["image",t("静态壁纸","Static wallpapers")]] :
    [["all",t("全部壁纸","All wallpapers")],["video",t("动态壁纸","Dynamic wallpapers")],["image",t("静态壁纸","Static wallpapers")]];
  const kindValue=kindOptions.some(([value])=>value===kind) ? kind : kindOptions[0][0];
  const filter=wallpaperView ? wallpaperSelect("wallpaper-kind",kindValue,kindOptions,t("壁纸类型","Wallpaper type")) : "";
  const labels=[t("首页","Home"),t("我的壁纸","My wallpapers"),t("官方壁纸","Official wallpapers"),t("桌面美化","Desktop styles")];
  return `<div class="ModuleToolbar"><div class="toolbar-side left">${filter}</div><nav class="HomeModuleTabs" aria-label="${t("模块导航","Module navigation")}"><span class="HomeModuleHighlight" style="transform:translateX(${views.indexOf(active) * 100}%)"></span>${views.map((view, i) => `<button class="module-tab${view === active ? " active" : ""}" data-view="${view}" ${view === active ? 'aria-current="page"' : ""}>${labels[i]}</button>`).join("")}</nav><div class="toolbar-side right">${searchBox(query)}</div></div>`;
}
export function searchBox(query = "") {
  const label=t("搜索壁纸、桌面美化、模块","Search wallpapers, desktop styles, and mods");
  return `<form class="ModuleSearch" role="search"><svg width="15" height="15" viewBox="0 0 24 24" aria-hidden="true"><circle cx="10.5" cy="10.5" r="7" fill="none" stroke="currentColor" stroke-width="2.8"/><path d="m16 16 5 5" stroke="currentColor" stroke-width="2.8"/></svg><input class="SearchInput" name="q" autocomplete="off" spellcheck="false" placeholder="${label}" aria-label="${label}" value="${h(query)}"><button type="button" class="SearchClear" data-action="clear" aria-label="${t("清除搜索","Clear search")}" ${query ? "" : "hidden"}>×</button></form>`;
}
export function home(state) {
  const items=wallpaperHomeItems(state);
  const index=items.length ? ((state.slide % items.length) + items.length) % items.length : 0;
  const online=items.length ? items[index] : null;
  const loading=state.wallpaper.catalogStatus==="loading" || state.wallpaper.catalogStatus==="idle";
  // Before the native catalog arrives, paint the remembered hero instead of an
  // empty shell; the image bytes are already in the WebView disk cache.
  const remembered=online || !loading ? null : readHomeHero();
  const p=online || (remembered ? {id:remembered.id,title:remembered.title,width:remembered.width,
    height:remembered.height,kind:remembered.kind} : undefined);
  const imageSource=online ? wallpaperHeroImageUrl(online) : (remembered?.source || "");
  const image=online ? wallpaperAssetPreferredUrl(imageSource) : (remembered?.url || "");
  const fallback=online ? wallpaperAssetFallbackUrl(imageSource) : "";
  const tone=online ? wallpaperCardTones(online) : (remembered
    ? {toneA:remembered.toneA || "#1c1f24", toneB:remembered.toneB || "#2a2f36"}
    : wallpaperCardTones(p || {id:"official-wallpaper",title:"官方壁纸"}));
  if(online && image) writeHomeHero(homeHeroRecord(online, imageSource, image, tone));
  const title=p?.title || (loading ? t("正在读取官方壁纸…","Loading official wallpapers…") : t("暂无官方壁纸","No official wallpapers"));
  const imageMarkup=image ? `<img class="preview-image" data-wallpaper-preview data-wallpaper-asset-source="${h(imageSource)}" src="${h(image)}"${fallback ? ` data-wallpaper-fallback-src="${h(fallback)}"` : ""} alt="" decoding="async" style="position:absolute;inset:0;width:100%;height:100%;object-fit:cover;z-index:-2">` : "";
  const slides=items.map((_,i)=>`<button class="dot${i === index ? " active" : ""}" data-dot="${i}" aria-label="${t(`第 ${i + 1} 张壁纸`,`Wallpaper ${i + 1}`)}" aria-current="${i === index}"></button>`).join("");
  const arrows=items.length>1 ? [-1,1].map((delta) => `<button class="home-arrow ${delta < 0 ? "prev" : "next"}" data-slide="${delta}" aria-label="${delta < 0 ? t("上一张","Previous wallpaper") : t("下一张","Next wallpaper")}"><svg viewBox="0 0 24 24"><path d="m9 18 6-6-6-6"/></svg></button>`).join("") : "";
  const action=p && online ? `<button class="card-action" data-wallpaper-id="${h(p.id)}" data-wallpaper-source="official">${t("前往","Open")}</button>` : "";
  const metadata=p ? `${p.width}x${p.height} · ${p.kind==="video" ? t("动态壁纸","Dynamic wallpapers") : t("静态壁纸","Static wallpapers")}` : t("官方目录","Official library");
  return `<div class="HomeRoot"><section class="HomeHero"><div class="preview current" style="--image:none;--tone-a:${h(tone.toneA)};--tone-b:${h(tone.toneB)}">${imageMarkup}</div>${arrows}<div class="HomeDots">${slides}</div><div class="HomeHoverOverlay"><div class="HoverBody"><div class="HoverTitle">${h(title)}</div><div class="CardActions">${action}</div></div><div class="HoverMetaRow">${metadata}</div></div></section>${toolbar("home")}</div>`;
}
export function card(item, state, isWidget = false) {
  const display=localizedItem(item);
  const supported = ["clock","calendar","performance","spectrum","almanac","todayTodo","weather","organizer","icons","taskbar","minimize","radial","dock","dynamicIsland","music","notes"].includes(item.id);
  const preferences = item.id==='music'?state.musicDesktop:item.id==='notes'?state.notes:item.id==='taskbar'?taskbarDisplayedPreferences(state):state[item.id];
  const enabled = item.id==='organizer'?organizerDisplayedEnabled(state):supported && !!preferences?.enabled;
  const status = item.id === "clock" ? state.status : state[`${item.id}Status`];
  const pending = item.id==='music'||item.id==='notes'?!!preferences?.pending:
    status === "saving" || (["taskbar","minimize","radial","dock","dynamicIsland"].includes(item.id) && status === "loading");
  const action = item.id === "clock" ? "toggle" : supported ? `${item.id}-toggle` : "unsupported";
  const busyLabel=item.id==='organizer'?`<span class="organizer-pending" data-organizer-pending role="status" ${pending?'':'hidden'}>${pending?h(organizerStatus(state)):''}</span>`:'';
  const control=toggle(enabled, display.title, action, supported ? pending : true);
  const organizerControl=item.id==='organizer'?control.replace('<button ',`<button aria-busy="${pending}" title="${pending?h(organizerStatus(state)):''}" `):control;
  return `<article class="Card" aria-label="${h(display.title)}${isWidget ? t("组件"," widget") : t("模块"," module")}${t("卡片"," card")}"><button class="OpenButton" data-open="${item.id}" aria-label="${h(t(`打开${display.title}设置`,`Open ${display.title} settings`))}"><h3 class="Title">${h(display.title)}</h3><p class="Description">${h(display.description)}</p></button>${isWidget || item.toggle ? `<div class="Toggle">${busyLabel}${organizerControl}</div>` : ""}</article>`;
}
export function catalog(state, isWidgets) {
  return `<div class="ExperienceRoot${isWidgets ? " widget-catalog" : ""}">${isWidgets ? "" : toolbar("desktopBeauty")}<div class="MarketShell"><section class="DesktopBeautyLayout">${isWidgets ? `<div class="list-back">${back(t("返回桌面美化","Back to desktop styles"))}</div>` : ""}<div class="CardGrid" role="region" aria-label="${isWidgets ? t("组件列表","Widgets") : t("桌面美化模块","Desktop styles")}">${(isWidgets ? widgets : modules).filter(item=>!item.hidden).map((item) => card(item, state, isWidgets)).join("")}</div></section></div></div>`;
}
export function syncMusicDesktopControls(root,state,action='music-toggle') {
  root.querySelectorAll(`[data-action="${action}"]`).forEach(button=>{
    button.classList.toggle('checked',state.enabled);
    button.setAttribute('aria-checked',String(state.enabled));
    button.disabled=state.pending;
  });
}
/*
 * Original Lume detail icons, copied without path changes from the historical
 * @fortawesome/free-solid-svg-icons 7.1.0 package (fa*.js).
 * Font Awesome Free by Fonticons, Inc. — SVG icons: CC BY 4.0.
 * https://fontawesome.com/license/free
 * https://creativecommons.org/licenses/by/4.0/
 * CSS presentation is adapted to the original Lume detail controls.
 *
 * Settings2 / Plus: lucide-react 1.37.0, settings-2.mjs / plus.mjs, ISC License.
 * Copyright (c) 2026 Lucide Icons and Contributors.
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 * Plus also derives from Feather (Cole Bemis, MIT). Both full notices ship in
 * assets/sources/wallpaper-detail-icons.md.
 */
const originalWallpaperIcons={
  back:[512,512,"faArrowLeft","M9.4 233.4c-12.5 12.5-12.5 32.8 0 45.3l160 160c12.5 12.5 32.8 12.5 45.3 0s12.5-32.8 0-45.3L109.3 288 480 288c17.7 0 32-14.3 32-32s-14.3-32-32-32l-370.7 0 105.4-105.4c12.5-12.5 12.5-32.8 0-45.3s-32.8-12.5-45.3 0l-160 160z"],
  download:[448,512,"faDownload","M256 32c0-17.7-14.3-32-32-32s-32 14.3-32 32l0 210.7-41.4-41.4c-12.5-12.5-32.8-12.5-45.3 0s-12.5 32.8 0 45.3l96 96c12.5 12.5 32.8 12.5 45.3 0l96-96c12.5-12.5 12.5-32.8 0-45.3s-32.8-12.5-45.3 0L256 242.7 256 32zM64 320c-35.3 0-64 28.7-64 64l0 32c0 35.3 28.7 64 64 64l320 0c35.3 0 64-28.7 64-64l0-32c0-35.3-28.7-64-64-64l-46.9 0-56.6 56.6c-31.2 31.2-81.9 31.2-113.1 0L110.9 320 64 320zm304 56a24 24 0 1 1 0 48 24 24 0 1 1 0-48z"],
  trash:[448,512,"faTrashCan","M136.7 5.9C141.1-7.2 153.3-16 167.1-16l113.9 0c13.8 0 26 8.8 30.4 21.9L320 32 416 32c17.7 0 32 14.3 32 32s-14.3 32-32 32L32 96C14.3 96 0 81.7 0 64S14.3 32 32 32l96 0 8.7-26.1zM32 144l384 0 0 304c0 35.3-28.7 64-64 64L96 512c-35.3 0-64-28.7-64-64l0-304zm88 64c-13.3 0-24 10.7-24 24l0 192c0 13.3 10.7 24 24 24s24-10.7 24-24l0-192c0-13.3-10.7-24-24-24zm104 0c-13.3 0-24 10.7-24 24l0 192c0 13.3 10.7 24 24 24s24-10.7 24-24l0-192c0-13.3-10.7-24-24-24zm104 0c-13.3 0-24 10.7-24 24l0 192c0 13.3 10.7 24 24 24s24-10.7 24-24l0-192c0-13.3-10.7-24-24-24z"],
  heart:[512,512,"faHeart","M241 87.1l15 20.7 15-20.7C296 52.5 336.2 32 378.9 32 452.4 32 512 91.6 512 165.1l0 2.6c0 112.2-139.9 242.5-212.9 298.2-12.4 9.4-27.6 14.1-43.1 14.1s-30.8-4.6-43.1-14.1C139.9 410.2 0 279.9 0 167.7l0-2.6C0 91.6 59.6 32 133.1 32 175.8 32 216 52.5 241 87.1z"],
  resolution:[512,512,"faCropSimple","M128 32c0-17.7-14.3-32-32-32S64 14.3 64 32l0 32-32 0C14.3 64 0 78.3 0 96s14.3 32 32 32l32 0 0 256c0 35.3 28.7 64 64 64l208 0 0-64-208 0 0-352zM384 480c0 17.7 14.3 32 32 32s32-14.3 32-32l0-32 32 0c17.7 0 32-14.3 32-32s-14.3-32-32-32l-32 0 0-256c0-35.3-28.7-64-64-64l-208 0 0 64 208 0 0 352z"],
  size:[448,512,"faHardDrive","M64 32C28.7 32 0 60.7 0 96L0 260.1C18.1 247.4 40.2 240 64 240l320 0c23.8 0 45.9 7.4 64 20.1L448 96c0-35.3-28.7-64-64-64L64 32zM448 352c0-35.3-28.7-64-64-64L64 288c-35.3 0-64 28.7-64 64l0 64c0 35.3 28.7 64 64 64l320 0c35.3 0 64-28.7 64-64l0-64zM224 384a32 32 0 1 1 64 0 32 32 0 1 1 -64 0zm128-32a32 32 0 1 1 0 64 32 32 0 1 1 0-64z"],
  duration:[512,512,"faClock","M256 0a256 256 0 1 1 0 512 256 256 0 1 1 0-512zM232 120l0 136c0 8 4 15.5 10.7 20l96 64c11 7.4 25.9 4.4 33.3-6.7s4.4-25.9-6.7-33.3L280 243.2 280 120c0-13.3-10.7-24-24-24s-24 10.7-24 24z"],
};
const wallpaperIcon = (name) => {
  const original=originalWallpaperIcons[name];
  if (original) {
    const [width,height,id,shape]=original;
    return `<svg class="wallpaper-fa-icon" data-icon="${id}" viewBox="0 0 ${width} ${height}" style="--wallpaper-fa-width:${width/height}em" fill="currentColor" aria-hidden="true"><path d="${shape}"/></svg>`;
  }
  if (name==="settings") return '<svg data-icon="Settings2" viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M14 17H5"/><path d="M19 7h-9"/><circle cx="17" cy="17" r="3"/><circle cx="7" cy="7" r="3"/></svg>';
  if (name==="plus") return '<svg data-icon="Plus" viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M5 12h14"/><path d="M12 5v14"/></svg>';
  const paths={
    play:'<path d="m9 5 10 7-10 7Z"/>',
    pause:'<path d="M8 5v14M16 5v14"/>',
    stop:'<rect x="6" y="6" width="12" height="12" rx="1"/>',
    download:'<path d="M12 3v12m-5-5 5 5 5-5M4 16v4h16v-4"/>',
    trash:'<path d="M3 6h18M9 3h6M6 6l1 15h10l1-15M10 10v7M14 10v7"/>',
    settings:'<path d="M4 6h16M4 12h16M4 18h16"/><circle cx="9" cy="6" r="2"/><circle cx="15" cy="12" r="2"/><circle cx="9" cy="18" r="2"/>',
    image:'<rect x="3" y="4" width="18" height="16" rx="2"/><circle cx="8" cy="9" r="1.5"/><path d="m3 17 6-5 4 3 3-4 5 6"/>',
  };
  return `<svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${paths[name] || paths.image}</svg>`;
};
const disabled = value => value ? " disabled" : "";
const wallpaperBusy = state => ["loading","saving","unavailable"].includes(state.wallpaperStatus);
function wallpaperSelect(id, value, options, label, disabledValue = false) {
  const selected = options.find(([option]) => option === value) || options[0] || ["", t("暂无选项","No options")];
  const optionMarkup = options.map(([option, text]) =>
    `<option value="${h(option)}"${option === selected[0] ? " selected" : ""}>${h(text)}</option>`).join("");
  const menuMarkup = options.map(([option, text]) =>
    `<button type="button" role="option" class="wallpaper-select-option" data-wallpaper-option="${h(id)}" data-value="${h(option)}" aria-selected="${option === selected[0]}">${h(text)}</button>`).join("");
  return `<div class="wallpaper-select" data-wallpaper-select="${h(id)}"><select id="${h(id)}" class="wallpaper-select-native"${disabledValue ? " disabled" : ""} aria-hidden="true" tabindex="-1">${optionMarkup}</select><button type="button" class="wallpaper-select-trigger" data-wallpaper-select-trigger="${h(id)}" aria-haspopup="listbox" aria-expanded="false"${disabledValue ? " disabled" : ""}><span>${h(selected[1])}</span><svg viewBox="0 0 16 16" aria-hidden="true"><path d="m4 6 4 4 4-4" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/></svg></button><div class="wallpaper-select-menu" role="listbox" aria-label="${h(label)}" hidden>${menuMarkup}</div></div>`;
}
const mediaImage = item => wallpaperThumbnailUrl(item);
function wallpaperTone(seed, offset = 0) {
  let hash = offset + 17;
  for (const character of String(seed || "wallpaper")) hash = (hash * 31 + character.charCodeAt(0)) >>> 0;
  return `hsl(${(hash % 360 + offset) % 360} 58% ${offset ? 58 : 28}%)`;
}
function wallpaperCardTones(item) {
  const validTone = value => typeof value === "string" && /^#[0-9a-f]{6}$/i.test(value);
  return {
    toneA: validTone(item.toneA) ? item.toneA : wallpaperTone(`${item.id}:${item.title}`, 0),
    toneB: validTone(item.toneB) ? item.toneB : wallpaperTone(`${item.id}:${item.title}`, 64),
  };
}

export function wallpaperCard(item, source = "official", eager = false) {
  const imageSource=mediaImage(item);
  const image=wallpaperAssetPreferredUrl(imageSource);
  const alternate=wallpaperAssetPreferredUrl(wallpaperHeroImageUrl(item));
  const fallback=alternate!==image ? alternate : "";
  const metadata=[item.size || (item.kind==="image" ? t("图片壁纸","Image wallpaper") : t("视频壁纸","Video wallpaper")),
    item.size || wallpaperBytes(item.bytes)].filter(Boolean);
  if (item.size) metadata[0] = item.type || (item.kind==="image" ? t("图片壁纸","Image wallpaper") : t("视频壁纸","Video wallpaper"));
  const tones=wallpaperCardTones(item);
  const badge=item.missing ? t("文件已丢失","File missing") : source==="official" && item.libraryId ? t("已下载","Downloaded") : "";
  const fallbackClass=item.kind==="video" ? " video" : "";
  const placeholder=`<div class="wallpaper-card-placeholder${fallbackClass}" data-wallpaper-poster-fallback="true" style="--wallpaper-tone-a:${h(tones.toneA)};--wallpaper-tone-b:${h(tones.toneB)}">${wallpaperIcon(item.kind==="video"?"play":"image")}</div>`;
  const imageMarkup=image?`<img data-wallpaper-preview data-wallpaper-asset-source="${h(imageSource)}" src="${h(image)}"${fallback ? ` data-wallpaper-fallback-src="${h(fallback)}"` : ""} alt="" loading="${eager?"eager":"lazy"}" decoding="async" fetchpriority="${eager?"high":"low"}">`:"";
  return `<button class="wallpaper-card" data-wallpaper-id="${h(item.id)}" data-wallpaper-source="${source}" aria-label="${t("查看","View")} ${h(item.title)}">${placeholder}${imageMarkup}<div class="wallpaper-card-shade"></div>${badge?`<span class="wallpaper-card-badge${item.missing?" error":""}">${badge}</span>`:""}<div class="CardTitle">${h(item.title)}</div><div class="CardMeta">${h(metadata.join(" · "))}</div></button>`;
}
export function wallpaperPagination(page,pages,total) {
  return `<div class="wallpaper-pagination" aria-label="${t("壁纸分页","Wallpaper pages")}"><div class="wallpaper-page-list"><button class="wallpaper-page-button wallpaper-page-previous" data-wallpaper-page="${page-1}" aria-label="${t("上一页","Previous page")}"${disabled(page<=1)}>${t("上一页","Previous page")}</button>${wallpaperPages(page,pages).map(value=>value==="…"?'<span class="wallpaper-page-ellipsis">…</span>':`<button class="wallpaper-page-button${value===page?" active":""}" data-wallpaper-page="${value}" ${value===page?'aria-current="page"':""}>${value}</button>`).join("")}<button class="wallpaper-page-button wallpaper-page-next" data-wallpaper-page="${page+1}" aria-label="${t("下一页","Next page")}"${disabled(page>=pages)}>${t("下一页","Next page")}</button></div><label class="wallpaper-page-jump"><span class="wallpaper-page-summary">${t(`共 ${pages} 页，跳至`,`${pages} ${pages===1?"page":"pages"}, go to`)}</span><input id="wallpaper-page-jump" inputmode="numeric" type="number" min="1" max="${pages}" value="${page}" aria-label="${t("跳转到页码","Page number")}">${t("页","page")}</label></div>`;
}
function wallpaperImportCard(state) {
  return `<button class="wallpaper-card wallpaper-import-card" data-wallpaper-action="import" aria-label="${t("添加壁纸","Add wallpaper")}" title="${t("添加壁纸","Add wallpaper")}"${disabled(wallpaperBusy(state))}><div class="wallpaper-import-plus" aria-hidden="true">${wallpaperIcon("plus")}</div></button>`;
}
function wallpaperLoadingCards() {
  return Array.from({length:wallpaperPageSize},(_,index)=>
    `<div class="wallpaper-card wallpaper-card-loading" aria-hidden="true"><div class="wallpaper-card-placeholder wallpaper-card-placeholder-loading" style="--wallpaper-tone-a:hsl(${198+index*7} 28% 25%);--wallpaper-tone-b:hsl(${224+index*11} 24% 38%)"></div></div>`).join("");
}
function wallpaperGrid(state,collection) {
  const {source,items,query,kind,page}=collection;
  const importCard=source==="local" && page===1 && !query && kind==="all" ? wallpaperImportCard(state) : "";
  if (source==="official" && !items.length && state.wallpaper.catalogStatus==="loading") return wallpaperLoadingCards();
  let empty="";
  // Each collection loads independently: a failed video catalog next to a
  // loaded image catalog is a failure, not an empty official library.
  const failedKinds=source==="official" ? [...String(state.wallpaper.catalogError||"").matchAll(/(?:^|[:;])(video|image):/g)].map(match=>match[1]) : [];
  const collectionFailed=source==="official" && !query && state.wallpaper.catalogStatus!=="loading" &&
    (kind==="all" ? failedKinds.length>0 : failedKinds.includes(kind));
  if (!items.length && collectionFailed) {
    return `<div class="empty" role="status">${t("官方目录加载失败，请重试","Could not load the official library. Please try again.")} <button class="wallpaper-retry" data-wallpaper-action="refresh"${disabled(wallpaperBusy(state))}>${t("重试","Retry")}</button></div>`;
  }
  if (!items.length) {
    empty=query || kind==="image" ? t("没有找到匹配的壁纸","No matching wallpapers found") :
      source==="local" ? t("尚未添加壁纸，点击添加壁纸开始","No wallpapers yet. Add a wallpaper to get started.") :
      state.wallpaper.catalogStatus==="loading" ? t("正在读取官方壁纸…","Loading official wallpapers…") :
      state.wallpaper.catalogStatus==="failed" ? t("官方目录加载失败，请重试","Could not load the official library. Please try again.") :
      state.wallpaper.catalogStatus==="idle" ? t("正在准备官方壁纸…","Preparing official wallpapers…") : t("暂无官方壁纸","No official wallpapers");
  }
  return importCard + items.map((item,index)=>wallpaperCard(item,source,index<8)).join("") +
    (empty?`<div class="empty${importCard?" wallpaper-library-empty":""}" role="status">${empty}</div>`:"");
}
function wallpaperLibraryFooter(state,collection) {
  const notice=wallpaperNotice(state);
  const catalogError=collection.source==="official" && state.wallpaper.catalogStatus==="failed";
  const catalogNotice=collection.source==="official" ? wallpaperError(state.wallpaper.catalogError) : "";
  const feedback=notice || catalogNotice || (catalogError ? wallpaperError(state.wallpaper.catalogError) : "");
  const pagination=collection.total ? wallpaperPagination(collection.page,collection.pages,collection.total) : "";
  return `${pagination}${feedback ? `<span class="wallpaper-inline-notice${catalogError ? " error" : ""}" role="status">${h(feedback)}</span>` : ""}`;
}
export function library(state) {
  const collection=wallpaperCollection(state);
  const dropZone=collection.source==="local" ? ' data-wallpaper-drop-zone="local"' : "";
  return `<div class="ExperienceRoot wallpaper-library" data-wallpaper-library>${toolbar(state.view,collection.query,collection.kind)}<div class="MarketShell"><section class="WallpaperLibraryLayout"${dropZone}><div class="wallpaper-grid" role="region" aria-label="${collection.source==="official"?t("官方壁纸","Official wallpapers"):t("我的壁纸","My wallpapers")}">${wallpaperGrid(state,collection)}</div><div class="wallpaper-library-footer">${wallpaperLibraryFooter(state,collection)}</div></section></div></div>`;
}
export function search(state) {
  const local=state.wallpaper.library.map(item=>({...item,source:"local"}));
  const official=state.wallpaper.catalog.filter(item=>!item.libraryId ||
    !state.wallpaper.library.some(local=>local.id===item.libraryId)).map(item=>({...item,source:"official"}));
  const result=searchCatalog([...local,...official,...modules.filter(item=>!item.hidden),...widgets.filter(item=>!item.hidden)],state.query);
  const pages=Math.max(1,Math.ceil(result.length/wallpaperPageSize));
  const page=Math.min(state.wallpaperSearchPage,pages);
  const pageItems=result.slice((page-1)*wallpaperPageSize,page*wallpaperPageSize);
  return `<div class="ExperienceRoot"><div class="MarketShell"><section class="SearchResultsLayout wallpaper-search-results"><div class="SearchResultsHead">${back()}<div class="SearchResultsSearch">${searchBox(state.query)}</div></div><div class="wallpaper-grid">${pageItems.length?pageItems.map(item=>item.source?wallpaperCard(item,item.source):card(item,state,widgets.some(widget=>widget.id===item.id))).join(""):`<div class="empty">${t("没有找到匹配的内容","No matching content found")}</div>`}</div>${wallpaperPagination(page,pages,result.length)}<p class="availability">${t("搜索范围：我的壁纸、已载入的官方目录与桌面美化模块","Search scope: My wallpapers, the loaded official library and desktop styles")}</p></section></div></div>`;
}
function wallpaperMetadata(item) {
  const values=[
    item.width && item.height ? ["resolution",t("分辨率","Resolution"),`${item.width}x${item.height}`] : null,
    item.bytes ? ["size",t("大小","Size"),wallpaperBytes(item.bytes)] : null,
    item.kind==="video" && item.durationSeconds>0 ?
      ["duration",t("时长","Duration"),`${Math.floor(item.durationSeconds/60)}:${String(Math.floor(item.durationSeconds%60)).padStart(2,"0")}`] : null,
  ];
  return values.filter(Boolean).map(([icon,label,value])=>
    `<div><span title="${label}" aria-label="${label}">${wallpaperIcon(icon)}</span><strong>${h(value)}</strong></div>`).join("");
}
function wallpaperSettings(state,item) {
  const imageSource=mediaImage(item);
  const image=wallpaperAssetPreferredUrl(imageSource);
  const fallback=wallpaperAssetFallbackUrl(imageSource);
  const open=state.wallpaperPanel==="settings";
  const imageMarkup=image?`<img data-wallpaper-preview data-wallpaper-asset-source="${h(imageSource)}" src="${h(image)}"${fallback ? ` data-wallpaper-fallback-src="${h(fallback)}"` : ""} alt="">`:"";
  const unavailable=`<span data-wallpaper-info-fallback="true"${image ? " hidden" : ""}>${t("暂无预览图","Preview unavailable")}</span>`;
  const engagement=selectedWallpaperEngagement(state,item);
  const ready=engagement?.status==="ready" && !engagement.error;
  const totals=item.officialId ? [
    ["likes","heart",t("喜欢总数","Likes")],["downloads","download",t("下载总数","Downloads")],
  ].map(([key,icon,label])=>`<div data-wallpaper-count="${key}"><span title="${label}" aria-label="${label}">${wallpaperIcon(icon)}</span><strong>${ready ? engagement[key] ?? "--" : "--"}</strong></div>`).join("") : "";
  return `<aside class="wallpaper-info-sidebar" id="wallpaper-info-sidebar" data-wallpaper-overlay="settings" tabindex="-1" aria-label="${t("壁纸设置","Wallpaper settings")}"${open?"":" hidden"}><div class="wallpaper-info-thumbnail">${imageMarkup}${unavailable}<div class="wallpaper-info-overlay"><h3 title="${h(item.title)}">${h(item.title)}</h3><div class="wallpaper-info-meta">${wallpaperMetadata(item)}${totals}</div></div></div></aside>`;
}
function wallpaperApplyLabel(state,item) {
  const download=state.wallpaper.download;
  if (download.id===(item.officialId || item.id) && download.status==="running")
    return download.total>0 ? `${Math.min(100,Math.floor(download.received/download.total*100))}%` : t("下载中…","Downloading…");
  if (!item.libraryItem) return t("下载并应用","Download and apply");
  return state.wallpaper.displays.some(display=>wallpaperDisplayApplied(display,item)) ?
    t("取消应用","Unapply") : t("应用","Apply");
}
function wallpaperDisplayApplied(display,item) {
  return !!item.libraryItem && display && !display.error && ["playing","paused"].includes(display.state) &&
    display.wallpaperId===item.libraryItem?.id;
}
function wallpaperApplyTargets(state,item,canApply) {
  const id=item.libraryItem?.id || item.id,open=state.wallpaperPanel==="targets";
  return `<div class="wallpaper-detail-target-menu glass" id="wallpaper-target-menu" data-wallpaper-overlay="targets" role="menu" aria-label="${t("选择屏幕","Select screen")}"${open?"":" hidden"}>${state.wallpaper.displays.map((display,index)=>{
    const applied=wallpaperDisplayApplied(display,item);
    const label=t(`屏幕${index+1}`,`Screen ${index+1}`);
    return `<button role="menuitem" class="${applied?"is-cancel":""}" ${applied ?
      `data-wallpaper-action="stop" data-wallpaper-display="${h(display.id)}"` :
      `data-wallpaper-action="apply" data-wallpaper-id="${h(id)}" data-wallpaper-display="${h(display.id)}"`} title="${h(`${applied?t("取消应用","Unapply"):t("应用","Apply")} ${label}`)}"${disabled(!canApply)}>${applied?t(`取消屏幕${index+1}`,`Unapply screen ${index+1}`):label}</button>`;
  }).join("")}</div>`;
}
function wallpaperDetailControls(state) {
  const item=selectedWallpaper(state);
  if (!item) return `<div class="wallpaper-detail-bar glass"><button class="wallpaper-detail-back" data-action="back" aria-label="${t("返回壁纸列表","Back to wallpapers")}" title="${t("返回壁纸列表","Back to wallpapers")}">${wallpaperIcon("back")}</button><span class="wallpaper-detail-info">${t("这张壁纸已不在库中","This wallpaper is no longer in the library")}</span></div>`;
  const local=item.libraryItem,busy=wallpaperBusy(state);
  const download=state.wallpaper.download;
  const needsPlayer=item.kind==="video";
  const canApply=!item.missing && (!needsPlayer || state.wallpaper.playerAvailable) && state.wallpaper.displays.length>0 &&
    !busy && download.status!=="running" && (!!local || !!item.officialId);
  const engagement=selectedWallpaperEngagement(state,item);
  const liked=engagement?.status==="ready" && !engagement.error && engagement.liked;
  const likeTitle=engagement?.canLike ? liked?t("取消喜欢","Unlike"):t("喜欢","Like") :
    state.wallpaperAccount.status!=="signed-in" ? t("登录 Lume 后可喜欢壁纸","Sign in to Lume to like wallpapers") :
    t("喜欢状态暂不可用","Like status is unavailable");
  const settingsOpen=state.wallpaperPanel==="settings";
  const applied=state.wallpaper.displays.some(display=>wallpaperDisplayApplied(display,item));
  const singleDirect=state.wallpaper.displays.length===1 && applied;
  const primary=`<div class="wallpaper-detail-apply-wrap"><button class="wallpaper-apply${applied ? " glass is-cancel" : ""}" ${singleDirect ? `data-wallpaper-action="stop" data-wallpaper-display="${h(state.wallpaper.displays[0].id)}"` : `data-wallpaper-panel="targets" aria-haspopup="menu" aria-expanded="${state.wallpaperPanel==="targets"}" aria-controls="wallpaper-target-menu"`}${disabled(!canApply)}><span>${wallpaperApplyLabel(state,item)}</span></button>${singleDirect ? "" : wallpaperApplyTargets(state,item,canApply)}</div>`;
  const note=wallpaperNotice(state) || (local?.missing?t("文件已丢失，请重新导入","File missing. Please import it again."):needsPlayer && !state.wallpaper.playerAvailable?t("播放器不可用，请检查安装文件","Player unavailable. Check the installation files."):wallpaperEngagementError(engagement?.error));
  const preview=item.kind==="video" ? `<button class="wallpaper-preview-button" data-wallpaper-action="preview" aria-label="${t("播放预览","Play preview")}" title="${t("播放预览","Play preview")}" aria-pressed="false"${disabled(item.missing || state.wallpaperStatus==="unavailable")}>${wallpaperIcon("play")}</button>` : "";
  return `<div class="wallpaper-detail-bar glass"><button class="wallpaper-detail-back" data-action="back" aria-label="${t("返回壁纸列表","Back to wallpapers")}" title="${t("返回壁纸列表","Back to wallpapers")}">${wallpaperIcon("back")}</button><div class="wallpaper-detail-info"><h2 class="wallpaper-detail-title" title="${h(item.title)}">${h(item.title)}</h2><div class="wallpaper-detail-meta">${wallpaperMetadata(item)}</div></div><button class="wallpaper-icon-action download" data-wallpaper-action="download" data-wallpaper-id="${h(item.officialId || item.id)}" aria-label="${t("下载壁纸","Download wallpaper")}" title="${t("下载壁纸","Download wallpaper")}"${disabled(!item.officialId || !!local || busy || download.status==="running")}>${wallpaperIcon("download")}</button><button class="wallpaper-icon-action delete" data-wallpaper-action="remove" data-wallpaper-id="${h(local?.id || "")}" aria-label="${t("从我的壁纸移除","Remove from My wallpapers")}" title="${t("从我的壁纸移除","Remove from My wallpapers")}"${disabled(!local || busy || download.status==="running")}>${wallpaperIcon("trash")}</button><button class="wallpaper-icon-action settings" data-wallpaper-panel="settings" aria-label="${t("壁纸设置","Wallpaper settings")}" title="${t("壁纸设置","Wallpaper settings")}" aria-expanded="${settingsOpen}" aria-controls="wallpaper-info-sidebar">${wallpaperIcon("settings")}</button>${item.officialId?`<div class="wallpaper-detail-engagement"><button class="wallpaper-icon-action" data-wallpaper-action="like" data-wallpaper-id="${h(item.officialId)}" aria-pressed="${!!liked}" aria-label="${liked?t("取消喜欢","Unlike"):t("喜欢","Like")}" title="${likeTitle}"${disabled(!canLikeWallpaper(state,item))}>${wallpaperIcon("heart")}</button></div>`:""}<div class="wallpaper-detail-actions">${primary}</div></div>${wallpaperSettings(state,item)}${preview}<div class="wallpaper-detail-notice" role="status">${h(note)}</div>`;
}
export function wallpaperDetail(state) {
  const item=selectedWallpaper(state);
  const imageSource=wallpaperHeroImageUrl(item);
  const image=wallpaperAssetPreferredUrl(imageSource);
  const thumbnail=wallpaperAssetPreferredUrl(wallpaperThumbnailUrl(item));
  const fallback=thumbnail!==image ? thumbnail : "";
  const underlay=fallback?`<img class="wallpaper-detail-underlay" data-wallpaper-preview data-wallpaper-asset-source="${h(thumbnail)}" src="${h(thumbnail)}" decoding="async" fetchpriority="high" alt="">`:"";
  const placeholder=`<div class="wallpaper-detail-placeholder" data-wallpaper-poster-fallback="true"${image ? " hidden" : ""}>${t("壁纸预览","Wallpaper preview")}</div>`;
  const imageMarkup=image?`<img class="wallpaper-detail-poster" data-wallpaper-preview data-wallpaper-asset-source="${h(imageSource)}" data-wallpaper-fallback-surface="detail" decoding="async" fetchpriority="high" src="${h(image)}"${fallback ? ` data-wallpaper-fallback-src="${h(fallback)}"` : ""} alt="">`:"";
  return `<div class="HomeRoot wallpaper-detail" data-wallpaper-detail><div class="wallpaper-detail-media">${placeholder}${underlay}${imageMarkup}${item?.kind==="video"?`<video class="wallpaper-preview-video" preload="none" loop playsinline aria-label="${t("壁纸视频预览","Wallpaper video preview")}"></video>`:""}<div class="wallpaper-detail-shade"></div></div><div data-wallpaper-controls>${wallpaperDetailControls(state)}</div></div>`;
}

export function updateWallpaperDetailLayout(root) {
  const bar=root.querySelector(".wallpaper-detail-bar");
  const title=bar?.querySelector(".wallpaper-detail-title");
  const meta=bar?.querySelector(".wallpaper-detail-meta");
  if (!title || !meta) return;
  const range=bar.ownerDocument.createRange();
  const width=element=>{
    range.selectNodeContents(element);
    return range.getBoundingClientRect().width;
  };
  const contentWidth=Math.max(width(title),width(meta));
  // The original detail page reserves 420px for its controls and clamps the
  // resulting capsule to 520–760px. Measure only when its content changes.
  bar.style.setProperty("--wallpaper-detail-width",`${Math.min(760,Math.max(520,420+Math.ceil(contentWidth)))}px`);
}

const wallpaperRenderCache=new WeakMap();
// Snapshot updates keep the current preview, search field and scroll position.
export function syncWallpaperControls(root,state,previewPlaying=false) {
  const detail=root.querySelector("[data-wallpaper-detail]");
  const list=root.querySelector("[data-wallpaper-library]");
  if (detail) {
    // Compare only this surface's markup. The catalog can contain thousands of
    // records; a display or revision update must not serialize it or reset focus.
    const signature=wallpaperDetailControls(state);
    if (wallpaperRenderCache.get(detail)!==signature) {
      const focused=root.ownerDocument?.activeElement;
      const focusId=focused?.id;
      const focusPanel=focused?.dataset.wallpaperPanel;
      const focusAction=focused?.dataset.wallpaperAction;
      detail.querySelector("[data-wallpaper-controls]").innerHTML=signature;
      const replacement=focusId ? detail.querySelector(`#${CSS.escape(focusId)}`) :
        focusPanel ? detail.querySelector(`[data-wallpaper-panel="${focusPanel}"]`) :
          focusAction ? [...detail.querySelectorAll("[data-wallpaper-action]")].find(button=>
            button.dataset.wallpaperAction===focusAction && button.dataset.wallpaperDisplay===focused.dataset.wallpaperDisplay) : null;
      if (replacement && !replacement.disabled && !replacement.closest("[hidden]")) replacement.focus({preventScroll:true});
      updateWallpaperDetailLayout(detail);
      wallpaperRenderCache.set(detail,signature);
    }
    updateWallpaperPreviewButton(root,previewPlaying);
  } else if (list) {
    const collection=wallpaperCollection(state);
    const signature=wallpaperGrid(state,collection);
    const cachedSignature=wallpaperRenderCache.get(list);
    if (cachedSignature===undefined) {
      // The initial render normally contains this exact grid.  A catalog can
      // also arrive asynchronously while the list is already visible,
      // though; in that case the DOM still contains the loading/empty grid.
      // Only replace it when it differs so the first partial host snapshot
      // does not reset scroll position or focused cards.
      const grid=list.querySelector(".wallpaper-grid");
      if (grid && grid.innerHTML!==signature) grid.innerHTML=signature;
      wallpaperRenderCache.set(list,signature);
    } else if (cachedSignature!==signature) {
      const grid=list.querySelector(".wallpaper-grid"),scroll=grid.scrollTop;
      grid.innerHTML=signature;
      grid.scrollTop=scroll;
      wallpaperRenderCache.set(list,signature);
    }
    const focused=root.ownerDocument?.activeElement;
    const jumpValue=focused?.id==="wallpaper-page-jump" ? focused.value : null;
    list.querySelector(".wallpaper-library-footer").innerHTML=wallpaperLibraryFooter(state,collection);
    if (jumpValue!==null) {
      const input=list.querySelector("#wallpaper-page-jump");
      input.value=jumpValue;
      input.focus();
    }
  }
}
export function updateWallpaperPreviewButton(root,playing) {
  const button=root.querySelector(".wallpaper-preview-button");
  if (button) {
    const label=playing?t("暂停预览","Pause preview"):t("播放预览","Play preview");
    button.setAttribute("aria-pressed",String(playing));
    button.setAttribute("aria-label",label);
    button.title=label;
    button.innerHTML=wallpaperIcon(playing?"pause":"play");
  }
}
export function updateWallpaperDownloadControls(root,state) {
  const item=selectedWallpaper(state),label=root.querySelector(".wallpaper-apply > span");
  if (item && label) label.textContent=wallpaperApplyLabel(state,item);
}
export function unavailablePage(title) {
  return `<div class="settings"><div class="settings-head">${back()}<div><h3>${h(title)}</h3><p>${t("该功能暂未接入","This feature is not yet available")}</p></div></div><div class="settings-body"><p>${t(`此版本尚未接入${h(title)}，没有更改系统设置。`,`${h(title)} is not available in this version. System settings have not been changed.`)}</p></div></div>`;
}
// Update only carousel nodes: keep the toolbar/search DOM, input value and focus.
export function updateHomeCarousel(root, state) {
  const items=wallpaperHomeItems(state),hero=root.querySelector(".HomeHero");
  if (!hero || !items.length) return;
  const index=((state.slide % items.length) + items.length) % items.length;
  const p=items[index],imageSource=wallpaperHeroImageUrl(p),image=wallpaperAssetPreferredUrl(imageSource),
    fallback=wallpaperAssetFallbackUrl(imageSource),
    tone=wallpaperCardTones(p);
  if (image) writeHomeHero(homeHeroRecord(p, imageSource, image, tone));
  const syncChrome = () => {
    hero.querySelector(".HoverTitle").textContent = p.title;
    hero.querySelector(".HoverMetaRow").textContent =
      `${p.width}x${p.height} · ${p.kind==="video" ? t("动态壁纸","Dynamic wallpapers") : t("静态壁纸","Static wallpapers")}`;
    const action=hero.querySelector("[data-wallpaper-id]");
    if (action) {
      action.dataset.wallpaperId=p.id;
      action.dataset.wallpaperSource="official";
    }
    hero.querySelectorAll("[data-dot]").forEach((dot, i) => {
      dot.classList.toggle("active", i === index);
      dot.setAttribute("aria-current", String(i === index));
    });
  };
  hero.querySelector(".preview.prev")?.remove();
  const previous = hero.querySelector(".preview.current");
  // A state publish (download progress, widget refresh, resource sampling) must
  // not rebuild and re-decode the same hero: update only the surrounding chrome.
  if (previous && previous.dataset.wallpaperHero === p.id && previous.dataset.wallpaperHeroSrc === image) {
    syncChrome();
    return;
  }
  if (previous) previous.className = "preview prev";
  const next = document.createElement("div");
  next.className = "preview current";
  next.dataset.wallpaperHero = p.id;
  next.dataset.wallpaperHeroSrc = image;
  next.style.setProperty("--image", "none");
  next.style.setProperty("--tone-a", tone.toneA);
  next.style.setProperty("--tone-b", tone.toneB);
  if (image) {
    const preview=document.createElement("img");
    preview.className="preview-image";
    preview.dataset.wallpaperPreview="";
    preview.dataset.wallpaperAssetSource=imageSource;
    preview.src=image;
    if (fallback) preview.dataset.wallpaperFallbackSrc=fallback;
    preview.alt="";
    preview.decoding="async";
    preview.style.cssText="position:absolute;inset:0;width:100%;height:100%;object-fit:cover;z-index:-2";
    next.append(preview);
  }
  hero.prepend(next);
  syncChrome();
}
