import test from 'node:test';
import assert from 'node:assert/strict';
import {createBridge,validHostMessage} from '../bridge.js';
import {initialMusicState,musicCommand,musicPage} from '../music.js';

const qualities=['standard','high','lossless','hires','jyeffect','sky','jymaster'];
const track=provider=>({id:`${provider}:1`,title:'Song',artist:'Artist',album:'',artwork:'',
  source:`online:${provider}:1`,duration:180});
const state=provider=>{
  const {query,nav,lyrics,score,...snapshot}=initialMusicState();
  return {...snapshot,version:1,type:'music-state',session:7,current:track(provider),queue:[track(provider)],
    audio:{requestedQuality:'lossless',quality:'high',bitrate:320000}};
};

test('quality commands are session-bound and reject invented values',()=>{
  const posted=[],bridge=createBridge({postMessage:message=>posted.push(message)});
  for(const value of qualities) {
    assert.equal(bridge.music('setQuality',{value,session:7}),true);
    assert.deepEqual(musicCommand('setQuality',{value},state('qq')),
      {version:1,scope:'music',action:'setQuality',session:7,value});
  }
  for(const value of ['',null,320,'super']) {
    assert.equal(bridge.music('setQuality',{value,session:7}),false);
    assert.equal(musicCommand('setQuality',{value},state('qq')),null);
  }
  assert.equal(musicCommand('setQuality',{value:'high',session:6},state('qq')),null);
  assert.equal(posted.length,qualities.length);
});

test('native quality metadata is optional, bounded and reports the resolved quality',()=>{
  const base=state('qq');
  assert.equal(validHostMessage(base),true);
  const {audio,...legacy}=base;
  assert.equal(validHostMessage(legacy),true);
  for(const changes of [{requestedQuality:'super'},{quality:'super'},{bitrate:-1},
    {bitrate:10000001},{bitrate:1.5},{bitrate:Infinity},{switching:'true'},{path:'private'}])
    assert.equal(validHostMessage({...base,audio:{...audio,...changes}}),false);
  assert.equal(validHostMessage({...base,audio:{...audio,quality:'',bitrate:0}}),true);
  const html=musicPage(base,{view:{screen:'player'},library:{signedIn:true}});
  assert.match(html,/data-music-quality-trigger[^>]+title="[^"]*HQ高品质[^"]*320kbps/);
  assert.match(html,/aria-checked="true" data-music-quality-option="high"/);
  assert.match(html,/SQ无损品质暂不可用/);
  assert.doesNotMatch(html,/音质未知|原生未提供音质/);
});

test('all providers report pending separately from the quality actually playing',()=>{
  for(const provider of ['qq','netease','kugou']) {
    const base=state(provider);
    const pending={...base,audio:{...base.audio,switching:true}};
    assert.equal(validHostMessage(pending),true);
    const html=musicPage(pending,{view:{screen:'player'},library:{signedIn:true}});
    assert.match(html,/aria-busy="true"[^>]*>切换中…/);
    assert.match(html,/aria-checked="true" data-music-quality-option="high"/);
    assert.match(html,/正在切换/);
    assert.doesNotMatch(html,/暂不可用/);
  }
});

test('each provider keeps the historical quality labels, order and membership images',()=>{
  const expected={
    qq:['hires','lossless','high','standard'],
    kugou:['standard','high','lossless','hires'],
    netease:['jyeffect','sky','jymaster','hires','lossless','high','standard'],
  };
  for(const [provider,values] of Object.entries(expected)) {
    const html=musicPage(state(provider),{view:{screen:'player'},library:{signedIn:true}});
    assert.deepEqual([...html.matchAll(/data-music-quality-option="([^"]+)"/g)].map(match=>match[1]),values);
    if(provider==='qq') {
      assert.match(html,/mediastyle\/lv-icon\/v14\/2x\/vip5\.png/);
      assert.match(html,/mediastyle\/lv-icon\/v14\/2x\/svip5\.png/);
    }
  }
  const local={...state('qq'),current:{...track('qq'),id:'local-1'}};
  const html=musicPage(local,{view:{screen:'player'}});
  assert.match(html,/data-music-quality-trigger[^>]+disabled>原始/);
  assert.doesNotMatch(html,/data-music-quality-option=/);
});

test('qualities and member songs follow the signed-in account tier',()=>{
  const account=(provider,...memberships)=>({signedIn:true,accounts:[{provider,userId:'1',
    memberships:memberships.map(([kind,active])=>({kind,label:kind,iconUrl:'',level:1,active}))}]});
  const options=(provider,library)=>Object.fromEntries([...musicPage(state(provider),{view:{screen:'player'},library})
    .matchAll(/data-music-quality-option="([^"]+)"([^>]*)>/g)].map(match=>[match[1],/ disabled/.test(match[2])]));
  // 黑胶VIP: everything except the SVIP-only sky/jymaster.
  assert.deepEqual(options('netease',account('netease',['netease-vip',true],['netease-svip',false])),
    {jyeffect:false,sky:true,jymaster:true,hires:false,lossless:false,high:false,standard:false});
  assert.deepEqual(options('netease',account('netease',['netease-svip',true])),
    {jyeffect:false,sky:false,jymaster:false,hires:false,lossless:false,high:false,standard:false});
  // 豪华绿钻 without SVIP: no 臻品; SVIP covers green.
  assert.deepEqual(options('qq',account('qq',['qq-green-vip',true],['qq-super-vip',false])),
    {hires:true,lossless:false,high:false,standard:false});
  assert.deepEqual(options('qq',account('qq',['qq-super-vip',true])),
    {hires:false,lossless:false,high:false,standard:false});
  assert.deepEqual(options('kugou',account('kugou',['kugou-vip',false])),
    {standard:false,high:true,lossless:true,hires:true});
  assert.match(musicPage(state('kugou'),{view:{screen:'player'},library:account('kugou',['kugou-vip',false])}),
    /data-music-quality-option="high" disabled aria-disabled="true" title="需要酷狗VIP"/);

  const song=(id,membershipBadge)=>({...track('netease'),id:`netease:${id}`,source:`online:netease:${id}`,
    ...(membershipBadge?{membershipBadge}:{})});
  const tracks=[song(1,'netease-svip'),song(2,'netease-vip'),song(3)];
  const home=library=>musicPage({...state('netease'),current:null,queue:[]},
    {view:{screen:'home',nav:'recommendations'},library:{...library,tracks}});
  const vip=home(account('netease',['netease-vip',true]));
  assert.match(vip,/data-music-track-id="netease:1"[^]*?data-music-locked="需要网易云黑胶SVIP"/);
  assert.doesNotMatch(vip,/data-music-track-id="netease:2"[^>]*>\s*<div[^>]*><button[^>]*data-music-locked/);
  assert.match(vip,/MusicPrimaryButton" type="button" data-music-action="online-play" data-music-track="netease:2"/);
  const free=home(account('netease',['netease-vip',false]));
  assert.match(free,/MusicPrimaryButton" type="button" data-music-action="online-play" data-music-track="netease:3"/);
  assert.equal([...free.matchAll(/data-music-locked=/g)].length,2);
});
