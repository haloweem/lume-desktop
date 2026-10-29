import {MUSIC_ICONS} from './music-icons.js';
import {musicCollectionKey} from './music-collections.js';
import {musicPlayerPanel} from './music-player-panel.js';
import {musicProviderButtons} from './music-provider-account.js';
import {musicOnlineError,musicTrackLiked,canLikeMusicTrack} from './music-online.js';
import {settingsFrame} from './settings-frame.js';

const icon = name => MUSIC_ICONS[name] || MUSIC_ICONS.queue;
const text = value => typeof value === 'string' ? value : '';
const number = (value, fallback = 0) => Number.isFinite(Number(value)) ? Number(value) : fallback;
const onlineTrack = track => /^(netease|qq|kugou):/.test(track?.id||'');
const localTrack = track => track?.id?.startsWith('local-');
const membershipBadges = new Set(['qq-green-vip','qq-super-vip','netease-vip','netease-svip','kugou-vip']);
const qualityOptions = {
  netease:[
    ['jyeffect','沉浸环绕声','Surround Audio','netease-vip'],
    ['sky','臻音全景声','Audio Vivid','netease-svip'],
    ['jymaster','超清母带','母带级音质','netease-svip'],
    ['hires','Hi-Res','Hi-Res 音质','netease-vip'],
    ['lossless','无损','无损音质','netease-vip'],
    ['high','极高','HQ · 最高 320kbps'],['standard','标准','128kbps'],
  ],
  qq:[
    ['hires','臻品音质','高解析无损','qq-super-vip'],
    ['lossless','SQ无损品质','无损音质','qq-green-vip'],
    ['high','HQ高品质','最高 320kbps','qq-green-vip'],
    ['standard','标准品质','标准音质'],
  ],
  kugou:[
    ['standard','128K','标准音质'],['high','320K','高品质','kugou-vip'],
    ['lossless','无损','无损音质','kugou-vip'],['hires','Hi-Res','Hi-Res 音质','kugou-vip'],
  ],
};
// Higher tiers include the lower ones (live 2026-10-11: a 黑胶VIP account
// resolved sky/jymaster down to jyeffect, a 豪华绿钻 account has no SVIP).
const membershipNames = {'qq-green-vip':'QQ音乐绿钻','qq-super-vip':'QQ音乐超级会员',
  'netease-vip':'网易云黑胶VIP','netease-svip':'网易云黑胶SVIP','kugou-vip':'酷狗VIP'};
const membershipCovers = {'qq-green-vip':['qq-green-vip','qq-super-vip'],'qq-super-vip':['qq-super-vip'],
  'netease-vip':['netease-vip','netease-svip'],'netease-svip':['netease-svip'],'kugou-vip':['kugou-vip']};
// '' when the signed-in account of that platform holds the tier, otherwise
// the notice to show. Unknown kinds never block.
export function musicMembershipBlocked(library, kind) {
  if (!membershipBadges.has(kind)) return '';
  const account = library?.accounts?.find(item=>item.provider===kind.split('-')[0]);
  const held = (account?.memberships||[]).some(item=>item.active===true&&membershipCovers[kind].includes(item.kind));
  return held ? '' : `需要${membershipNames[kind]}`;
}
const audioQualities = new Set(Object.values(qualityOptions).flat().map(option=>option[0]));
const escapeHtml = value => text(value).replace(/[&<>"']/g, character => ({
  '&':'&amp;', '<':'&lt;', '>':'&gt;', '"':'&quot;', "'":'&#39;',
}[character]));

function safeImage(value) {
  if (typeof value !== 'string' || value.length > 262144) return '';
  if (/^data:image\/(?:png|jpeg|webp);base64,[A-Za-z0-9+/=]+$/.test(value)) return value;
  try {
    if(value.startsWith('//'))value=`https:${value}`;
    const url = new URL(value);
    if(/^http:\/\/127\.0\.0\.1:[1-9]\d{0,4}\/[a-f0-9]{64}\/media\.png$/.test(value)&&Number(url.port)<=65535)return url.href;
    if (!['https:','http:'].includes(url.protocol) || url.username || url.password || url.port) return '';
    const providerHost = ['music.126.net','qq.com','gtimg.cn','kugou.com','kgimg.com']
      .some(domain=>url.hostname===domain||url.hostname.endsWith(`.${domain}`));
    if (!['static.lumedesktop.com', 'lume-wallpaper.local', 'lume-12345-1444884137.cos.ap-nanjing.myqcloud.com']
      .includes(url.hostname) && !providerHost) return '';
    url.protocol='https:';
    return url.href;
  } catch { return ''; }
}

export function musicFormatTime(value) {
  const seconds = Number(value);
  if (!Number.isFinite(seconds) || seconds < 0) return '--:--';
  const total = Math.floor(seconds);
  return `${Math.floor(total / 60)}:${String(total % 60).padStart(2, '0')}`;
}

