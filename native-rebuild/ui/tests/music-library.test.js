import test from 'node:test';
import assert from 'node:assert/strict';
import {createMusicLibrary,validMusicLibraryCommand,validMusicLibraryMessage} from '../music-library.js';
import {createMusicDesktop,validMusicDesktopMessage} from '../music-desktop.js';
import {createBridge,validHostMessage} from '../bridge.js';
import {initialState} from '../state.js';
import {widgets} from '../catalog.js';
import {card,catalog,search} from '../pages.js';

const track={id:'local-1',title:'测试歌曲',artist:'歌手',album:'',source:'',artwork:'',
  duration:120,available:true,error:''};
const page=(revision=1)=>({version:1,type:'music-library-state',revision,loaded:true,error:'',
  total:1,offset:0,limit:100,tracks:[{...track}]});
const desktop=()=>({version:1,type:'music-desktop-state',enabled:false,running:false,stopped:false,
  locked:false,material:'liquid-glass',width:320,height:152,loadStatus:'loaded',error:'',
  smtc:{registered:true,status:'ready'},result:'snapshot',saved:false});

test('本地曲库协议拒绝路径、额外字段和无界分页',()=>{
  assert.equal(validMusicLibraryCommand('page',{offset:100,limit:100}),true);
  assert.equal(validMusicLibraryCommand('page',{offset:0,limit:101}),false);
  assert.equal(validMusicLibraryCommand('play',{id:'C:/music.mp3'}),false);
  assert.equal(validMusicLibraryCommand('play',{id:'local-1',source:'secret'}),false);
  assert.equal(validHostMessage(page()),true);
  for(const field of ['source','artwork']) {
    const value=page();value.tracks[0][field]='C:/private/file.mp3';
    assert.equal(validMusicLibraryMessage(value),false);
  }
  const bad=page();bad.tracks[0].artwork='http://127.0.0.1:99999/'+ 'a'.repeat(64)+'/media.png';
  assert.equal(validMusicLibraryMessage(bad),false);
});
test('本地曲库独立分页和播放，拒绝旧快照，隐藏清除封面',()=>{
  const sent=[];
  const library=createMusicLibrary({send:(action,fields)=>{sent.push({action,...fields});return true;}});
  library.setVisible(true);library.load();
  assert.equal(library.state.pending,true);
  assert.equal(library.receive(page()),true);
  assert.equal(library.state.pending,false);
  assert.equal(library.receive(page()),false);
  assert.equal(library.play('local-1'),true);
  assert.deepEqual(sent.at(-1),{action:'play',id:'local-1'});
  library.page(100);assert.equal(sent.at(-1).offset,100);
  library.setVisible(false);
  const count=sent.length;library.setVisible(false);
  assert.equal(sent.length,count);
  library.dispose();assert.equal(library.load(),false);
});
test('离开音乐页释放本地曲库曲目元数据，重进仍可重新读取',()=>{
  const sent=[];
  const library=createMusicLibrary({send:(action,fields)=>{sent.push({action,...fields});return true;}});
  library.setVisible(true);
  library.receive(page());
  assert.equal(library.state.tracks.length,1);

  library.setVisible(false);
  library.releasePageData();
  assert.deepEqual(library.state.tracks,[]);
  assert.equal(library.state.total,0);
  assert.equal(library.state.loaded,false);

  library.setVisible(true);
  assert.equal(library.load(),true);
  assert.deepEqual(sent.map(value=>value.action),['visibility','visibility','visibility','snapshot']);
});
test('原生 metadata 显示文本和 Valid 时长范围通过桥接并结束 pending',()=>{
  const library=createMusicLibrary({send:()=>true});
  // Metadata preserves whitespace within 512 UTF-8 bytes and converts up to 1e16 ticks to seconds.
  const title='T'.repeat(509)+'\t\r\n';
  const artist='\u6b4c'.repeat(169)+'\t\r\n';
  const album='Album\nDisc\t1\r\n';
  assert.equal(Buffer.byteLength(title),512);
  assert.equal(Buffer.byteLength(artist),510);
  for(const [index,duration] of [0,86400,86400.0000001,1e16/1e7].entries()) {
    assert.equal(library.load(),true);
    assert.equal(library.state.pending,true);
    const message=page(index+1);
    message.tracks[0]={...track,title,artist,album,duration};
    assert.equal(validHostMessage(message),true);
    assert.equal(library.receive(message),true);
    assert.equal(library.state.pending,false);
    assert.equal(library.state.error,'');
    assert.deepEqual(library.state.tracks,message.tracks);
  }
  library.dispose();
});
test('显示文本仍拒绝不可用控制字符，时长仍拒绝非有限和越界值',()=>{
  const controls=Array.from({length:33},(_,index)=>index===32?127:index)
    .filter(code=>![9,10,13].includes(code));
  for(const key of ['title','artist','album'])for(const code of controls) {
    const message=page();message.tracks[0][key]='Display'+String.fromCharCode(code);
    assert.equal(validHostMessage(message),false,`${key}: control ${code}`);
  }
  for(const duration of [-1,1e9+0.1,NaN,Infinity,-Infinity,'120',null]) {
    const message=page();message.tracks[0].duration=duration;
    assert.equal(validHostMessage(message),false,`duration: ${duration}`);
  }
  const message=page();message.tracks[0].title='T'.repeat(1025);
  assert.equal(validHostMessage(message),false);
  message.tracks[0].title='\u6b4c'.repeat(171);
  assert.equal(new TextEncoder().encode(message.tracks[0].title).length,513);
  assert.equal(validHostMessage(message),false);
});
test('显示文本放行不影响 ID、source、封面和错误字段边界',()=>{
  const values={
    id:['local-fixture','local-1\n','local-'+ '1'.repeat(91)],
    source:['\n','\t','C:/private/file.mp3'],
    artwork:['music-artwork:local-1','http://127.0.0.1:1234/private.png',
      'http://127.0.0.1:65536/'+'a'.repeat(64)+'/media.png'],
    error:['\n','\t','\0'],
  };
  for(const [key,entries] of Object.entries(values))for(const value of entries) {
    const message=page();message.tracks[0][key]=value;
    assert.equal(validHostMessage(message),false,`${key}: ${JSON.stringify(value)}`);
  }
  assert.equal(validHostMessage({...page(),error:'\n'}),false);
  const message=page();
  message.tracks[0].artwork='http://127.0.0.1:65535/'+'a'.repeat(64)+'/media.png';
  assert.equal(validHostMessage(message),true);
});
test('本地曲库发送失败退出pending，实际桥接不发送无效命令',()=>{
  const library=createMusicLibrary({send:()=>false});
  assert.equal(library.load(),false);
  assert.equal(library.state.pending,false);
  const sent=[],bridge=createBridge({postMessage:value=>sent.push(value)});
  assert.equal(bridge.musicLibrary('play',{id:'local-1'}),true);
  assert.equal(bridge.musicLibrary('play',{id:'https://example.invalid'}),false);
  assert.equal(sent.length,1);
});
test('桌面开关由原生确认，失败和忙态不伪造成功',()=>{
  const sent=[],controls=createMusicDesktop({send:(action,fields)=>{sent.push({action,...fields});return true;}});
  assert.equal(validMusicDesktopMessage(desktop()),true);
  assert.equal(validHostMessage(desktop()),true);
  assert.equal(controls.receive(desktop()),true);
  controls.setEnabled(true);
  assert.equal(controls.state.enabled,false);
  assert.equal(controls.state.pending,true);
  assert.equal(controls.setEnabled(false),false);
  controls.receive({...desktop(),result:'applied',error:'save'});
  assert.equal(controls.state.pending,false);
  assert.equal(controls.state.enabled,false);
  assert.equal(controls.receive({...desktop(),path:'private'}),false);
  controls.dispose();assert.equal(controls.load(),false);
});
test('音乐组件卡片和搜索结果复用桌面控制器的开关及忙态',()=>{
  const widget=widgets.find(item=>item.id==='music');
  for(const enabled of [false,true])for(const pending of [false,true]) {
    const state={...initialState(),query:widget.title,musicDesktop:{enabled,pending}};
    for(const html of [card(widget,state,true),catalog(state,true),search(state)]) {
      const button=html.match(/<button\b[^>]*data-action="music-toggle"[^>]*>/)?.[0];
      assert.ok(button,'音乐卡片应使用可操作的开关');
      assert.ok(button.includes(`aria-checked="${enabled}"`));
      assert.ok(button.includes(`class="switch${enabled?' checked':''}"`));
      assert.equal(/\bdisabled\b/.test(button),pending);
    }
  }
});
