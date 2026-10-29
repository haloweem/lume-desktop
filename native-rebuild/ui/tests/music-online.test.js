import test from 'node:test';
import assert from 'node:assert/strict';
import {createMusicOnline,validMusicOnlineCommand,validMusicOnlineMessage,musicTrackLiked,canLikeMusicTrack} from '../music-online.js';
import {createBridge,validHostMessage} from '../bridge.js';
import {musicPage,normalizeMusicState} from '../music.js';
import {musicCollectionKey} from '../music-collections.js';
const data=()=>({tracks:[],playlists:[],recommendations:[],more:false});
const state=(request,action,values={})=>({version:1,type:'music-online-state',request,provider:'netease',
  action,status:'ready',error:'',data:{...data(),...(['home','likes'].includes(action)?{likedIds:[]}: {})},...values});
const accounts=userId=>({accounts:[{provider:'netease',status:'signed-in',userId,displayName:'测试账号'}]});

test('在线消息只允许固定操作、歌曲ID和有界分页，不接受URL与凭证',()=>{
  const base={provider:'netease',request:1};
  assert.equal(validMusicOnlineCommand('playlist',{...base,id:'123',offset:0,limit:50}),true);
  for(const fields of [{...base,id:'https://evil.invalid'},{...base,id:'123',cookie:'secret'},
    {...base,id:'123',offset:-1},{...base,id:'123',limit:101}])
    assert.equal(validMusicOnlineCommand('playlist',fields),false);
  assert.equal(validMusicOnlineCommand('resolve',{...base,id:'123'}),false);
  assert.equal(validHostMessage(state(1,'home')),true);
  assert.equal(validMusicOnlineMessage(state(1,'home',{data:{...data(),cookie:'secret'}})),false);
  assert.equal(validMusicOnlineMessage(state(1,'home',{data:{...data(),tracks:[{
    id:'netease:123',title:'测试',artist:'',album:'',source:'https://secret.invalid',artwork:'',duration:1,
  }]}})),false);
  assert.equal(validMusicOnlineMessage(state(1,'home',{data:{...data(),tracks:[{
    id:'netease:123',title:'测试',artist:'',album:'',source:'online:netease:123',artwork:'',duration:1,vipRequired:'true',
  }]}})),false);
});

test('登录后读取首页，账号改变和页面隐藏丢弃旧响应',()=>{
  const sent=[],changes=[];
  const online=createMusicOnline({send:(action,fields)=>{sent.push({action,...fields});return true;},
    now:()=>10,onChange:value=>changes.push(value)});
  online.accounts(accounts('1'));
  assert.equal(sent.at(-1).action,'cancel');
  online.setVisible(true);
  assert.equal(sent.at(-1).action,'home');
  const first=sent.at(-1).request;
  online.request('search',{query:'测试'});
  assert.equal(online.receive(state(first,'home')),false);
  const search=sent.at(-1).request;
  assert.equal(online.receive(state(search,'search',{data:{...data(),more:true}})),true);
  online.next();
  assert.equal(sent.at(-1).offset,50);
  online.accounts(accounts('2'));
  assert.equal(online.receive(state(search,'search')),false);
  const current=sent.at(-1).request;
  online.setVisible(false);
  assert.equal(online.receive(state(current,'home')),false);
  assert.equal(online.request('like',{id:'netease:123',value:true}),false);
  assert.ok(changes.length);
  online.dispose();
});

test('同步恢复正常后自动重载歌单',()=>{
  const sent=[];
  const online=createMusicOnline({send:(action,fields)=>{sent.push({action,...fields});return true;},now:()=>1});
  const signed={accounts:[{provider:'netease',status:'signed-in',userId:'1',displayName:'账号'}]};
  online.accounts(signed);online.setVisible(true);
  const home=sent.at(-1);
  assert.equal(home.action,'home');
  online.receive(state(home.request,'home',{status:'error',error:'network'}));
  assert.equal(online.snapshot().error,'network');
  const homes=()=>sent.filter(value=>value.action==='home').length;
  assert.equal(homes(),1);
  // A failed sync keeps the same identity: no request is needed until it recovers.
  online.accounts({accounts:[{provider:'netease',status:'error',userId:'1',displayName:'账号'}]});
  assert.equal(homes(),1);
  online.accounts(signed);
  assert.equal(homes(),2,'a recovered sync reloads the page data');
  assert.equal(online.snapshot().error,'');
  online.dispose();
});

const cacheTrack=(id,title)=>({id:`netease:${id}`,title,artist:'',album:'',source:`online:netease:${id}`,artwork:'',duration:100});
const cachePlaylist=(id,title)=>({id:`netease:${id}`,providerId:'netease',type:'playlist',title,artwork:'',owned:true,description:''});
const cacheHome=(id,title)=>({tracks:[cacheTrack(id,title)],playlists:[cachePlaylist(`p${id}`,`List ${title}`)],
  recommendations:[],more:false,likedIds:[]});