export function initialMusicState() {
  return {
    pageOpen:false, pageVisible:false, account:{signedIn:false}, session:0, revision:0,
    status:'empty', playing:false, currentIndex:0, current:null, queue:[],
    timeline:{position:0, duration:0}, repeat:'none', shuffle:false,volume:1,muted:false,
    analysis:{energy:0,bins:Array(32).fill(0)},
    capabilities:{canPrevious:false, canNext:false, canToggle:false, canSeek:false},
    error:'', query:'', nav:'recommendations', lyrics:[], score:null,
  };
}

function normalizeTrack(track) {
  if (!track || typeof track !== 'object' || !text(track.id)) return null;
  return {id:text(track.id), title:text(track.title) || '未知歌曲', artist:text(track.artist),
    album:text(track.album), source:text(track.source), artwork:safeImage(track.artwork),
    duration:Math.max(0, number(track.duration)), vipRequired:track.vipRequired===true,
    ...(membershipBadges.has(track.membershipBadge) ? {membershipBadge:track.membershipBadge} : {})};
}

// Payload validation belongs to bridge.js; this projection rejects stale replies.
export function applyMusicLyrics(state, message) {
  if(message.session!==state.session || message.trackId!==state.current?.id) return state;
  return {...state,lyrics:message.lines};
}

export function normalizeMusicState(message, previous = initialMusicState()) {
  if (!message || typeof message !== 'object') return previous;
  const queue = Array.isArray(message.queue) ? message.queue.slice(0,512).map(normalizeTrack).filter(Boolean) : previous.queue;
  // A null current track is an authoritative native end-of-queue snapshot.
  const current = Object.hasOwn(message, 'current') ? normalizeTrack(message.current) : previous.current;
  const duration = Math.max(0, number(message.timeline?.duration, current?.duration || 0));
  const changed = current?.id !== previous.current?.id || current?.source !== previous.current?.source ||
    (message.session !== undefined && message.session !== previous.session);
  return {...previous, ...message, account:{signedIn:message.account?.signedIn === true}, queue, current,
    timeline:{position:Math.max(0,Math.min(duration,number(message.timeline?.position))),duration},
    capabilities:{...previous.capabilities,...message.capabilities},
    lyrics:Array.isArray(message.lyrics) ? message.lyrics : changed ? [] : previous.lyrics,
    score:Object.hasOwn(message,'score') ? message.score ?? null : changed ? null : previous.score ?? null};
}

export function musicCommand(action, fields = {}, state = initialMusicState()) {
  if (fields.session !== undefined && fields.session !== state.session) return null;
  const wireAction = {'play-now':'playNow','load-home':'loadHome','load-lyrics':'loadLyrics'}[action] || action;
  const allowed = new Set(['open','close','visibility','snapshot','login','logout','loadHome','loadLyrics','loadScore',
    'play','pause','toggle','previous','next','seek','playNow','enqueue','insertNext','removeFromQueue','clearQueue','setRepeat','setShuffle','setVolume','setMuted','setQuality']);
  if (!allowed.has(wireAction)) return null;
  const command = {version:1,scope:'music',action:wireAction};
  if (Number.isSafeInteger(state.session) && state.session > 0) command.session = state.session;
  if (wireAction === 'visibility' && typeof fields.visible === 'boolean') command.visible = fields.visible;
  else if (wireAction === 'seek' && Number.isFinite(Number(fields.position)) && Number(fields.position) >= 0) command.position = Number(fields.position);
  else if (wireAction === 'playNow' && Number.isSafeInteger(fields.index) && fields.index >= 0) {
    const id=fields.id ?? state.queue[fields.index]?.id;
    if(!text(id) || id.length>128)return null;
    command.index=fields.index;command.id=id;
  }
  else if (['loadLyrics','loadScore','enqueue','insertNext','removeFromQueue'].includes(wireAction) && text(fields.id)) command.id = text(fields.id);
  else if (wireAction === 'setRepeat' && ['none','one','all'].includes(fields.value)) command.value = fields.value;
  else if (['setShuffle','setMuted'].includes(wireAction) && typeof fields.value === 'boolean') command.value = fields.value;
  else if (wireAction === 'setVolume' && Number.isFinite(fields.value) && fields.value>=0 && fields.value<=1) command.value=fields.value;
  else if (wireAction === 'setQuality' && audioQualities.has(fields.value)) command.value=fields.value;
  else if (!['open','close','snapshot','login','logout','loadHome','play','pause','toggle','previous','next','clearQueue'].includes(wireAction)) return null;
  return command;
}

// Playback errors name the cause the user can act on; anything else keeps the
// original generic status.
export function musicPlaybackError(error) {
  if (!error) return '';
  return {
    'music-source-expired':'音乐平台登录已过期，请重新扫码登录',
    'music-source-network':'网络连接失败，请检查网络后重试',
    'music-source-unavailable':'这首歌暂时无法播放，可能需要会员或已下架',
    'music-quality-buffering-timeout':'新音质缓冲较慢，已保留原音质播放，请稍后重试',
    'load-failed':'这首歌的音频格式无法播放',
  }[error] || '音乐操作未成功';
}

