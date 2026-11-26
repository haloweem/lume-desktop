'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { createFacade } = require('../facade.cjs');
const { withRequest, guardStorage } = require('../request-context.cjs');
const { memoryStorage } = require('../environment.cjs');

test('home uses Folia recommendations while explore retains its own feed', async () => {
    const qq=fixture('qq'); let homeCalls=0, exploreCalls=0;
    const recommended={...qq.collection,id:'home',isOwned:false,coverUrl:'http://music-file.y.qq.com/fixture.jpg'};
    qq.provider.recommendations={getRecommendedCollections:async limit=>{homeCalls++;assert.equal(limit,35);return[recommended,recommended,{...recommended,id:'fm',personalFm:{}}];}};
    qq.provider.explore={getFeed:async()=>{exploreCalls++;return{sections:[{items:[{kind:'collection',collection:{...recommended,id:'explore'}}]}]};}};
    const facade=createFacade({providers:{qq:qq.provider}});
    const home=await facade.online('qq',{action:'home'});
    assert.equal(homeCalls,1);assert.equal(exploreCalls,0);
    assert.deepEqual(home.recommendations.map(item=>item.id),['qq:home']);
    assert.equal(home.recommendations[0].artwork,'https://music-file.y.qq.com/fixture.jpg');
    const explore=await facade.online('qq',{action:'explore'});
    assert.equal(homeCalls,1);assert.equal(exploreCalls,1);
    assert.equal(explore.recommendations[0].id,'qq:explore');
});

test('account badges distinguish QQ green and super membership', async () => {
    const qq=fixture('qq'), facade=createFacade({providers:{qq:qq.provider}});
    for (const [tier,kind,file] of [[1,'qq-green-vip','vip5'],[2,'qq-super-vip','svip5']]) {
        qq.state.user={id:'member',nickname:'Member',vipType:tier};
        const account=await facade.auth('qq',{operation:'refresh'});
        assert.equal(account.memberships[0].kind,kind);
        assert.equal(account.memberships[0].iconUrl,`https://y.qq.com/mediastyle/lv-icon/v14/2x/${file}.png`);
    }
    qq.state.user.vipType=0;
    assert.deepEqual((await facade.auth('qq',{operation:'refresh'})).memberships,[]);
});

function fixture(id, extra = {}) {
    const state = { user: null, key: 0, cancelled: [], calls: [] };
    const song = { id: '123', name: 'Fixture song', artists: [{ name: 'Artist' }], album: { name: 'Album' }, durationMs: 10000,
        sourceRef: { kind: 'online', providerId: id, mediaId: '123', providerData: { privateRouting: 'trusted-fixture' } } };
    const collection = { providerId: id, id: 'list-123', type: 'playlist', name: 'Own list', isOwned: true,
        providerData: { ownerId: 'server-owner' } };
    const provider = {
        id, normalizeSong: value => value, normalizeCollection: value => value,
        auth: { getLoginStatus: async () => state.user,
            startLogin: async () => `key-${++state.key}`, createQr: async () => 'data:image/png;base64,AA==',
            checkLogin: async () => ({ state: state.user ? 'confirmed' : 'waiting' }),
            cancelLogin: async key => state.cancelled.push(key), logout: async () => { state.user = null; } },
        search: { searchSongs: async () => ({ items: [song], hasMore: false }) },
        library: { getUserPlaylists: async () => ({ items: [collection], hasMore: false }), getLikedSongIds: async () => ({ ids: ['123'], whole: true }) },
        catalog: { getPlaylistTracks: async (raw, limit, offset, list) => { state.calls.push(['playlist', raw, list]); return { items: [song], hasMore: false }; } },
        playback: { getSongDetail: async () => song, getAudioSource: async value => {
            assert.equal(value.sourceRef.providerData.privateRouting, 'trusted-fixture');
            return { url: 'https://media.example.test/fixture.mp3', quality: 'standard' };
        } },
        lyrics: { getLyrics: async () => ({ lyrics: { lines: [{ startTime: 1, endTime: 3, fullText: 'Fixture',
            words: [{ text: 'Fixture', startTime: 1, endTime: 3 }], translation: '测试' }] } }) },
        mutations: { likeSong: async value => state.calls.push(['like', value]),
            updatePlaylistTracks: async (op, value, tracks) => state.calls.push(['add', value, tracks]) },
        ...extra,
    };
    return { provider, state, song, collection };
}