test('切换页面复用已加载的歌单和歌曲，不重新请求',()=>{
  const sent=[];
  const online=createMusicOnline({cache:true,send:(action,fields)=>{sent.push({action,...fields});return true;},now:()=>100});
  const signed={accounts:[{provider:'netease',status:'signed-in',userId:'1',displayName:'账号'}]};
  online.accounts(signed);online.setVisible(true);
  const home=sent.at(-1);
  assert.equal(home.action,'home');
  assert.equal(online.receive(state(home.request,'home',{data:cacheHome(42,'Home')})),true);
  assert.equal(online.snapshot().tracks[0].title,'Home');
  const homes=()=>sent.filter(value=>value.action==='home').length;
  assert.equal(homes(),1);
  // Closing and reopening the same music page reuses the page without a request.
  online.setVisible(false);online.setVisible(true);
  assert.equal(homes(),1);
  assert.equal(online.snapshot().loading,false);
  assert.equal(online.snapshot().tracks[0].title,'Home');
  online.request('favorites');
  const favorites=sent.at(-1);
  online.receive(state(favorites.request,'favorites',{data:{...data(),tracks:[cacheTrack(7,'Liked')]}}));
  // Another page is displayed now; returning home shows the cache immediately
  // and refreshes the native displayed queue in the background.
  online.request('home');
  assert.equal(online.snapshot().tracks[0].title,'Home');
  assert.equal(online.snapshot().loading,false);
  assert.ok(homes()>=2);
  online.request('playlist',{id:'9'});
  const playlist=sent.at(-1);
  online.receive(state(playlist.request,'playlist',{data:{...data(),tracks:[cacheTrack(9,'Nine')]}}));
  const playlists=()=>sent.filter(value=>value.action==='playlist').length;
  online.request('playlist',{id:'9'});
  assert.equal(playlists(),1);
  assert.equal(online.snapshot().tracks[0].title,'Nine');
  online.request('playlist',{id:'10'});
  assert.equal(playlists(),2);
  online.receive(state(sent.at(-1).request,'playlist',{data:{...data(),tracks:[cacheTrack(10,'Ten')]}}));
  // An explicit refresh must still hit the platform.
  const beforeRefresh=homes();
  online.refresh();
  assert.equal(homes(),beforeRefresh+1);
  online.dispose();
});

test('重启后先显示缓存，再静默刷新',()=>{
  const store=new Map();
  const previousStorage=globalThis.localStorage;
  Object.defineProperty(globalThis,'localStorage',{configurable:true,value:{
    getItem:key=>store.has(key)?store.get(key):null,
    setItem:(key,value)=>store.set(key,String(value)),
    removeItem:key=>store.delete(key),
  }});
  try{
    const signed={accounts:[{provider:'netease',status:'signed-in',userId:'1',displayName:'账号'}]};
    const first=[];
    let online=createMusicOnline({cache:true,now:()=>100,
      send:(action,fields)=>{first.push({action,...fields});return true;}});
    online.accounts(signed);online.setVisible(true);
    const home=first.at(-1);
    online.receive(state(home.request,'home',{data:cacheHome(42,'Cached')}));
    online.dispose();
    const second=[];
    online=createMusicOnline({cache:true,now:()=>200,
      send:(action,fields)=>{second.push({action,...fields});return true;}});
    online.accounts(signed);online.setVisible(true);
    assert.equal(online.snapshot().tracks[0].title,'Cached');
    const refresh=second.find(value=>value.action==='home');
    assert.ok(refresh,'a disk cache still refreshes and registers its tracks');
    // Playback waits for that registration instead of racing it.
    assert.equal(online.request('play',{id:'netease:42'}),true);
    assert.equal(second.some(value=>value.action==='play'),false);
    online.receive(state(refresh.request,'home',{data:cacheHome(42,'Fresh')}));
    assert.equal(online.snapshot().tracks[0].title,'Fresh');
    assert.equal(second.some(value=>value.action==='play'),true);
    online.dispose();
  } finally {
    if(previousStorage===undefined)delete globalThis.localStorage;
    else Object.defineProperty(globalThis,'localStorage',{configurable:true,value:previousStorage});
  }
});

test('账号快照不会把失败的搜索替换成首页',()=>{
  const sent=[];
  const online=createMusicOnline({send:(action,fields)=>{sent.push({action,...fields});return true;},now:()=>1});
  const signed={accounts:[{provider:'netease',status:'signed-in',userId:'1',displayName:'账号'}]};
  online.accounts(signed);online.setVisible(true);
  const home=sent.at(-1);
  online.receive(state(home.request,'home'));
  online.request('search',{query:'测试'});
  const search=sent.at(-1);
  assert.equal(search.action,'search');
  online.receive(state(search.request,'search',{status:'error',error:'network'}));
  assert.equal(online.snapshot().error,'network');
  const homes=()=>sent.filter(value=>value.action==='home').length;
  const before=homes();
  // Neither an unchanged snapshot nor a status recovery may replace the failed
  // search with a fresh home page.
  online.accounts(signed);
  online.accounts({accounts:[{provider:'netease',status:'error',userId:'1',displayName:'账号'}]});
  online.accounts(signed);
  assert.equal(homes(),before);
  assert.equal(online.snapshot().error,'network');
  online.dispose();
});