function artwork(track, className, fallback='disc') {
  const source = safeImage(track?.artwork);
  return `<span class="${className}">${icon(fallback)}${source ? `<img data-music-image src="${escapeHtml(source)}" alt="" loading="lazy" decoding="async" draggable="false">` : ''}</span>`;
}

function control(action, label, name, {disabled=false, active=false, extra=''} = {}) {
  return `<button class="MusicIconButton${active ? ' is-active' : ''}" type="button" data-music-action="${action}" aria-label="${label}" title="${label}" ${disabled ? 'disabled' : ''} ${extra}>${icon(name)}</button>`;
}

function membershipBadge(track) {
  const badge = track?.membershipBadge;
  if (badge === 'qq-green-vip' || badge === 'qq-super-vip') {
    const superVip = badge === 'qq-super-vip';
    return `<span class="MusicSongMembership MusicSongMembership--qq-${superVip ? 'super' : 'green'}" data-song-membership-badge="${badge}" data-song-membership-icon="${badge}" title="${superVip ? 'QQ 超级会员' : 'QQ 绿钻会员'}" aria-label="${superVip ? 'QQ 超级会员' : 'QQ 绿钻会员'}"><img data-music-image src="https://y.qq.com/mediastyle/lv-icon/v14/2x/${superVip?'svip5':'vip5'}.png" alt="" loading="lazy" decoding="async" draggable="false"></span>`;
  }
  if (badge === 'netease-vip' || badge === 'netease-svip') {
    const label = badge === 'netease-svip' ? '网易云黑胶SVIP' : '网易云黑胶VIP';
    return `<span class="MusicSongMembership MusicSongMembership--netease${badge === 'netease-svip' ? ' is-super' : ''}" data-song-membership-badge="${badge}" data-song-membership-provider="netease" title="${label}" aria-label="${label}">${icon('disc')}</span>`;
  }
  if (badge === 'kugou-vip') {
    return `<span class="MusicSongMembership MusicSongMembership--kugou" data-song-membership-badge="${badge}" data-song-membership-provider="kugou" title="酷狗VIP会员" aria-label="酷狗VIP会员">VIP</span>`;
  }
  return track?.vipRequired ? '<span class="MusicTrackVip" title="需要会员或付费权益" aria-label="需要会员或付费权益">VIP</span>' : '';
}

function audioQualityControl(state, library) {
  const provider=state.current?.id?.split(':')[0], options=qualityOptions[provider]||[];
  const memberships=library.accounts?.find(account=>account.provider===provider)?.memberships||[];
  const quality=state.audio?.quality;
  const switching=state.audio?.switching===true;
  const requested=options.find(option=>option[0]===state.audio?.requestedQuality)?.[1]||'音质';
  const fallback=!switching&&quality&&quality!==state.audio?.requestedQuality;
  const label=!state.current?'音质':!onlineTrack(state.current)?'原始':
    options.find(option=>option[0]===quality)?.[1]||'音质';
  const title=(state.audio?.bitrate?`音质：${label} · ${Math.round(state.audio.bitrate/1000)}kbps`:`音质：${label}`)+
    (switching?` · 正在切换至${requested}，继续播放${label}`:fallback?` · ${requested}暂不可用，已使用${label}`:'');
  const selectable=options.length>0&&state.session>0;
  return `<div class="MusicAudioQualityPicker" data-music-quality-picker>
    <button type="button" class="MusicAudioQuality" data-music-quality data-music-quality-trigger title="${title}" aria-label="${title}" aria-busy="${switching}" aria-haspopup="menu" aria-expanded="false" ${selectable?'':'disabled'}>${switching?'切换中…':label}</button>
    <div class="MusicAudioQualityMenu" data-music-quality-menu role="menu" aria-label="音质" hidden>${options.map(([value,name,detail,badge])=>{
      if(provider==='qq'&&value==='standard'&&state.current.vipRequired)badge='qq-green-vip';
      const membership=memberships.find(item=>item.kind===badge);
      const blocked=musicMembershipBlocked(library,badge);
      const image=safeImage(membership?.iconUrl);
      const marker=image?`<span class="MusicQualityMembership${membership.active===false?' is-inactive':''}"><img data-music-image src="${escapeHtml(image)}" alt="" decoding="async"></span>`:
        badge?.startsWith('netease')?'':badge?membershipBadge({membershipBadge:badge}):'';
      const status=switching&&value===state.audio?.requestedQuality?'正在切换…':
        fallback&&value===state.audio?.requestedQuality?`暂不可用，当前播放${label}`:detail;
      return `<button type="button" role="menuitemradio" aria-checked="${value===(quality||state.audio?.requestedQuality)}" data-music-quality-option="${value}"${blocked?` disabled aria-disabled="true" title="${blocked}"`:''}><span><span class="MusicQualityName">${name}${marker}</span><small>${blocked||status}</small></span><span class="MusicQualityCheck" aria-hidden="true">${icon('check')}</span></button>`;
    }).join('')}</div></div>`;
}

