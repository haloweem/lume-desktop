const text = value => typeof value === 'string' ? value : '';
const number = (value, fallback = 0) => Number.isFinite(Number(value)) ? Number(value) : fallback;
const membershipBadges = new Set(['qq-green-vip','qq-super-vip','netease-vip','netease-svip','kugou-vip']);
const audioQualities = new Set(['jyeffect','sky','jymaster','hires','lossless','high','standard']);

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

export function applyMusicLyrics(state, message) {
  if(message.session!==state.session || message.trackId!==state.current?.id) return state;
  return {...state,lyrics:message.lines};
}

export function normalizeMusicState(message, previous = initialMusicState()) {
  if (!message || typeof message !== 'object') return previous;
  const queue = Array.isArray(message.queue) ? message.queue.slice(0,512).map(normalizeTrack).filter(Boolean) : previous.queue;
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
    'play','pause','toggle','previous','next','seek','playNow','enqueue','insertNext','removeFromQueue','clearQueue',
    'setRepeat','setShuffle','setVolume','setMuted','setQuality']);
  if (!allowed.has(wireAction)) return null;
  const command = {version:1,scope:'music',action:wireAction};
  if (Number.isSafeInteger(state.session) && state.session > 0) command.session = state.session;
  if (wireAction === 'visibility' && typeof fields.visible === 'boolean') command.visible = fields.visible;
  else if (wireAction === 'seek' && Number.isFinite(Number(fields.position)) && Number(fields.position) >= 0)
    command.position = Number(fields.position);
  else if (wireAction === 'playNow' && Number.isSafeInteger(fields.index) && fields.index >= 0) {
    const id=fields.id ?? state.queue[fields.index]?.id;
    if(!text(id) || id.length>128)return null;
    command.index=fields.index;command.id=id;
  } else if (['loadLyrics','loadScore','enqueue','insertNext','removeFromQueue'].includes(wireAction) && text(fields.id))
    command.id = text(fields.id);
  else if (wireAction === 'setRepeat' && ['none','one','all'].includes(fields.value)) command.value = fields.value;
  else if (['setShuffle','setMuted'].includes(wireAction) && typeof fields.value === 'boolean') command.value = fields.value;
  else if (wireAction === 'setVolume' && Number.isFinite(fields.value) && fields.value>=0 && fields.value<=1)
    command.value=fields.value;
  else if (wireAction === 'setQuality' && audioQualities.has(fields.value)) command.value=fields.value;
  else if (!['open','close','snapshot','login','logout','loadHome','play','pause','toggle','previous','next','clearQueue'].includes(wireAction))
    return null;
  return command;
}

// 音符律动: the current track's melody analysis; payload validation is in bridge.js.
export function applyMusicScore(state, message) {
  if(message.session!==state.session || message.trackId!==state.current?.id) return state;
  return {...state,score:message};
}