test('线上读取失败显示错误，不把失败当成空曲库成功',()=>{
  const sent=[];
  const online=createMusicOnline({send:(action,fields)=>{sent.push({action,...fields});return true;},now:()=>1});
  online.accounts(accounts('1'));online.setVisible(true);
  online.receive(state(sent.at(-1).request,'home',{status:'error',error:'expired'}));
  assert.equal(online.snapshot().error,'expired');
  assert.equal(online.snapshot().loading,false);
  online.retry();
  assert.equal(sent.at(-1).action,'home');
});

function fixture(options={}) {
  const sent=[],changes=[],errors=[];
  const online=createMusicOnline({now:()=>10,onChange:value=>changes.push(value),onError:value=>errors.push(value),
    send:(action,fields)=>{sent.push({action,...fields});return true;},...options});
  online.accounts(accounts('fixture-user'));online.setVisible(true);
  const reply=(values={})=>{
    const {request,action,provider}=sent.at(-1);
    return online.receive(state(request,action,{provider,...values}));
  };
  reply();
  return {online,sent,changes,errors,reply};
}

test('写请求在发送前锁定，读取、播放与重复写入不能覆盖在途请求',()=>{
  const f=fixture();
  assert.equal(f.online.request('like',{id:'netease:123',value:true}),true);
  const write=f.sent.at(-1),count=f.sent.length;
  assert.equal(f.online.snapshot().loading,true);
  assert.equal(f.online.snapshot().busy,true);
  for(const [action,fields] of [['home',{}],['play',{id:'netease:123'}],
    ['like',{id:'netease:123',value:false}],['createPlaylist',{title:'重复'}]])
    assert.equal(f.online.request(action,fields),false);
  assert.equal(f.sent.length,count);
  assert.equal(f.online.receive(state(write.request,'home')),false,'操作也必须匹配');
  f.reply({status:'error',error:'busy'});
  assert.equal(f.online.snapshot().busy,false);
  assert.equal(f.online.request('home'),true);
});

test('终态只消费一次，失败发送和账号切换后的迟到响应不能改变曲库',()=>{
  const f=fixture();
  f.online.request('search',{query:'测试'});
  const search=f.sent.at(-1);
  f.reply();
  assert.equal(f.online.receive(state(search.request,search.action,{status:'loading'})),false);
  assert.equal(f.online.snapshot().loading,false);
  f.online.request('like',{id:'netease:123',value:true});
  const write=f.sent.at(-1);
  f.online.accounts(accounts('different-user'));
  assert.equal(f.online.receive(state(write.request,write.action)),false);
  assert.equal(f.sent.at(-1).action,'home');
  assert.equal(f.online.snapshot().busy,false);
  assert.deepEqual(f.online.snapshot().liked,{});
});

test('其他平台账号变化也通知视图，切换只取消一次，退出后同步平台',()=>{
  const f=fixture(),count=f.sent.length,changes=f.changes.length;
  const multi={accounts:[...accounts('fixture-user').accounts,
    {provider:'qq',status:'signed-in',userId:'qq-fixture'}]};
  f.online.accounts(multi);
  assert.equal(f.changes.length,changes+1);
  assert.equal(f.sent.length,count+1);
  assert.equal(f.sent.at(-1).action,'likes','新增平台只同步红心，不刷新当前列表');
  assert.equal(f.sent.at(-1).provider,'qq');
  assert.equal(f.online.select('qq'),true);
  assert.deepEqual(f.sent.slice(count+1).map(value=>[value.action,value.provider]),[['cancel','netease'],['home','qq']]);
  assert.equal(f.online.snapshot().providerId,'qq');
  const old=f.sent.at(-1);
  f.online.accounts(accounts('fixture-user'));
  assert.equal(f.online.snapshot().providerId,'netease');
  assert.equal(f.online.receive(state(old.request,old.action,{provider:'qq'})),false);
  f.online.select('kugou');
  assert.equal(f.online.snapshot().providerId,'kugou');
  assert.equal(f.online.snapshot().signedIn,false);
});

test('反复隐藏不重复取消，恢复重读且销毁后不再发送',()=>{
  const f=fixture();
  f.online.request('search',{query:'测试'});
  const old=f.sent.at(-1),count=f.sent.length;
  f.online.setVisible(false);f.online.setVisible(false);f.online.setVisible(false);
  assert.equal(f.sent.length,count+1);
  assert.equal(f.online.snapshot().loading,false);
  assert.equal(f.changes.at(-1).loading,false);
  assert.equal(f.online.receive(state(old.request,old.action)),false);
  f.online.setVisible(true);f.online.setVisible(true);
  assert.equal(f.sent.length,count+2);
  assert.equal(f.sent.at(-1).action,'search');
  f.online.dispose();
  const end=f.sent.length;
  f.online.accounts(accounts('late-user'));f.online.select('qq');f.online.setVisible(true);
  assert.equal(f.sent.length,end);
});