function navButton(id, label, name, active) {
  return `<button class="MusicNavItem${active === id ? ' is-active' : ''}" type="button" data-music-nav="${id}" aria-current="${active === id ? 'page' : 'false'}">${icon(name)}<span>${label}</span></button>`;
}

function sidebar(state, view, library) {
  const playlists = (library.playlists || []).filter(item => (item.owned ? 'owned' : 'subscribed') === (view.playlistGroup || 'owned'));
  const empty = view.playlistGroup === 'subscribed' ? '还没有收藏歌单' : '还没有自建歌单';
  return `<aside class="MusicSidebar" data-music-home-sidebar aria-label="音乐导航">
    <div class="MusicBrand">音乐</div>
    <div class="MusicNavLabel">在线音乐</div><nav class="MusicNav" aria-label="在线音乐">
      ${navButton('recommendations','首页','home',view.nav)}${navButton('albums','探索','discover',view.nav)}</nav>
    <div class="MusicNavLabel MusicNavLabel--library">我的音乐</div><nav class="MusicNav" aria-label="我的音乐">
      ${navButton('favorites','喜欢','heart',view.nav)}${navButton('recent','最近播放','recent',view.nav)}</nav>
    <section class="MusicPlaylistLibrary"><div class="MusicPlaylistTabs">
      <button type="button" data-music-playlist-group="owned" aria-pressed="${view.playlistGroup !== 'subscribed'}">自建歌单</button><span>|</span>
      <button type="button" data-music-playlist-group="subscribed" aria-pressed="${view.playlistGroup === 'subscribed'}">收藏歌单</button>
      ${control('create-playlist','新建歌单','plus',{disabled:!library.signedIn||library.loading})}</div>
      <div class="MusicPlaylistItems">${playlists.length ? playlists.map(item => `<button type="button" class="MusicPlaylistItem" data-music-playlist="${escapeHtml(musicCollectionKey(item))}" aria-current="${musicCollectionKey(item)===view.playlistId?'page':'false'}">${artwork(item,'MusicPlaylistArtwork')}<span>${escapeHtml(item.title)}</span></button>`).join('') : `<p class="MusicPlaylistEmpty">${empty}</p>`}</div></section>
    <div class="MusicLocalLibrary">${navButton('local','本地音乐','queue',view.nav)}
      <button class="MusicImportButton" type="button" data-music-action="import-local">${icon('upload')}<span>导入本地音乐</span></button></div>
  </aside>`;
}

function guestState(library) {
  return `<section class="MusicGuest" aria-labelledby="music-guest-title">
    <div class="MusicGuestIcon">${icon('user')}</div><div class="MusicGuestCopy"><h1 id="music-guest-title">登录，开启你的音乐</h1>
    <p>扫码连接音乐平台账户。</p></div>
    <div class="MusicProviderButtons">${musicProviderButtons(library.accounts)}</div>
    <button class="MusicTextButton" type="button" data-music-action="import-local">${icon('upload')}导入本地音乐</button>
  </section>`;
}

function recommendations(items, library) {
  if (!items.length) return '';
  const name = library.accounts?.find(account=>account.provider===library.providerId)?.displayName?.trim();
  const card = (item, featured=false) => `<button class="MusicRecommendationCard${featured ? ' MusicRecommendationCard--featured' : ''}" type="button" data-music-playlist="${escapeHtml(musicCollectionKey(item))}">
    <span class="MusicRecommendationArtwork">${artwork(item,'MusicRecommendationImage','queue')}<span class="MusicRecommendationShade"></span>${featured ? `<strong>${escapeHtml(item.title)}</strong>` : ''}</span>
    ${featured ? '' : `<strong>${escapeHtml(item.title)}</strong><span>${escapeHtml(item.description || '推荐歌单')}</span>`}</button>`;
  return `<section class="MusicRecommendations" data-music-home-recommendation-playlists>
    <h2>${name ? `Hi ${escapeHtml(name)}，` : ''}今日为你推荐</h2><div class="MusicRecommendationGrid MusicRecommendationGrid--featured">${items.slice(0,5).map((item,index)=>card(item,index===0)).join('')}</div>
    ${items.length > 5 ? `<h2 class="MusicSupplyTitle">你的歌单补给站</h2><div class="MusicRecommendationGrid MusicRecommendationGrid--supply">${items.slice(5,65).map(item=>card(item)).join('')}</div>` : ''}</section>`;
}