for (const id of ['netease', 'qq', 'kugou', 'qishui']) {
    test(`${id}: a restored recommendation keeps its private route while changed tracks are fetched again`, async () => {
        const f = fixture(id), saved = new Map();
        const store = { get: (key, fallback) => structuredClone(saved.get(key) ?? fallback),
            set: (key, value) => saved.set(key, structuredClone(value)) };
        f.state.user = { id: 'owner' };
        const collection = { ...f.collection, id: 'public-recommendation', isOwned: false,
            providerData: { ownerId: 'route-owner', dirId: 42, cardId: 7, virtualRecommendation: id === 'kugou' } };
        f.provider.recommendations = { getRecommendedCollections: async () => [collection] };
        let songs = [f.song];
        f.provider.catalog.getPlaylistTracks = async (raw, limit, offset, context) => {
            assert.equal(context.providerData.ownerId, 'route-owner');
            assert.equal(context.providerData.dirId, 42);
            return { items: songs, hasMore: false };
        };
        let facade = createFacade({ providers: { [id]: f.provider }, store });
        const home = await facade.online(id, { action: 'home' });
        await facade.online(id, { action: 'playlist', args: { id: home.recommendations[0].id } });
        // The platform removes this card from home, replaces and reorders its songs.
        f.provider.recommendations.getRecommendedCollections = async () => [];
        songs = ['789', '456'].map(songId => ({ ...f.song, id: songId }));
        facade = createFacade({ providers: { [id]: f.provider }, store });
        await facade.online(id, { action: 'home' });
        const refreshed = await facade.online(id, { action: 'playlist', args: { id: home.recommendations[0].id } });
        assert.deepEqual(refreshed.tracks.map(track => track.id), [`${id}:789`, `${id}:456`]);
        assert.equal(JSON.stringify(home).includes('route-owner'), false);
    });
}

test('legacy NetEase and QQ public IDs load without a previous home request', async () => {
    for (const id of ['netease', 'qq']) {
        const f = fixture(id);
        if (id === 'qq') f.provider.catalog.getPlaylistDetail = async (raw, context) => {
            assert.ok(context, 'Folia QQ requires collection context');
            return context;
        };
        const facade = createFacade({ providers: { [id]: f.provider } });
        assert.equal((await facade.online(id, { action: 'playlist', args: { id: `${id}:123` } })).tracks.length, 1);
    }
});

test('legacy album and KuGou virtual-card IDs retain their catalog type and route', async () => {
    const netease = fixture('netease');
    netease.provider.catalog.getAlbumTracks = async () => ({ items: [netease.song] });
    netease.provider.catalog.getPlaylistTracks = async () => { throw Error('wrong playlist route'); };
    const album = createFacade({ providers: { netease: netease.provider } });
    assert.equal((await album.online('netease', { action: 'playlist',
        args: { id: 'netease:123', collectionType: 'album' } })).tracks.length, 1);
    const kugou = fixture('kugou');
    kugou.provider.catalog.getPlaylistDetail = async (raw, context) => {
        assert.equal(context.providerData.virtualRecommendation, true);
        assert.equal(context.providerData.cardId, 7); return context;
    };
    const card = createFacade({ providers: { kugou: kugou.provider } });
    assert.equal((await card.online('kugou', { action: 'playlist', args: { id: 'kugou:kugou-card-7' } })).tracks.length, 1);
    const qq = fixture('qq');
    qq.provider.catalog.getPlaylistTracks = async (raw, limit, offset, context) => {
        assert.equal(context.providerData.listId, 7);
        assert.equal(context.providerData.list, raw.startsWith('toplist') ? 'toplist' : 'radio');
        return { items: [qq.song] };
    };
    const qqLists = createFacade({ providers: { qq: qq.provider } });
    for (const kind of ['toplist', 'radio'])
        assert.equal((await qqLists.online('qq', { action: 'playlist', args: { id: `qq:${kind}-7` } })).tracks.length, 1);
});