test('离开音乐页释放在线浏览缓存，重进从首页重新加载',()=>{
  const f=fixture();
  f.online.request('search',{query:'测试'});
  f.reply({data:{...data(),tracks:[{
    id:'netease:fixture',title:'缓存曲目',artist:'',album:'',source:'online:netease:fixture',artwork:'',duration:1,
  }],likedIds:['netease:fixture']}});
  assert.equal(f.online.snapshot().tracks.length,1);

  f.online.setVisible(false);
  f.online.releasePageData();
  const released=f.online.snapshot();
  assert.equal(released.tracks.length,0);
  assert.equal(released.searchTracks.length,0);
  assert.equal(released.favorites.length,0);
  assert.equal(released.playlists.length,0);
  assert.equal(released.recommendations.length,0);
  assert.deepEqual(released.liked,{});
  assert.equal(released.accounts[0].userId,'fixture-user','account identity remains available');

  f.online.setVisible(true);
  assert.equal(f.sent.at(-1).action,'home');
});

test('搜索页新建歌单先刷新首页歌单再恢复搜索分页',()=>{
  const f=fixture();
  f.online.request('search',{query:'测试',offset:50});f.reply();
  f.online.request('createPlaylist',{title:'新歌单'});f.reply();
  assert.equal(f.sent.at(-1).action,'home');
  const playlist={id:'new-list',providerId:'netease',type:'playlist',title:'新歌单',artwork:'',description:'',owned:true};
  f.reply({data:{...data(),playlists:[playlist]}});
  assert.deepEqual(f.online.snapshot().playlists,[playlist]);
  assert.equal(f.sent.at(-1).action,'search');
  assert.equal(f.sent.at(-1).query,'测试');
  assert.equal(f.sent.at(-1).offset,50);
  f.reply();
  assert.equal(f.online.snapshot().loading,false);
});

test('喜欢成功记住确认值，不依赖尚未加载的收藏列表',()=>{
  const f=fixture();
  f.online.request('like',{id:'netease:123',value:true});
  f.reply({data:{...data(),liked:true}});
  assert.equal(f.online.snapshot().liked['netease:123'],true);
  f.reply();
  f.online.request('like',{id:'netease:123',value:false});
  f.reply({data:{...data(),liked:false}});
  assert.equal(f.online.snapshot().liked['netease:123'],false);
});

for(const provider of ['netease','qq','kugou']) {
  test(`${provider}: playlist likes survive an interrupted home read and refresh removes stale hearts`,()=>{
    const f=fixture(),id=`${provider}:123`;
    const track={id,title:'Liked song',artist:'',album:'',artwork:'',duration:180,source:`online:${id}`};
    f.online.accounts({accounts:[{provider,status:'signed-in',userId:'liked-user'}]});
    const home=f.sent.at(-1);
    f.online.request('playlist',{id:'liked-list'});
    f.reply({data:{...data(),tracks:[track],likedIds:[id]}});
    assert.equal(f.online.receive(state(home.request,home.action,{provider,data:{...data(),likedIds:[]}})),false);
    assert.equal(musicTrackLiked(f.online.snapshot(),id),true);
    assert.equal(canLikeMusicTrack(f.online.snapshot(),id),true);
    const musicState=normalizeMusicState({current:track,queue:[track]});
    const listPage=musicPage(musicState,{library:f.online.snapshot()});
    const listHeart=listPage.match(/<button\b[^>]*data-music-action="like"[^>]*>/)?.[0]||'';
    assert.ok(listHeart.includes('aria-pressed="true"')&&!listHeart.includes('disabled'));
    const playerPage=musicPage(musicState,{view:{screen:'player'},library:f.online.snapshot()});
    const playerHeart=playerPage.match(/<button\b[^>]*data-music-action="like"[^>]*>/)?.[0]||'';
    assert.ok(playerHeart.includes('aria-pressed="true"')&&!playerHeart.includes('disabled'));
    f.online.request('favorites');
    f.reply({data:{...data(),tracks:[track]}});
    assert.equal(f.online.snapshot().favorites.length,1);
    f.online.request('search',{query:'song'});
    f.reply({data:{...data(),tracks:[track],likedIds:[]}});
    assert.equal(musicTrackLiked(f.online.snapshot(),id),false);
    assert.equal(f.online.snapshot().favorites.length,0,'old favorite rows cannot restore a removed heart');
    f.online.request('like',{id,value:true});f.reply({data:{...data(),liked:true}});
    f.online.request('playlist',{id:'unrelated-title'});
    f.reply({data:{...data(),tracks:[track]}});
    assert.equal(musicTrackLiked(f.online.snapshot(),id),true,'a failed optional snapshot keeps known likes');
    f.online.request('like',{id,value:false});f.reply({status:'error',error:'network'});
    assert.equal(musicTrackLiked(f.online.snapshot(),id),true,'failed writes do not clear hearts');
    f.online.request('like',{id,value:false});f.reply({data:{...data(),liked:false}});
    assert.equal(musicTrackLiked(f.online.snapshot(),id),false);
  });
}