function trackTable(state, tracks, query='', {queue=false,rail=false,library={}}={}) {
  const term = query.trim().toLocaleLowerCase();
  const visible = tracks.filter(track => !term || [track.title,track.artist,track.album].some(value=>text(value).toLocaleLowerCase().includes(term)));
  return `<div class="MusicTrackList" data-music-home-track-list role="table" aria-label="歌曲列表">
    <div class="MusicTableHead" role="row"><span role="columnheader">歌名/歌手</span><span role="columnheader" aria-label="歌曲操作"></span><span role="columnheader">专辑</span><span role="columnheader">时长</span></div>
    ${visible.length ? visible.map((track,i) => {const index=state.queue.findIndex(item=>item.id===track.id); const online=onlineTrack(track);
      const local=!queue&&localTrack(track);
      const liked=musicTrackLiked(library,track.id);
      const locked=online?musicMembershipBlocked(library,track.membershipBadge):'';
      return `<div class="MusicTrackRow${state.current?.id === track.id ? ' is-current' : ''}${locked?' is-locked':''}" role="row" data-music-track-id="${escapeHtml(track.id)}">
      <div class="MusicTrackName" role="cell"><button type="button" class="MusicTrackPlay" data-music-action="${local?'local-play':online&&!queue?'online-play':'play-now'}" data-music-index="${index}" data-music-session="${state.session}" aria-label="播放 ${escapeHtml(track.title)}"${locked?` data-music-locked="${locked}" title="${locked}"`:''} ${index < 0&&!online&&!local||online&&!queue&&library.busy||local&&(library.localLoading||track.available===false) ? 'disabled' : ''}>
      <span class="MusicTrackNumber">${i+1}</span>${artwork(track,'MusicTrackArtwork')}<span class="MusicTrackMeta"><span class="MusicTrackTitle"><strong>${escapeHtml(track.title)}</strong>${membershipBadge(track)}</span><span>${escapeHtml(track.artist || '本地音乐')}</span></span></button></div>
      <div class="MusicTrackActions" role="cell">${rail?
        online?control('like',liked?'取消喜欢':'喜欢','heart',{disabled:!canLikeMusicTrack(library,track.id),active:liked,extra:`aria-pressed="${liked}"`}):'':
        queue?
        control('insert-next','下一首播放','next')+control('remove-track','移出队列','remove'):
        local?control('local-enqueue','加入播放队列','plus',{disabled:library.localLoading||track.available===false})+
          control('local-remove','从本地库移除','remove',{disabled:library.localLoading}):
        control('like',liked?'取消喜欢':'喜欢','heart',{disabled:!canLikeMusicTrack(library,track.id),active:liked,extra:`aria-pressed="${liked}"`})+
        control('add-to-playlist','添加到歌单','plus',{disabled:!online||!library.signedIn||library.busy})}
        ${!queue&&!rail?control(local?'local-next':'insert-next','下一首播放','next',{disabled:local?library.localLoading||track.available===false:index<0&&!online||online&&library.busy}):''}</div>
      <span class="MusicTrackAlbum" role="cell">${escapeHtml(track.album || '本地文件')}</span><span class="MusicTrackTime" role="cell">${track.duration ? musicFormatTime(track.duration) : '--:--'}</span></div>`;}).join('') :
      `<div class="MusicEmptyQueue">${term ? '没有匹配歌曲' : '暂无歌曲'}</div>`}</div>`;
}

