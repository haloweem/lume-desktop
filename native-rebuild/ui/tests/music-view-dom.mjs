const musicUrl = new URL("../music.js", import.meta.url).href;

// A small renderer contract fixture keeps controller tests independent of the
// parent's visual work; native normalization and musicCommand remain real.
export const rendererModule = `
export {initialMusicState, normalizeMusicState, musicCommand} from ${JSON.stringify(musicUrl)};
export let renderCount=0, playbackCount=0, lastSeeking=false;
const escape=value=>String(value??'').replaceAll('&','&amp;').replaceAll('"','&quot;').replaceAll('<','&lt;');
export function musicPage(state,{view}) {
  renderCount++;
  const actions=['toggle','previous','next','clear-queue','open-player','back-home','queue','lyrics',
    'close-panel','play-mode','import-local','clear-search','list-search','close-list-search'];
  const navs=['recommendations','albums','favorites','recent','local','playlists'];
  const rows=state.queue.map((track,index)=>({track,index})).filter(({track})=>
    [view.search,view.listQuery].every(query=>!query || [track.title,track.artist,track.album]
      .join(' ').toLowerCase().includes(query.trim().toLowerCase())));
  return '<section data-music-root tabindex="0" data-screen="'+view.screen+'" data-music-session="'+state.session+'">'+
    navs.map(nav=>'<button data-music-nav="'+nav+'">'+nav+'</button>').join('')+
    actions.map(action=>'<button data-music-action="'+action+'">'+action+'</button>').join('')+
    '<button data-music-playlist-group="owned">Owned</button><button data-music-playlist-group="subscribed">Subscribed</button>'+
    (view.screen==='home'?'<form data-music-search-form><input type="search" data-music-search value="'+escape(view.query)+'"></form>':'')+
    (view.listSearchOpen?'<input type="search" data-music-list-search value="'+escape(view.listQuery)+'">':'')+
    '<input type="file" data-music-file-input multiple><input type="range" data-music-progress min="0" max="'+state.timeline.duration+'" value="'+state.timeline.position+'">'+
    (view.screen==='home'?'<div data-music-home-content>'+rows.map(({track,index})=>
      '<button data-music-action="play-now" data-music-index="'+index+'" data-music-session="'+state.session+'">'+escape(track.title)+'</button>').join('')+'</div>':'')+
    (view.panel?'<aside data-music-panel="'+view.panel+'">'+(view.panel==='lyrics'?'No lyrics':'Queue')+'</aside>':'')+'</section>';
}
export function updateMusicPlayback(root,state,{seeking}={}) {
  playbackCount++;lastSeeking=seeking;
  root.querySelector('[data-music-root]').dataset.playing=String(state.playing);
  if(!seeking)root.querySelectorAll('[data-music-progress]').forEach(input=>{input.value=state.timeline.position;});
}
`;