test('likes remain per signed-in account across providers and player writes target the song provider',()=>{
  const f=fixture();
  const users=['netease','qq','kugou'].map(provider=>({provider,status:'signed-in',userId:`${provider}-user`}));
  f.online.accounts({accounts:users});
  for(const provider of ['netease','qq','kugou']) {
    f.online.select(provider);
    f.online.request('home');
    f.reply({data:{...data(),likedIds:[`${provider}:123`]}});
  }
  assert.deepEqual(f.online.snapshot().liked,{'netease:123':true,'qq:123':true,'kugou:123':true});
  assert.equal(f.online.request('like',{id:'qq:123',value:false}),true);
  assert.equal(f.sent.at(-1).provider,'qq');
  f.reply({data:{...data(),liked:false}});
  assert.equal(f.online.snapshot().providerId,'kugou','player writes do not navigate the current library');
  assert.equal(musicTrackLiked(f.online.snapshot(),'qq:123'),false);
  assert.equal(musicTrackLiked(f.online.snapshot(),'netease:123'),true);
  f.online.request('like',{id:'qq:123',value:true});
  const late=f.sent.at(-1);
  f.online.accounts({accounts:users.map(account=>account.provider==='qq'?{...account,userId:'replacement'}:account)});
  assert.equal(f.online.receive(state(late.request,'like',{provider:'qq',data:{...data(),liked:true}})),false);
  assert.equal(f.online.snapshot().busy,false);
  assert.equal(Object.hasOwn(f.online.snapshot().liked,'qq:123'),false);
  assert.equal(musicTrackLiked(f.online.snapshot(),'kugou:123'),true);
  f.online.accounts({accounts:users.filter(account=>account.provider!=='qq')});
  assert.equal(canLikeMusicTrack(f.online.snapshot(),'qq:123'),false);
  assert.equal(f.online.request('like',{id:'qq:123',value:true}),false);
});

test('cold-start queue likes warm all signed-in providers without replacing the visible list',()=>{
  const f=fixture();
  const users=['netease','qq','kugou'].map(provider=>({provider,status:'signed-in',userId:`${provider}-user`}));
  f.online.accounts({accounts:users});
  f.reply({data:{...data(),likedIds:['netease:1']}});
  assert.equal(f.sent.at(-1).action,'likes');
  assert.equal(f.sent.at(-1).provider,'qq');
  assert.equal(f.online.snapshot().loading,false);
  assert.equal(canLikeMusicTrack(f.online.snapshot(),'qq:123'),false,'unknown is not unliked');
  assert.equal(f.online.request('like',{id:'qq:123',value:true}),false);
  const tracks=f.online.snapshot().tracks;
  f.reply({data:{...data(),likedIds:['qq:123']}});
  assert.equal(f.online.snapshot().providerId,'netease');
  assert.equal(f.online.snapshot().tracks,tracks);
  assert.equal(musicTrackLiked(f.online.snapshot(),'qq:123'),true);
  assert.equal(canLikeMusicTrack(f.online.snapshot(),'qq:123'),true);
  assert.equal(f.sent.at(-1).provider,'kugou');
  f.reply({data:{...data(),likedIds:[]}});
  assert.equal(canLikeMusicTrack(f.online.snapshot(),'kugou:456'),true,'verified empty snapshot enables liking');
  f.online.request('like',{id:'qq:123',value:!musicTrackLiked(f.online.snapshot(),'qq:123')});
  assert.equal(f.sent.at(-1).value,false,'first queue click unlikes an already liked song');
  const old=f.sent.at(-1);
  f.online.accounts({accounts:users.map(account=>account.provider==='qq'?{...account,userId:'other'}:account)});
  assert.equal(canLikeMusicTrack(f.online.snapshot(),'qq:123'),false);
  assert.equal(f.online.receive(state(old.request,'like',{provider:'qq'})),false);
  assert.equal(f.sent.at(-1).action,'likes');
});