function homeContent(state, view, library) {
  const isLocal = !view.search && view.nav === 'local';
  if (!isLocal && !library.signedIn && !view.search) return guestState(library);
  const collection = (library.playlists || []).concat(library.recommendations || []).find(item=>musicCollectionKey(item)===view.playlistId);
  const tracks = isLocal ? library.localTracks||state.queue.filter(track=>!onlineTrack(track)) :
    view.search ? library.signedIn?library.searchTracks||[]:state.queue : view.nav === 'favorites' ? library.favorites || [] :
    view.nav === 'recent' ? library.recent || [] : collection?.tracks || library.tracks || [];
  const recommended = !view.search && ['recommendations','albums'].includes(view.nav) && !view.playlistId && library.signedIn ? library.recommendations || [] : [];
  const title = view.search ? '搜索结果' : isLocal ? '本地音乐' : collection?.title || ({favorites:'喜欢',recent:'最近播放',albums:'探索',playlists:'歌单'})[view.nav] || '为你推荐';
  const terms=[library.signedIn?'':view.search,view.listQuery].filter(Boolean).map(value=>value.trim().toLocaleLowerCase());
  const visible=tracks.filter(track=>terms.every(term=>[track.title,track.artist,track.album]
    .some(value=>text(value).toLocaleLowerCase().includes(term))));
  const first = visible.find(track=>track.available!==false&&!(onlineTrack(track)&&musicMembershipBlocked(library,track.membershipBadge)));
  const favoriteCover = (view.nav==='favorites'||collection?.isLiked===true) &&
    (tracks[0] || collection);
  const firstIndex = state.queue.findIndex(track=>track.id===first?.id);
  const local=isLocal&&Array.isArray(library.localTracks);
  const loading=isLocal?library.localLoading:library.loading;
  const error=isLocal?library.localError:library.error;
  const offset=(isLocal?library.localOffset:library.offset)||0;
  const limit=isLocal?library.localLimit||50:50;
  const headerArtwork = favoriteCover ? artwork(favoriteCover,'MusicPlaylistArtwork','heart') : '';
  return `${recommended.length ? recommendations(recommended,library) : `<header class="MusicSectionHeader"><h1${headerArtwork?' style="display:flex;align-items:center;gap:10px"':''}>${headerArtwork}${escapeHtml(title)}</h1><div class="MusicSongTab">歌曲<span>${visible.length}</span></div></header>`}
    <div class="MusicToolbar" data-music-home-toolbar><button class="MusicPrimaryButton" type="button" data-music-action="${localTrack(first)?'local-play':onlineTrack(first)?'online-play':'play-now'}" data-music-track="${escapeHtml(first?.id)}" data-music-index="${firstIndex}" ${!first || loading || error || !isLocal&&library.busy || firstIndex<0&&!localTrack(first)&&!onlineTrack(first) ? 'disabled' : ''}>${icon('play')}播放</button>
      <div class="MusicListSearch">${view.listSearchOpen ? `<label>${icon('search')}<input data-music-list-search maxlength="512" aria-label="搜索当前列表" placeholder="搜索当前列表" value="${escapeHtml(view.listQuery || '')}">${control('close-list-search','关闭列表搜索','close')}</label>` : `<button type="button" class="MusicTextButton" data-music-action="list-search" aria-expanded="false">${icon('search')}搜索</button>`}</div></div>
    ${loading&&!library.appending?'<p class="MusicEmptyQueue" role="status">正在加载...</p>':
      error&&!library.appending?`<div class="MusicEmptyQueue" role="status">${escapeHtml(isLocal?'本地音乐库读取失败，请重试':musicOnlineError(error))}${control(isLocal?'local-retry':'online-retry','重试','reset')}</div>`:
      trackTable(state,visible,visible.length?'':terms.join(' '),{library})}
    ${!isLocal&&library.appending?`<div class="MusicLoadMore" role="status">${error?
      `${escapeHtml(musicOnlineError(error))}${control('online-retry','重试','reset')}`:loading?'正在加载...':''}</div>`:''}
    ${local?`<nav class="MusicPagination" aria-label="歌曲分页">${control('local-previous','上一页','back',{disabled:!offset||loading})}<span>第 ${Math.floor(offset/limit)+1} 页 / 共 ${library.localTotal||0} 首</span>${control('local-more','下一页','forward',{disabled:!library.localMore||loading})}</nav>`:''}`;
}

function playerBar(state, library, options) {
  const canPlay = !!state.current && state.capabilities.canToggle;
  const liked=musicTrackLiked(library,state.current?.id);
  return `<footer class="MusicPlayerBar" data-music-session="${state.session}">
    <div class="MusicProgressLine"><input type="range" data-music-progress min="0" max="${state.timeline.duration || 1}" step="1" value="${state.timeline.position}" aria-label="播放进度" ${state.capabilities.canSeek && state.timeline.duration > 0 ? '' : 'disabled'}></div>
    <div class="MusicPlayerLayout"><div class="MusicNowPlaying">${artwork(state.current,'MusicPlayerArtwork')}<div class="MusicNowPlayingMeta"><strong>${escapeHtml(state.current?.title || '暂无播放')}</strong><span>${escapeHtml(state.current?.artist || '')}</span></div></div>
    <div class="MusicPlayerControls">${control('like',liked?'取消喜欢':'喜欢','heart',{disabled:!canLikeMusicTrack(library,state.current?.id),active:liked,extra:`aria-pressed="${liked}"`})}${control('play-mode',state.shuffle ? '随机播放（当前列表）' : state.repeat==='one' ? '单曲循环' : state.repeat==='all' ? '列表循环' : '顺序播放',state.shuffle ? 'shuffle' : state.repeat==='one' ? 'repeatOne' : state.repeat==='all' ? 'repeat' : 'ordered',{disabled:!state.current})}
      ${control('previous','上一首','previous',{disabled:!state.capabilities.canPrevious})}<button class="MusicPlayButton" type="button" data-music-action="toggle" aria-label="${state.playing ? '暂停' : '播放'}" title="${state.playing ? '暂停' : '播放'}" ${canPlay ? '' : 'disabled'}>${icon(state.playing ? 'pause' : 'play')}</button>${control('next','下一首','next',{disabled:!state.capabilities.canNext})}</div>
    <div class="MusicPlayerActions">${control('queue','播放队列','queue')}
      ${audioQualityControl(state,library)}
      ${control('immersive',options.view.immersive?'退出沉浸模式':'沉浸模式','focus',{active:options.view.immersive,extra:`aria-pressed="${!!options.view.immersive}"`})}
      <div class="MusicVolumeControl" data-music-volume-picker>${control('volume','音量',state.muted||state.volume===0?'muted':'volume',{extra:'data-music-volume-trigger aria-haspopup="dialog" aria-expanded="false" aria-controls="music-volume-popover"'})}<div id="music-volume-popover" class="MusicVolumePopover" data-music-volume-menu role="dialog" aria-label="音量" hidden><input type="range" data-music-volume aria-label="音量" min="0" max="1" step=".01" value="${state.muted?0:state.volume??1}"><span data-music-volume-value>${Math.round((state.muted?0:state.volume??1)*100)}%</span></div></div><span class="MusicTimeReadout"><span data-music-elapsed>${musicFormatTime(state.timeline.position)}</span> / <span data-music-duration>${musicFormatTime(state.timeline.duration)}</span></span></div></div></footer>`;
}