test('private collection contexts do not cross accounts, and cancelled reads cannot overwrite them', async () => {
    const f = fixture('qishui'), saved = new Map();
    const store = { get: (key, fallback) => structuredClone(saved.get(key) ?? fallback),
        set: (key, value) => saved.set(key, structuredClone(value)) };
    f.state.user = { id: 'first' };
    let facade = createFacade({ providers: { qishui: f.provider }, store });
    await facade.online('qishui', { action: 'home' });
    f.state.user = { id: 'second' };
    f.provider.library.getUserPlaylists = async () => ({ items: [] });
    f.provider.catalog.getPlaylistDetail = async (raw, context) => context;
    f.provider.catalog.getPlaylistTracks = async (raw, limit, offset, context) => {
        assert.equal(context.isOwned, undefined);
        assert.equal(context.providerData, undefined);
        return { items: [] };
    };
    facade = createFacade({ providers: { qishui: f.provider }, store });
    await facade.online('qishui', { action: 'playlist', args: { id: 'qishui:list-123' } });
    let release;
    f.provider.recommendations = { getRecommendedCollections: () => new Promise(resolve => { release = resolve; }) };
    const task = { cancelled: false };
    const pending = withRequest(task, () => facade.online('qishui', { action: 'home' }));
    while (!release) await new Promise(resolve => setImmediate(resolve));
    task.cancelled = true; release([{ ...f.collection, name: 'late' }]);
    await assert.rejects(pending, /cancelled/);
    assert.equal(JSON.stringify([...saved.values()]).includes('late'), false);
});

test('each provider owns its login attempt; cancellation neither logs out nor cancels another account', async () => {
    const qq = fixture('qq'), qishui = fixture('qishui');
    const facade = createFacade({ providers: { qq: qq.provider, qishui: qishui.provider } });
    assert.equal((await facade.auth('qq', { operation: 'begin', attempt: 1 })).qrImage, 'data:image/png;base64,AA==');
    await facade.auth('qishui', { operation: 'begin', attempt: 4 });
    await facade.cancel('qq', 99);
    assert.deepEqual(qq.state.cancelled, []);
    qq.state.user = { id: 'u1', nickname: 'QQ' };
    assert.equal((await facade.auth('qq', { operation: 'poll', attempt: 1 })).status, 'signed-in');
    await facade.cancel('qishui', 4);
    assert.equal((await facade.auth('qq', { operation: 'refresh' })).status, 'signed-in');
    assert.deepEqual(qishui.state.cancelled, ['key-1']);
    assert.equal((await facade.auth('qishui', { operation: 'poll', attempt: 4 })).status, 'expired');
    assert.equal((await facade.auth('qq', { operation: 'refresh' })).displayName, 'QQ');
});

test('a cancelled in-flight QR begin cannot create or confirm its old session', async () => {
    const qq = fixture('qq');
    let release;
    qq.provider.auth.startLogin = () => new Promise(resolve => { release = resolve; });
    let created = 0;
    qq.provider.auth.createQr = async () => { created++; return 'data:image/png;base64,AA=='; };
    const facade = createFacade({ providers: { qq: qq.provider } });
    const pending = facade.auth('qq', { operation: 'begin', attempt: 7 });
    while (!release) await new Promise(resolve => setImmediate(resolve));
    await facade.cancel('qq', 7); release('late-key');
    assert.equal((await pending).status, 'signed-out');
    assert.equal(created, 0);
    assert.deepEqual(qq.state.cancelled, ['late-key']);
});

test('a cancelled late login poll cannot replace credentials or confirm the cancelled account', async () => {
    const qq = fixture('qq');
    const storage = guardStorage(memoryStorage());
    storage.setItem('session', 'previous-account');
    let release;
    qq.provider.auth.checkLogin = async () => {
        await new Promise(resolve => { release = resolve; });
        storage.setItem('session', 'late-account');
        return { state: 'confirmed' };
    };
    const facade = createFacade({ providers: { qq: qq.provider } });
    await facade.auth('qq', { operation: 'begin', attempt: 8 });
    const task = { cancelled: false };
    const pending = withRequest(task, () => facade.auth('qq', { operation: 'poll', attempt: 8 }, () => task.cancelled));
    while (!release) await new Promise(resolve => setImmediate(resolve));
    task.cancelled = true; await facade.cancel('qq', 8); release();
    assert.equal((await pending).status, 'signed-out');
    assert.equal(storage.getItem('session'), 'previous-account');
    assert.equal((await facade.auth('qq', { operation: 'poll', attempt: 8 })).status, 'expired');
});