test('background likes are preemptible, isolated on failure and retried by explicit refresh',()=>{
  const f=fixture();
  f.online.accounts({accounts:[...accounts('fixture-user').accounts,{provider:'qq',userId:'q',status:'signed-in'}]});
  const old=f.sent.at(-1);
  f.online.request('search',{query:'foreground'});
  assert.equal(f.online.receive(state(old.request,'likes',{provider:'qq'})),false);
  f.reply();
  assert.equal(f.sent.at(-1).action,'likes');
  f.reply({status:'error',error:'network'});
  assert.equal(f.online.snapshot().error,'');
  assert.equal(f.online.snapshot().loading,false);
  assert.equal(canLikeMusicTrack(f.online.snapshot(),'qq:123'),false);
  const count=f.sent.length;
  f.online.accounts({accounts:f.online.snapshot().accounts});
  assert.equal(f.sent.length,count,'failed background requests do not loop');
  f.online.refresh();
  f.reply();
  f.reply();
  assert.equal(f.sent.at(-1).action,'likes');
  assert.equal(f.sent.at(-1).provider,'qq');
});

test('a provider snapshot cannot inject likes into another account or use bare IDs',()=>{
  for(const likedIds of [['qq:123'],['netease:'],['__proto__']])
    assert.equal(validMusicOnlineMessage(state(1,'playlist',{data:{...data(),likedIds}})),false);
  assert.equal(validMusicOnlineMessage(state(1,'playlist',{data:{...data(),likedIds:['123']}})),true);
  assert.equal(validMusicOnlineMessage(state(1,'playlist',{data:{...data(),likedIds:['netease:123']}})),true);
});

test('平台裸歌曲 ID 和 likedIds 归一化后与展示歌曲保持一致',()=>{
  for(const provider of ['netease','qq','kugou']) {
    const f=fixture();
    f.online.accounts({accounts:[{provider,status:'signed-in',userId:`${provider}-user`}]});
    f.online.select(provider);
    const id=`${provider}-bare`;
    const track={id:id,title:'裸 ID 歌曲',artist:'Fixture',album:'',artwork:'',duration:180,source:`online:${id}`};
    f.reply({data:{...data(),tracks:[track],likedIds:[id]}});
    assert.equal(f.online.snapshot().tracks[0].id,`${provider}:${id}`);
    assert.equal(musicTrackLiked(f.online.snapshot(),`${provider}:${id}`),true);
  }
});

test('a like response also reconciles the complete provider snapshot',()=>{
  const f=fixture();
  f.online.request('home');
  f.reply({data:{...data(),likedIds:['netease:old']}});
  f.online.request('like',{id:'netease:new',value:true});
  f.reply({data:{...data(),liked:true,likedIds:['netease:new','netease:other']}});
  assert.equal(musicTrackLiked(f.online.snapshot(),'netease:old'),false);
  assert.equal(musicTrackLiked(f.online.snapshot(),'netease:new'),true);
  assert.equal(musicTrackLiked(f.online.snapshot(),'netease:other'),true);
});

test('同步回包与发送失败不会留下忙状态或接受迟到成功',()=>{
  const sent=[];
  let online,reject=false;
  online=createMusicOnline({now:()=>1,send:(action,fields)=>{
    sent.push({action,...fields});
    if(reject)return false;
    if(action!=='cancel')online.receive(state(fields.request,action));
    return true;
  }});
  online.accounts(accounts('fixture'));online.setVisible(true);
  assert.equal(online.snapshot().loading,false);
  online.request('createPlaylist',{title:'同步'});
  assert.equal(online.snapshot().loading,false);
  reject=true;
  assert.equal(online.request('like',{id:'netease:123',value:true}),false);
  assert.equal(online.snapshot().loading,false);
  assert.equal(online.snapshot().busy,false);
  assert.equal(online.receive(state(sent.at(-1).request,'like')),false);
});

test('本地封面只接受现有协议的有界 loopback 租约，不接受任意 HTTP 文件',()=>{
  const cover=`http://127.0.0.1:12345/${'a'.repeat(64)}/media.png`;
  const render=artwork=>musicPage(undefined,{view:{nav:'local'},library:{localTracks:[{
    id:'local-123',source:'',title:'Fixture',artist:'',album:'',duration:1,artwork,
  }]}});
  assert.ok(render(cover).includes(`src="${cover}"`));
  for(const invalid of [cover.replace('127.0.0.1','localhost'),cover+'?file=secret',
    cover.replace('12345','65536'),cover.replace('/media.png','/secret.png'),'http://evil.invalid/cover.png',
    'https://lume-music-artwork.local/cover.png'])
    assert.equal(render(invalid).includes('data-music-image'),false);
});

test('三平台常见封面 CDN URL 都能安全渲染',()=>{
  const covers=[
    'https://p5.music.126.net/cover.jpg',
    'https://qpic.y.qq.com/music_cover/cover.jpg',
    'https://imgcache.qq.com/music/cover.jpg',
    'https://img.kgimg.com/stdmusic/cover.jpg',
    'https://cdn.kugou.com/cover.jpg',
  ];
  for(const artwork of covers) {
    const html=musicPage(undefined,{view:{nav:'local'},library:{localTracks:[{
      id:'local-cover',source:'',title:'封面',artist:'',album:'',duration:1,artwork,
    }]}});
    assert.match(html,new RegExp(`src="${artwork.replaceAll('.','\\.').replaceAll('/','\\/')}"`));
  }
});