function playlistRail(state,view,library) {
  const playlists=view.railTab==='playlists';
  const pending=library.loading&&!library.appending&&(view.railSearch||playlists&&view.railPlaylist);
  const error=library.error&&!library.appending&&(view.railSearch||playlists&&view.railPlaylist);
  const songs=tracks=>trackTable(state,tracks,view.railSearch?'':view.railQuery||'',{queue:!view.railSearch&&!playlists,rail:true,library});
  const body=pending?'<p role="status">正在加载歌曲</p>':error?`<p role="status">${escapeHtml(musicOnlineError(error))}${control('online-retry','重试','reset')}</p>`:
    view.railSearch?songs(library.searchTracks||[]):!playlists?songs(state.queue):
    (library.playlists||[]).map(item=>`<section class="MusicRailCollection"><button type="button" data-music-rail-playlist="${escapeHtml(musicCollectionKey(item))}" aria-expanded="${view.railPlaylist===musicCollectionKey(item)}">${artwork(item,'MusicRailCover')}<strong>${escapeHtml(item.title)}</strong>${icon('down')}</button>${view.railPlaylist===musicCollectionKey(item)?songs(library.tracks||[]):''}</section>`).join('')||'<p>还没有歌单</p>';
  return `<div class="MusicPlaylistRail" data-music-panel="queue" data-music-rail><aside class="MusicPanel MusicQueuePanel" aria-label="歌单 / 队列">
    <header><div><h2>歌单 / 队列</h2><p>PLAYLISTS · QUEUE</p></div>${control('rail-pin','固定歌单面板','pin',{active:view.railPinned,extra:`aria-pressed="${!!view.railPinned}"`})}${control('close-panel','关闭歌单','close')}</header>
    <form class="MusicRailSearch" data-music-rail-search-form>${icon('search')}<input data-music-rail-search aria-label="搜索所有歌曲" placeholder="搜索所有歌曲" maxlength="512" value="${escapeHtml(view.railQuery||'')}">${control('rail-clear-search','清空搜索','close',{extra:view.railQuery?'':'hidden'})}</form>
    <div class="MusicRailTabs" role="tablist" aria-label="歌单 / 队列">${[['queue','当前队列'],['playlists','我的歌单']].map(([tab,label])=>`<button type="button" role="tab" data-music-rail-tab="${tab}" aria-selected="${(view.railTab||'queue')===tab}">${label}</button>`).join('')}</div>
    ${playlists?`<div class="MusicRailLibrary"><span>在线音乐歌单</span>${control('rail-refresh','刷新歌单','reset',{disabled:!library.signedIn||library.loading||library.busy})}</div>`:''}
    <div class="MusicPanelBody">${body}${library.appending&&library.loading?'<div class="MusicLoadMore" role="status">正在加载...</div>':''}${library.appending&&library.error?`<div class="MusicLoadMore" role="status">${escapeHtml(musicOnlineError(library.error))}${control('online-retry','重试','reset')}</div>`:''}</div></aside></div>`;
}

function panel(state, view, preferences, background, library) {
  if (!view.panel) return '';
  const dismiss='<button class="MusicPanelDismiss" type="button" tabindex="-1" data-music-action="close-panel" aria-label="关闭面板"></button>';
  if(view.panel==='lyrics')return dismiss+musicPlayerPanel(state,view,preferences,background);
  if(view.screen==='player')return (view.railPinned?'':dismiss)+playlistRail(state,view,library);
  return `${dismiss}<aside class="MusicPanel MusicQueuePanel" data-music-panel="queue" aria-label="播放队列">
    <header><h2>播放队列</h2>${control('clear-queue','清空播放列表','remove',{disabled:!state.queue.length})}${control('close-panel','关闭面板','close')}</header><div class="MusicPanelBody">${trackTable(state,state.queue,'',{queue:true})}</div></aside>`;
}

