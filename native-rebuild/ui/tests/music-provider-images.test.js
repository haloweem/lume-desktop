import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {musicProviderImage,validMusicProviderAccountMessage,MUSIC_PROVIDERS} from '../music-provider-account.js';
import {musicPage} from '../music.js';
import {normalizeMusicState} from '../music-runtime-state.js';

const qqImageHosts=['qpic.y.qq.com','y.gtimg.cn','thirdqq.qlogo.cn'];
test('QQ profile and collection image origins survive the native, account, renderer and CSP boundaries',()=>{
  const native=readFileSync(new URL('../../core/music/music_provider_account.h',import.meta.url),'utf8');
  const page=readFileSync(new URL('../index.html',import.meta.url),'utf8');
  for(const host of qqImageHosts) {
    const artwork=`https://${host}/fixture.png`;
    assert.equal(musicProviderImage(artwork),artwork);
    assert.ok(native.includes(`"${host}"`),`native origin: ${host}`);
    assert.ok(page.includes(`https://${host}`),`CSP origin: ${host}`);
    const accounts=MUSIC_PROVIDERS.map(provider=>({provider,request:1,status:'signed-in',phase:'',
      userId:'fixture',displayName:'Fixture',qrImage:'',qrText:'',error:'',avatarUrl:provider==='qq'?artwork:''}));
    assert.equal(validMusicProviderAccountMessage({version:1,type:'music-provider-account-state',revision:1,accounts}),true);
    const track={id:'qq:fixture',title:'Fixture',artist:'',album:'',source:'online:qq:fixture',duration:10,artwork};
    if(host!=='thirdqq.qlogo.cn') {
      assert.equal(normalizeMusicState({current:track}).current.artwork,artwork);
      assert.ok(musicPage(undefined,{library:{signedIn:true,provider:'qq',tracks:[track]}}).includes(`src="${artwork}"`));
    }
  }
});

test('QQ public image expansion keeps exact-host, HTTPS and port restrictions',()=>{
  for(const host of qqImageHosts) {
    for(const image of [`https://${host}.evil.invalid/a.png`,`https://user:password@${host}/a.png`,
      `https://${host}:444/a.png`,`http://${host}/a.png`,`//${host}/a.png`])
      assert.equal(musicProviderImage(image),'',image);
    assert.equal(musicProviderImage(`https://${host}:443/a.png`),`https://${host}/a.png`);
  }
});