test('喜欢页使用列表首首歌曲封面作为页头回退',()=>{
  const first={id:'netease:first',source:'online:netease:first',title:'第一首',artist:'',album:'',duration:1,
    artwork:'https://p5.music.126.net/first.jpg'};
  const html=musicPage(undefined,{view:{nav:'favorites'},library:{signedIn:true,favorites:[first]}});
  assert.match(html,/class="MusicSectionHeader"[\s\S]*class="MusicPlaylistArtwork"[\s\S]*src="https:\/\/p5\.music\.126\.net\/first\.jpg"/);
});

test('QQ 201 侧栏歌单的真实曲目确认红心，完整快照失败不影响已知喜欢',()=>{
  const f=fixture();
  f.online.accounts({accounts:[{provider:'qq',status:'signed-in',userId:'qq-fixture'}]});
  const liked={id:'901',providerId:'qq',type:'playlist',title:'重命名的喜欢歌单',artwork:'',
    description:'',owned:true,isLiked:true};
  const ordinary={...liked,id:'902',title:'我喜欢',isLiked:undefined};
  delete ordinary.isLiked;
  f.reply({data:{...data(),playlists:[liked,ordinary]}});
  const home=f.sent.at(-1);
  const tracks=Array.from({length:50},(_,index)=>({id:`qq:favorite${index}`,title:`Liked ${index}`,
    artist:'Fixture',album:'Album',duration:180,source:`online:qq:favorite${index}`,
    artwork:`https://qpic.y.qq.com/music_cover/first${index}/600`}));
  f.online.request('playlist',{id:'901'});
  f.reply({data:{...data(),tracks,more:true}});
  assert.equal(f.online.snapshot().favorites.length,50);
  assert.ok(tracks.every(track=>musicTrackLiked(f.online.snapshot(),track.id)&&
    canLikeMusicTrack(f.online.snapshot(),track.id)));
  assert.equal(f.online.snapshot().likedReady.qq,undefined,'a known liked page is not a complete account snapshot');
  const html=musicPage(undefined,{view:{nav:'playlists',playlistId:musicCollectionKey(liked)},
    library:f.online.snapshot()});
  assert.equal((html.match(/data-music-action="like"[^>]*aria-pressed="true"/g)||[]).length,50);
  const sidebar=html.match(/<aside class="MusicSidebar"[\s\S]*?<\/aside>/)?.[0]||'';
  const heading=html.match(/<header class="MusicSectionHeader">[\s\S]*?<\/header>/)?.[0]||'';
  assert.match(sidebar,/src="https:\/\/qpic\.y\.qq\.com\/music_cover\/first0\/600"/);
  assert.match(heading,/src="https:\/\/qpic\.y\.qq\.com\/music_cover\/first0\/600"/);
  const filtered=musicPage(undefined,{view:{nav:'playlists',playlistId:musicCollectionKey(liked),listQuery:'Liked 1'},
    library:f.online.snapshot()});
  assert.match(filtered.match(/<header class="MusicSectionHeader">[\s\S]*?<\/header>/)?.[0]||'',
    /src="https:\/\/qpic\.y\.qq\.com\/music_cover\/first0\/600"/,
    'searching the list does not change the collection cover');
  f.online.next();
  const next={...tracks[0],id:'qq:page2',source:'online:qq:page2'};
  f.reply({data:{...data(),tracks:[next]}});
  assert.equal(f.online.snapshot().favorites.length,51);
  assert.equal(musicTrackLiked(f.online.snapshot(),next.id),true);
  f.online.request('like',{id:next.id,value:false});
  assert.equal(f.sent.at(-1).provider,'qq');
  assert.equal(f.sent.at(-1).value,false,'red heart issues unlike, never another like');
  f.reply({data:{...data(),liked:false}});
  assert.equal(musicTrackLiked(f.online.snapshot(),next.id),false);
  f.online.request('playlist',{id:'902'});
  const unliked={...tracks[0],id:'qq:ordinary',source:'online:qq:ordinary'};
  f.reply({data:{...data(),tracks:[unliked]}});
  assert.equal(musicTrackLiked(f.online.snapshot(),unliked.id),false,'a title alone does not identify QQ favorites');
  assert.equal(f.online.receive(state(home.request,home.action,{provider:'qq',data:{...data(),likedIds:[]}})),false);
  const player=musicPage(normalizeMusicState({current:tracks[0],queue:tracks}),{
    view:{screen:'player'},library:f.online.snapshot()});
  assert.match(player, /data-music-action="like"[^>]*aria-pressed="true"/);
  f.online.accounts({accounts:[{provider:'qq',status:'signed-in',userId:'replacement'}]});
  assert.equal(musicTrackLiked(f.online.snapshot(),tracks[0].id),false,'a replacement account cannot inherit known likes');
});

