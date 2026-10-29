import test from "node:test";
import assert from "node:assert/strict";
import {
  initialMusicState,
  musicCommand,
  musicFormatTime,
  musicPage,
  normalizeMusicState,
  applyMusicLyrics,
} from "../music.js";

test("music page renders the high-fidelity shell and signed-out state", () => {
  const html = musicPage(initialMusicState());
  assert.match(html, /class="MusicRoot"/);
  assert.match(html, /data-music-home-content/);
  assert.match(html, /data-music-home-sidebar/);
  assert.match(html, /在线音乐/);
  assert.match(html, /我的音乐/);
  assert.match(html, /data-music-search/);
  assert.match(html, /data-music-nav="recommendations"/);
  assert.match(html, /data-music-nav="favorites"/);
  assert.match(html, /data-music-nav="recent"/);
  assert.match(html, /class="MusicGuest"/);
  assert.match(html, /登录，开启你的音乐/);
  assert.doesNotMatch(html, /class="MusicPlayerBar/);
});

test("music home renders supplied collections, without fabricated recommendations", () => {
  const html = musicPage({
    ...initialMusicState(),
    account: { signedIn: true },
    queue: [{ id: "track-1", title: "你的", artist: "歌手", album: "专辑", duration: 163 }],
    current: { id: "track-1", title: "你的", artist: "歌手", album: "专辑", duration: 163 },
  }, { library: { signedIn: true, recommendations: Array.from({length: 9}, (_, i) => ({id: String(i), title: `歌单 ${i}`, tracks: []})) } });
  assert.match(html, /data-music-home-recommendation-playlists/);
  assert.match(html, /MusicRecommendationCard--featured/);
  assert.match(html, /data-music-home-track-list/);
  assert.match(html, /歌名\/歌手/);
  assert.match(html, /data-music-home-toolbar/);
  assert.doesNotMatch(html, /MusicPlayerBar/);
  const empty = musicPage({...initialMusicState(), account:{signedIn:true}});
  assert.doesNotMatch(empty, /私人雷达|轻松一刻|华语新声|每日推荐/);
});

test("music recommendation greeting uses the selected account name safely", () => {
  const recommendations = [{id:"playlist-1",providerId:"qq",type:"playlist",title:"推荐",artwork:"",description:"",owned:false}];
  const accounts = [
    {provider:"netease",status:"signed-in",displayName:"别的用户"},
    {provider:"qq",status:"signed-in",displayName:"关山 <script>"},
  ];
  const render = (providerId, selectedAccounts=accounts) => musicPage(initialMusicState(), {
    library:{signedIn:true,providerId,accounts:selectedAccounts,recommendations},
  });
  assert.match(render("qq"), /Hi 关山 &lt;script&gt;，今日为你推荐/);
  assert.doesNotMatch(render("qq"), /别的用户|<script>/);
  assert.match(render("netease"), /Hi 别的用户，今日为你推荐/);
  assert.match(render("qq", [{provider:"qq",status:"signed-in",displayName:""}]), /今日为你推荐/);
  assert.doesNotMatch(render("qq", [{provider:"qq",status:"signed-in",displayName:""}]), /Hi|~/);
});

test("music page renders track, queue, lyrics, and session-bound controls", () => {
  const state = {
    ...initialMusicState(),
    pageOpen: true,
    account: { signedIn: true },
    session: 7,
    revision: 3,
    status: "playing",
    playing: true,
    currentIndex: 0,
    current: {
      id: "track-1", title: "Midnight City", artist: "M83", album: "Hurry Up",
      source: "https://static.lumedesktop.com/music/midnight.mp3", artwork: "", duration: 270,
    },
    queue: [{
      id: "track-1", title: "Midnight City", artist: "M83", album: "Hurry Up",
      source: "https://static.lumedesktop.com/music/midnight.mp3", artwork: "", duration: 270,
    }],
    timeline: { position: 173, duration: 270 },
  };
  const html = musicPage(state, { view: { screen: "player", panel: "lyrics" } });
  assert.match(html, /Midnight City/);
  assert.match(html, /data-music-action="lyrics"/);
  assert.match(html, /class="MusicPlayerPanel/);
  assert.match(html, /data-music-session="7"/);
  assert.deepEqual(musicCommand("play", {}, state), {
    version: 1, scope: "music", action: "play", session: 7,
  });
});

test("an ended native snapshot does not revive the first queue track", () => {
  const track = {id:"one", title:"One", duration:30};
  const previous = {...initialMusicState(), current:track, queue:[track], session:2, revision:4};
  const next = normalizeMusicState({...previous, current:null, status:"ready", revision:5}, previous);
  assert.equal(next.current, null);
});

test("music commands reject stale sessions and format timelines consistently", () => {
  const state = { ...initialMusicState(), session: 9 };
  assert.equal(musicCommand("play", { session: 8 }, state), null);
  assert.equal(musicFormatTime(0), "0:00");
  assert.equal(musicFormatTime(173), "2:53");
  assert.equal(musicFormatTime(Number.NaN), "--:--");
});

test('lyric payload is scoped to the current track/session and retained across progress snapshots',()=>{
  const track={id:'one',source:'C:/one.mp3',duration:30};
  const state=normalizeMusicState({...initialMusicState(),session:7,current:track,queue:[track]});
  const lines=[{startTime:0,endTime:5,fullText:'First',words:[],translation:''}];
  const message={version:1,type:'music-lyrics',session:7,trackId:'one',lines};
  const next=applyMusicLyrics(state,message);
  assert.equal(next.lyrics,lines);
  assert.equal(applyMusicLyrics(state,{...message,session:6}),state);
  assert.equal(applyMusicLyrics(state,{...message,trackId:'two'}),state);
  const progressed=normalizeMusicState({session:7,current:track,timeline:{position:4,duration:30}},next);
  assert.equal(progressed.lyrics,lines);
  const switched=normalizeMusicState({current:{...track,id:'two'}},next);
  assert.deepEqual(switched.lyrics,[]);
});
