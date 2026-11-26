'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { installProviderEnvironment } = require('../environment.cjs');
const { createFacade } = require('../facade.cjs');

test('upstream normalization and the facade retain membership markers and normalized songs', async () => {
    installProviderEnvironment({platform:'win32'});
    const {providers}=require('../../dist/providers.cjs');
    for(const [id,raw,required,badge] of [
        ['qq',{songid:123,songmid:'fixture',name:'Member song',pay:{pay_play:1}},true,'qq-green-vip'],
        ['qq',{songid:123,songmid:'fixture',name:'Member song',isSuperVip:1,pay:{pay_play:1}},true,'qq-super-vip'],
        ['qq',{songid:123,songmid:'fixture',name:'Free song',pay:{pay_play:0}},false,undefined],
        ['netease',{id:123,name:'VIP',fee:1},true,'netease-vip'],
        ['netease',{id:123,name:'SVIP',fee:1,redplus:1},true,'netease-svip'],
        ['netease',{id:123,name:'Purchased album',fee:4},true,undefined],
        ['netease',{id:123,name:'Standard free',fee:8},false,undefined],
        ['kugou',{hash:'a'.repeat(32),SongName:'VIP',Privilege:10},true,'kugou-vip'],
        ['kugou',{hash:'a'.repeat(32),SongName:'Free',Privilege:8,PayType:1},false,undefined],
    ]) {
        const song=providers[id].normalizeSong(raw);
        const again=providers[id].normalizeSong(song);
        assert.equal(again.sourceRef.providerData.lumeVipRequired,required);
        assert.equal(again.sourceRef.providerData.lumeMembershipBadge,badge);
        song.durationMs=123000;song.album={id:3,name:'Keep album',coverUrl:'https://y.gtimg.cn/fixture.jpg'};
        const facade=createFacade({providers:{[id]:{...providers[id],search:{searchSongs:async()=>({items:[song]})}}}});
        const result=(await facade.online(id,{action:'search',args:{query:'fixture'}})).tracks[0];
        assert.equal(result.vipRequired,required);assert.equal(result.membershipBadge,badge);
        assert.equal(result.duration,123);assert.equal(result.album,'Keep album');
    }
});

test('the built four-provider bundle executes Folia login, audio and lyric adapters', async () => {
    const calls = [];
    const qishuiSong = { base_info: { id: '9876543210123456789', name: 'Qishui fixture', duration_ms: 10000 },
        related_info: { artist_links: [{ id: '1', name: 'Artist' }], album_link: { id: '2', name: 'Album' } } };
    installProviderEnvironment({ platform: 'win32',
        getNeteasePort: async () => 12345, getQqPort: async () => 12346,
        kugouRequest: async () => { throw new Error('Unexpected KuGou request'); },
        qishuiRequest: async (operation, params) => {
            calls.push(['qishui', operation, params]);
            const data = operation === 'login_status' ? { id: 'fixture-user', nickname: 'Qishui user' }
                : operation === 'login_qr_key' ? { key: 'fixture-key' }
                    : operation === 'login_qr_create' ? { imageUrl: 'data:image/png;base64,AA==' }
                        : operation === 'login_qr_check' ? { state: 'confirmed' }
                            : operation === 'search' ? { items: [qishuiSong], hasMore: false, nextOffset: 1 }
                                : operation === 'check_tracks' ? { statuses: [{ id: qishuiSong.base_info.id, riskResult: 0 }] }
                                    : operation === 'audio' ? { url: 'https://media.example.test/qishui.mp3', quality: 'standard' }
                                        : operation === 'lyrics' ? { lyric_entity: { type: 'lrc', content: '[00:01.00]Qishui lyric\n[00:03.00]Next line' } }
                                            : operation === 'liked_songs' ? { ids: [], whole: true } : null;
            return { ok: true, data };
        },
    });
    const originalFetch = globalThis.fetch;
    globalThis.fetch = async url => {
        const address = new URL(url);
        assert.equal(address.origin, 'http://127.0.0.1:12345');
        calls.push(['netease', address.pathname]);
        const data = address.pathname === '/register/anonimous' ? { cookie: 'fixture-anonymous=1' }
            : address.pathname.startsWith('/lyric') ? { code: 200, lrc: { lyric: '[00:01.00]NetEase lyric\n[00:03.00]Next line' } }
                : address.pathname.includes('/chorus') ? { code: 200, chorus: [] }
                    : address.pathname.startsWith('/song/url') ? { code: 200, data: [{ url: 'https://media.example.test/netease.mp3' }] }
                        : { code: 200, result: { songs: [{ id: 12, name: 'NetEase fixture', ar: [{ id: 1, name: 'Artist' }], al: { id: 2, name: 'Album' }, dt: 10000 }], songCount: 1 } };
        return new Response(JSON.stringify(data));
    };
    try {
        const { providers } = require('../../dist/providers.cjs');
        assert.deepEqual(Object.keys(providers), ['netease', 'qq', 'kugou', 'qishui']);
        for (const provider of Object.values(providers)) assert.equal(typeof provider.auth.getLoginStatus, 'function');
        const facade = createFacade({ providers });
        const started = await facade.auth('qishui', { operation: 'begin', attempt: 1 });
        assert.equal(started.status, 'authorizing'); assert.equal(started.qrImage, 'data:image/png;base64,AA==');
        assert.equal((await facade.auth('qishui', { operation: 'poll', attempt: 1 })).status, 'signed-in');
        const qishui = await facade.online('qishui', { action: 'search', args: { query: 'fixture' } });
        assert.equal(qishui.tracks[0].id, 'qishui:9876543210123456789');
        assert.equal((await facade.online('qishui', { action: 'resolve', args: { id: qishui.tracks[0].id } })).url, 'https://media.example.test/qishui.mp3');
        assert.equal((await facade.online('qishui', { action: 'lyrics', args: { id: qishui.tracks[0].id } })).foliaLines[0].fullText, 'Qishui lyric');
        const netease = await providers.netease.search.searchSongs('fixture', 10, 0);
        assert.equal((await providers.netease.playback.getAudioSource(netease.items[0], 'high')).url, 'https://media.example.test/netease.mp3');
        const neteaseLyrics = await providers.netease.lyrics.getLyrics(netease.items[0]);
        assert.equal(neteaseLyrics.lyrics.lines[0].fullText, 'NetEase lyric');
        assert.ok(calls.some(([id, operation]) => id === 'netease' && operation.startsWith('/lyric')));
    } finally { globalThis.fetch = originalFetch; }
});