test('QQ 喜欢歌单标记必须来自自建歌单的布尔状态',()=>{
  const item={id:'901',providerId:'qq',type:'playlist',title:'Likes',artwork:'',description:'',owned:true,isLiked:true};
  const message=value=>state(1,'home',{provider:'qq',data:{...data(),playlists:[value]}});
  assert.equal(validHostMessage(message(item)),true);
  for(const invalid of [{...item,owned:false},{...item,isLiked:'true'},{...item,providerId:'netease'}])
    assert.equal(validMusicOnlineMessage(message(invalid)),false);
});

test('QQ 旧版 HTTP 或协议相对封面规范化为 HTTPS，非法来源仍拒绝',()=>{
  const artwork='https://qpic.y.qq.com/music_cover/fixture/600';
  const track={id:'qq:cover',title:'Cover',artist:'',album:'',duration:1,source:'online:qq:cover'};
  for(const input of [artwork,artwork.replace('https:','http:'),artwork.replace('https:','')]) {
    const page=musicPage(undefined,{library:{signedIn:true,tracks:[{...track,artwork:input}]}});
    assert.ok(page.includes(`src="${artwork}"`));
  }
  for(const input of ['http://evil.invalid/cover.png','//127.0.0.1/cover.png','http://qpic.y.qq.com.evil.invalid/cover.png',
    'https://secret@qpic.y.qq.com/cover.png','https://qpic.y.qq.com:444/cover.png']) {
    const page=musicPage(undefined,{library:{signedIn:true,tracks:[{...track,artwork:input}]}});
    assert.equal(page.includes('data-music-image'),false);
  }
});

test('原生公开曲目不含 source，归一化保留 ID 且在线、本地按 ID 路由',()=>{
  const online={id:'qq:fixture',title:'Online fixture',artist:'',album:'',artwork:'',duration:180,vipRequired:true};
  const local={...online,id:'local-fixture',title:'Local fixture'};
  const snapshot=normalizeMusicState({current:online,queue:[online,local]});
  assert.equal(snapshot.current.id,online.id);
  assert.deepEqual(snapshot.queue.map(track=>track.id),[online.id,local.id]);
  assert.equal(snapshot.current.source,'');
  const onlinePage=musicPage(snapshot,{library:{signedIn:true,tracks:[online]}});
  assert.match(onlinePage,/data-music-action="online-play"/);
  assert.match(onlinePage,/class="MusicTrackVip"/);
  const localPage=musicPage(snapshot,{view:{nav:'local'}});
  assert.match(localPage,/data-music-action="local-play"/);
  assert.equal(localPage.includes('Online fixture'),false);
});

test('历史平台会员标识和已喜欢红心使用结构化视觉状态',()=>{
  const tracks=['qq-green-vip','qq-super-vip','netease-vip','netease-svip','kugou-vip'].map((membershipBadge,index)=>({
    id:`qq:badge-${index}`,title:`Badge ${membershipBadge}`,artist:'Fixture',album:'Album',artwork:'',duration:180,
    vipRequired:true,membershipBadge,
  }));
  const page=musicPage(normalizeMusicState({queue:tracks,current:tracks[0]}),{
    library:{signedIn:true,liked:{[tracks[0].id]:true},tracks},
  });
  for(const membershipBadge of ['qq-green-vip','qq-super-vip','netease-vip','netease-svip','kugou-vip'])
    assert.match(page,new RegExp(`data-song-membership-badge="${membershipBadge}"`));
  const like = page.match(/<button\b[^>]*data-music-action="like"[^>]*>/)?.[0] || '';
  assert.match(like,/aria-pressed="true"/);
  assert.match(like,/class="[^"]*\bis-active\b[^"]*"/);
});

test('创建途中隐藏只取消一次，恢复刷新歌单但不重放写请求',()=>{
  const f=fixture();
  f.online.request('favorites');f.reply();
  f.online.request('createPlaylist',{title:'Fixture'});
  const old=f.sent.at(-1);
  f.online.setVisible(false);
  assert.equal(f.online.receive(state(old.request,'createPlaylist')),false);
  f.online.setVisible(true);
  assert.equal(f.sent.at(-1).action,'home');
  f.reply();
  assert.equal(f.sent.at(-1).action,'favorites');
  assert.equal(f.sent.filter(value=>value.action==='createPlaylist').length,1);
  f.online.setVisible(false);
  const count=f.sent.length;
  f.online.dispose();
  assert.equal(f.sent.length,count);
});

test('歌单刷新失败后重试首页，再恢复原列表，不重复创建',()=>{
  const f=fixture();
  f.online.request('playlist',{id:'old-list'});f.reply();
  f.online.request('createPlaylist',{title:'Fixture'});f.reply();
  f.reply({status:'error',error:'network'});
  f.online.retry();
  assert.equal(f.sent.at(-1).action,'home');
  f.reply();
  assert.equal(f.sent.at(-1).action,'playlist');
  assert.equal(f.sent.at(-1).id,'old-list');
  assert.equal(f.sent.filter(value=>value.action==='createPlaylist').length,1);
});