test('songs and private collection routing remain in the facade, while audio and lyrics use original adapters', async () => {
    const qishui = fixture('qishui'); qishui.state.user = { id: 'u2', nickname: 'Qishui' };
    const facade = createFacade({ providers: { qishui: qishui.provider }, mediaUrl: async () => 'http://127.0.0.1:1234/media/capability' });
    const page = await facade.online('qishui', { action: 'search', args: { query: 'Fixture' } });
    assert.equal(page.tracks[0].id, 'qishui:123');
    assert.equal(page.tracks[0].source, 'online:qishui:123');
    assert.equal(JSON.stringify(page).includes('privateRouting'), false);
    const home = await facade.online('qishui', { action: 'home', args: {} });
    await facade.online('qishui', { action: 'playlist', args: { id: home.playlists[0].id } });
    assert.equal(qishui.state.calls[0][2].providerData.ownerId, 'server-owner');
    await facade.online('qishui', { action: 'addToPlaylist', args: { id: home.playlists[0].id, trackId: page.tracks[0].id } });
    assert.equal(qishui.state.calls[1][2][0].sourceRef.providerData.privateRouting, 'trusted-fixture');
    const source = await facade.online('qishui', { action: 'resolve', args: { id: page.tracks[0].id } });
    assert.equal(source.url, 'http://127.0.0.1:1234/media/capability');
    const lyrics = await facade.online('qishui', { action: 'lyrics', args: { id: page.tracks[0].id } });
    assert.equal(lyrics.foliaLines[0].startTime, 1);
    assert.equal(lyrics.foliaLines[0].words[0].endTime, 3);
    assert.equal(lyrics.foliaLines[0].translation, '测试');
    await assert.rejects(facade.online('qishui', { action: 'resolve', args: { id: 'qq:123' } }), /invalid-response/);
});

test('preview-only audio is never upgraded into a full-track playback source', async () => {
    const qishui = fixture('qishui');
    qishui.provider.playback.getAudioSource = async () => ({ url: 'https://media.example.test/clip.mp3', preview: true });
    const facade = createFacade({ providers: { qishui: qishui.provider } });
    await assert.rejects(facade.online('qishui', { action: 'resolve', args: { id: 'qishui:123' } }), /preview-only/);
    for (const quality of ['ultra', 'jyeffect', 'sky', 'jymaster'])
        await assert.rejects(facade.online('qishui', { action: 'resolve', args: { id: 'qishui:123', quality } }), /unsupported/);
});

test('provider lyric translations and romanization survive the public facade', async () => {
    const qq=fixture('qq');
    qq.provider.lyrics={getLyrics:async()=>({lyrics:{lines:[
        {startTime:1,endTime:3,fullText:'你好',translation:'Hello',romanization:'nǐ hǎo',words:[]},
        {startTime:3,endTime:5,fullText:'再见',alternateTexts:[{role:'translation',text:'Goodbye'},
            {role:'romanization',text:'zài jiàn'}],words:[]},
    ]}})};
    const facade=createFacade({providers:{qq:qq.provider}});
    const tracks=await facade.online('qq',{action:'search',args:{query:'fixture'}});
    const result=await facade.online('qq',{action:'lyrics',args:{id:tracks.tracks[0].id}});
    assert.equal(result.foliaLines[0].translation,'Hello');
    assert.equal(result.foliaLines[0].romanization,'nǐ hǎo');
    assert.equal(result.foliaLines[1].translation,'Goodbye');
    assert.equal(result.foliaLines[1].romanization,'zài jiàn');
});

test('QQ official image addresses become HTTPS without accepting arbitrary HTTP origins or credentials', async () => {
    const qq = fixture('qq'); qq.state.user = { id: 'fixture', nickname: 'Fixture', avatarUrl: 'http://thirdqq.qlogo.cn/fixture' };
    qq.collection.coverUrl = '//qpic.y.qq.com/fixture';
    qq.song.album.coverUrl = 'http://y.gtimg.cn/fixture';
    const facade = createFacade({ providers: { qq: qq.provider } });
    assert.equal((await facade.auth('qq', { operation: 'refresh' })).avatarUrl, 'https://thirdqq.qlogo.cn/fixture');
    assert.equal((await facade.online('qq', { action: 'home' })).playlists[0].artwork, 'https://qpic.y.qq.com/fixture');
    assert.equal((await facade.online('qq', { action: 'search', args: { query: 'fixture' } })).tracks[0].artwork, 'https://y.gtimg.cn/fixture');
    for (const source of ['http://other.example/fixture', '//other.example/fixture', 'http://user:password@y.gtimg.cn/fixture',
        'http://y.gtimg.cn:8080/fixture', 'https://y.gtimg.cn:8080/fixture', 'file:///fixture']) {
        qq.state.user.avatarUrl = source;
        assert.equal((await facade.auth('qq', { operation: 'refresh' })).avatarUrl, '');
    }
});