export function musicDesktopPage(controls={pending:true},tab='settings') {
  const status=controls.error?'桌面音乐组件操作失败，请重试':
    controls.pending?'正在保存...':controls.enabled&&!controls.running?'桌面音乐组件未运行':'';
  const body=`<div class="setting-row"><label for="music-desktop-enabled">启用组件</label>
    <button id="music-desktop-enabled" type="button" role="switch" aria-label="音乐" aria-checked="${!!controls.enabled}"
      class="switch${controls.enabled?' checked':''}" data-music-action="desktop-enabled" ${controls.pending?'disabled':''}><span></span></button></div>`;
  return `<section class="MusicDesktopSettings" data-music-root data-screen="desktop">${settingsFrame({
    title:'音乐',description:'在桌面显示 Lume 音乐的歌曲和进度，并控制播放、暂停与切歌。',
    tab,body:tab==='settings'?body:'<div class="settings-details"><h3>音乐</h3><p>桌面音乐组件</p></div>',
    status,error:!!controls.error,scope:'music-desktop',
  })}</section>`;
}

export function musicPage(input = initialMusicState(), options = {}) {
  const state = normalizeMusicState(input);
  const view = {nav:'recommendations',screen:'home',panel:'',query:'',...options.view};
  const library = options.library || {};
  if(view.screen==='desktop')return musicDesktopPage(options.desktopControls,view.desktopTab);
  const player = view.screen === 'player';
  return `<div class="MusicRoot" data-music-root data-music-home-content data-screen="${player ? 'player' : 'home'}" data-music-session="${state.session}">
    <canvas class="MusicBackground" data-music-background aria-hidden="true"></canvas>
    <input class="MusicFileInput" type="file" data-music-file-input accept="audio/*,.flac,.m4a,.ogg,.wma" multiple>
    ${player ? `<section class="MusicPlayback" data-music-drop-zone="local"><div class="MusicStage" data-music-stage aria-label="歌词"></div>${control('back-home','返回音乐','back',{extra:'data-music-return'})}<div class="MusicImmersiveHoverZone" data-music-immersive-zone aria-hidden="true"></div>${playerBar(state,library,{...options,view})}</section><div class="MusicRailHotspot" data-music-rail-hotspot aria-hidden="true"></div>` :
      `${sidebar(state,view,library)}<main class="MusicMain"><header class="MusicTopbar"><form class="MusicSearchShell" data-music-search-form role="search"><button type="submit" aria-label="搜索">${icon('search')}</button><input data-music-search type="search" maxlength="512" value="${escapeHtml(view.query)}" placeholder="搜索歌曲、歌手或歌单" aria-label="搜索歌曲、歌手或歌单">${control('clear-search','清空搜索','close',{extra:view.query?'':'hidden'})}</form></header><div class="MusicContent" data-music-drop-zone="local">${homeContent(state,view,library)}</div></main>${state.current ? control('open-player','返回播放器','forward',{extra:'data-music-open-player'}) : ''}`}
    ${control('lyrics','音乐设置','settings',{active:view.panel==='lyrics',extra:'data-music-settings-toggle'})}
    ${panel(state,view,options.preferences,options.background,library)}<span class="MusicPlayerStatus" role="status">${escapeHtml(musicPlaybackError(state.error))}</span></div>`;
}

export function updateMusicPlayback(root, state, {seeking=false} = {}) {
  const volume=root.querySelector('[data-music-volume]');
  if(volume&&root.ownerDocument.activeElement!==volume)volume.value=String(state.muted?0:state.volume??1);
  const volumeValue=root.querySelector('[data-music-volume-value]');
  if(volumeValue)volumeValue.textContent=`${Math.round(Number(volume?.value??state.volume??1)*100)}%`;
  const mute=root.querySelector('[data-music-volume-trigger]');
  if(mute) {
    mute.innerHTML=icon(state.muted||state.volume===0?'muted':'volume');
  }
  const progress = root.querySelector('[data-music-progress]');
  if (progress && !seeking) {
    const duration = state.timeline.duration;
    progress.max = String(duration || 1);
    progress.value = String(state.timeline.position);
    progress.disabled = !state.capabilities.canSeek || duration <= 0;
  }
  const position = seeking && progress ? number(progress.value) : state.timeline.position;
  if(progress) progress.style.setProperty('--progress', `${state.timeline.duration ? position/state.timeline.duration*100 : 0}%`);
  const elapsed = root.querySelector('[data-music-elapsed]');
  if (elapsed) elapsed.textContent = musicFormatTime(position);
  const duration = root.querySelector('[data-music-duration]');
  if (duration) duration.textContent = musicFormatTime(state.timeline.duration);
  const toggle = root.querySelector('[data-music-action="toggle"]');
  if (toggle) {
    const label = state.playing ? '暂停' : '播放';
    if (toggle.getAttribute('aria-label') !== label) {
      toggle.innerHTML = icon(state.playing ? 'pause' : 'play');
      toggle.setAttribute('aria-label',label);
      toggle.title = label;
    }
    toggle.disabled = !state.current || !state.capabilities.canToggle;
  }
  for (const row of root.querySelectorAll('[data-music-track-id]')) row.classList.toggle('is-current',row.dataset.musicTrackId === state.current?.id);
}
