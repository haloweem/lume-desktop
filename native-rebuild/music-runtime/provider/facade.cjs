'use strict';

const crypto = require('node:crypto');
const { requestCancelled } = require('./request-context.cjs');

// Translate Lume's small public contract while keeping Folia songs, keys and routing metadata private.
const PROVIDERS = new Set(['netease', 'qq', 'kugou', 'qishui']);
const ACTIONS = new Set(['home', 'explore', 'playlist', 'search', 'favorites', 'recent', 'likes',
    'like', 'createPlaylist', 'addToPlaylist', 'resolve', 'lyrics']);
const QUALITIES = new Set(['standard', 'high', 'lossless', 'hires']);
const ARGUMENTS = {
    search: ['query'], playlist: ['id', 'collectionType'], like: ['id', 'value'], createPlaylist: ['title'],
    addToPlaylist: ['id', 'trackId'], resolve: ['id', 'quality'], lyrics: ['id'],
};
const fail = code => { const error = new Error(code); error.code = code; throw error; };
const bounded = (value, fallback, maximum) => Number.isSafeInteger(value) && value >= 0 ? Math.min(value, maximum) : fallback;
const text = (value, maximum = 1024) => typeof value === 'string' ? value.replace(/[\u0000-\u001f\u007f]/g, ' ').slice(0, maximum) : '';
const QQ_IMAGE_HOSTS = new Set(['y.qq.com', 'y.gtimg.cn', 'q1.qlogo.cn', 'q2.qlogo.cn', 'thirdqq.qlogo.cn', 'qpic.y.qq.com', 'music-file.y.qq.com']);
const image = value => {
    if (typeof value !== 'string' || value.length > 2048) return '';
    try {
        const relative = value.startsWith('//');
        const url = new URL(relative ? `https:${value}` : value);
        if (url.username || url.password || url.port) return '';
        if (relative || url.protocol === 'http:') {
            if (!QQ_IMAGE_HOSTS.has(url.hostname)) return '';
            url.protocol = 'https:';
        }
        return url.protocol === 'https:' ? url.href : '';
    }
    catch { return ''; }
};
const list = value => Array.isArray(value) ? value : [];
const newPage = () => ({ tracks: [], playlists: [], recommendations: [], more: false });
function createFacade({ providers, mediaUrl, store }) {
    const states = new Map();
    const contextKey = 'collectionContexts.v1';
    let savedContexts = list(store?.get(contextKey, []));
    const requireProvider = id => {
        if (!PROVIDERS.has(id) || !providers?.[id]) fail('unsupported');
        return providers[id];
    };
    const stateOf = id => {
        requireProvider(id);
        if (!states.has(id)) states.set(id, { user: undefined, songs: new Map(), collections: new Map(), liked: null, attempt: null, feed: null,
            collectionsOwner: undefined, collectionsDirty: false });
        return states.get(id);
    };
    const remember = (map, key, value, maximum) => {
        map.delete(key); map.set(key, value);
        while (map.size > maximum) map.delete(map.keys().next().value);
        return key;
    };
    const stableId = (provider, rawId) => {
        const value = String(rawId ?? '');
        if (!value || value.length > 4096) fail('invalid-response');
        const suffix = /^[a-zA-Z0-9_.:-]{1,100}$/.test(value) ? value :
            `opaque-${crypto.createHash('sha256').update(value).digest('hex').slice(0, 40)}`;
        return `${provider}:${suffix}`;
    };
    const keepSong = (id, raw) => {
        if (requestCancelled()) fail('cancelled');
        const provider = requireProvider(id);
        const song = raw?.sourceRef?.kind === 'online' && raw.sourceRef.providerId === id ? raw : provider.normalizeSong(raw);
        if (song.sourceRef?.kind !== 'online' || song.sourceRef.providerId !== id) fail('invalid-response');
        const key = stableId(id, song.id);
        remember(stateOf(id).songs, key, song, 4096);
        return { id: key, title: text(song.name), artist: list(song.artists).map(artist => text(artist.name)).filter(Boolean).join(', '),
            album: text(song.album?.name), artwork: image(song.album?.coverUrl),
            duration: Math.max(0, Math.min(604800, Number(song.durationMs) / 1000 || 0)), source: `online:${key}`,
            vipRequired: song.sourceRef.providerData?.lumeVipRequired === true || Number(song.fee) === 1 || Number(song.fee) === 4,
            ...(song.sourceRef.providerData?.lumeMembershipBadge ? { membershipBadge: song.sourceRef.providerData.lumeMembershipBadge } : {}),
            ...(stateOf(id).liked ? { liked: stateOf(id).liked.has(String(provider.library?.getSongLikeId?.(song) ?? song.id)) } : {}) };
    };
    const keepCollection = (id, raw) => {
        if (requestCancelled()) fail('cancelled');
        const provider = requireProvider(id);
        const collection = raw?.providerId === id ? raw : provider.normalizeCollection?.(raw);
        if (!collection || collection.providerId !== id) fail('invalid-response');
        const key = stableId(id, collection.id);
        const map = stateOf(id).collections;
        const existing = map.get(key);
        // Public recommendations must not overwrite private routing of a list the account owns.
        remember(map, key, existing?.isOwned && !collection.isOwned ? existing : collection, 1024);
        stateOf(id).collectionsDirty = true;
        return { id: key, providerId: id, type: text(collection.type, 32) || 'playlist', title: text(collection.name),
            artwork: image(collection.coverUrl), owned: collection.isOwned === true, description: text(collection.description || collection.creator?.nickname, 2000),
            ...(id === 'qq' && collection.isOwned === true && collection.isLiked ? { isLiked: true } : {}),
            ...(Number.isFinite(collection.trackCount) ? { trackCount: Math.max(0, collection.trackCount) } : {}) };
    };
    const saveCollections = id => {
        const state = stateOf(id);
        if (!store || !state.collectionsDirty || requestCancelled()) return;
        const userId = String(state.user?.id ?? '');
        const entries = savedContexts.filter(entry => entry?.provider !== id);
        for (const collection of state.collections.values()) entries.push({ provider: id, userId, collection });
        // Keep routing metadata within the existing encrypted store's size limit.
        let bytes = 0;
        const retained = [];
        for (const entry of entries.reverse()) {
            const size = Buffer.byteLength(JSON.stringify(entry));
            if (size > 64 * 1024 || bytes + size > 256 * 1024) continue;
            retained.push(entry); bytes += size;
            if (retained.length === 128) break;
        }
        savedContexts = retained.reverse();
        try { store.set(contextKey, savedContexts); state.collectionsDirty = false; }
        catch { /* Browsing can continue when its optional routing cache cannot be saved. */ }
    };
    const publicUser = (id, user, status = 'signed-in') => {
        const memberships = [];
        const tier = Number(user?.vipType) || 0;
        if (['netease', 'qq', 'kugou'].includes(id) && (id === 'netease' ? tier === 10 || tier === 11 || tier >= 100 : tier > 0)) {
            const superVip = id === 'qq' && tier === 2;
            memberships.push({ kind: id === 'qq' ? superVip ? 'qq-super-vip' : 'qq-green-vip' : `${id}-vip`,
                label: id === 'qq' ? superVip ? '超级会员' : '绿钻VIP' : id === 'netease' ? '黑胶VIP' : '酷狗VIP',
                level: Math.min(1000, tier), active: true,
                iconUrl: id === 'qq' ? `https://y.qq.com/mediastyle/lv-icon/v14/2x/${superVip ? 'svip5' : 'vip5'}.png` : '', expiresAt: 0 });
        }
        return { status, phase: '', userId: text(String(user?.id ?? ''), 128), displayName: text(user?.nickname, 256),
            avatarUrl: image(user?.avatarUrl), memberships, qrImage: '', qrText: '', error: '', loginMode: 'qr' };
    };
    const setUser = (id, user) => {
        if (requestCancelled()) fail('cancelled');
        const state = stateOf(id);
        const owner = String(user?.id ?? '');
        if (state.collectionsOwner !== undefined && state.collectionsOwner !== owner) {
            state.songs.clear(); state.collections.clear(); state.liked = null; state.feed = null;
        }
        state.user = user ?? null;
        if (state.collectionsOwner !== owner) {
            state.collectionsOwner = owner;
            for (const entry of savedContexts) {
                const collection = entry?.collection;
                if (entry?.provider === id && entry.userId === owner && collection?.providerId === id &&
                    ['string', 'number'].includes(typeof collection.id) && typeof collection.type === 'string') {
                    remember(state.collections, stableId(id, collection.id), collection, 1024);
                }
            }
        }
        if (!user) {
            savedContexts = savedContexts.filter(entry => entry?.provider !== id || entry.userId === '');
            state.collectionsDirty = true;
            saveCollections(id);
        }
        return user;
    };
    const readUser = async (id, force = false) => {
        const state = stateOf(id);
        if (force || state.user === undefined) setUser(id, await requireProvider(id).auth.getLoginStatus());
        return state.user;
    };
    const currentReply = id => publicUser(id, stateOf(id).user, stateOf(id).user ? 'signed-in' : 'signed-out');
    const authError = error => {
        const code = error?.code;
        if (code === 'auth-required') return 'expired';
        if (code === 'network' || error instanceof TypeError) return 'network';
        if (code === 'unsupported') return 'service';
        if (code === 'invalid-response') return 'invalid-response';
        return 'service';
    };
    const checkedAttempt = (id, attempt) => {
        const value = stateOf(id).attempt;
        return value && !value.cancelled && (attempt === undefined || String(value.id) === String(attempt)) ? value : null;
    };
    async function cancel(id, attempt) {
        const value = checkedAttempt(id, attempt);
        if (value) {
            value.cancelled = true;
            stateOf(id).attempt = null;
            if (value.key) await requireProvider(id).auth.cancelLogin?.(value.key);
        }
        return currentReply(id);
    }
    async function auth(id, args = {}, isCancelled = () => false) {
        const provider = requireProvider(id);
        const state = stateOf(id);
        const operation = args.operation;
        try {
            if (isCancelled()) return currentReply(id);
            if (operation === 'refresh') {
                const user = await provider.auth.getLoginStatus();
                if (isCancelled()) return currentReply(id);
                setUser(id, user);
                return publicUser(id, user, user ? 'signed-in' : 'signed-out');
            }
            if (operation === 'logout') {
                await cancel(id);
                await provider.auth.logout(); setUser(id, null);
                return currentReply(id);
            }
            if (operation === 'begin') {
                await cancel(id);
                if (isCancelled()) return currentReply(id);
                const value = { id: args.attempt ?? crypto.randomBytes(16).toString('hex'), key: null, cancelled: false,
                    expiresAt: 0, image: '', loginMode: provider.auth.createQr ? 'qr' : 'browser' };
                state.attempt = value;
                await provider.auth.resolveLoginMethods?.();
                if (state.attempt !== value || value.cancelled || isCancelled()) return currentReply(id);
                value.key = await provider.auth.startLogin?.();
                if (typeof value.key !== 'string' || !value.key) fail('invalid-response');
                if (state.attempt !== value || value.cancelled || isCancelled()) {
                    await provider.auth.cancelLogin?.(value.key);
                    return currentReply(id);
                }
                value.expiresAt = Date.now() + Math.min(provider.auth.getLoginTtlMs?.() || 300000, 15 * 60000);
                if (value.loginMode === 'qr') value.image = await provider.auth.createQr(value.key);
                else if (!provider.auth.opensLoginPageItself?.(value.key)) await provider.auth.openLoginPage?.(value.key);
                if (state.attempt !== value || value.cancelled || isCancelled()) return currentReply(id);
                if (value.loginMode === 'qr' && (typeof value.image !== 'string' || !value.image.trim())) fail('invalid-response');
                return { ...publicUser(id, null, 'authorizing'), phase: value.loginMode === 'browser' ? 'browser' : 'waiting',
                    loginMode: value.loginMode, qrImage: /^data:image\/png;base64,[A-Za-z0-9+/=]+$/.test(value.image) ? value.image : '',
                    qrText: value.image && !value.image.startsWith('data:') ? text(value.image, 2048) : '' };
            }
            const value = checkedAttempt(id, args.attempt);
            if (!value) return { ...publicUser(id, null, 'expired'), error: 'expired' };
            if (operation === 'reopen') {
                await provider.auth.openLoginPage?.(value.key);
                return { ...publicUser(id, null, 'authorizing'), loginMode: value.loginMode,
                    phase: value.loginMode === 'browser' ? 'browser' : 'waiting' };
            }
            if (operation !== 'poll') fail('unsupported');
            if (Date.now() >= value.expiresAt) { await cancel(id, value.id); return { ...publicUser(id, null, 'expired'), error: 'expired' }; }
            const answer = await provider.auth.checkLogin(value.key);
            if (state.attempt !== value || value.cancelled || isCancelled()) return currentReply(id);
            if (answer.state === 'confirmed') {
                const user = await provider.auth.getLoginStatus();
                if (state.attempt !== value || value.cancelled || isCancelled()) return currentReply(id);
                if (!user) return { ...publicUser(id, null, 'authorizing'), phase: 'scanned', loginMode: value.loginMode };
                setUser(id, user); state.attempt = null;
                return publicUser(id, user);
            }
            if (answer.state === 'expired') { await cancel(id, value.id); return { ...publicUser(id, null, 'expired'), error: 'expired' }; }
            if (answer.state === 'error' && !answer.transient) {
                await cancel(id, value.id);
                return { ...publicUser(id, null, 'error'), error: 'service', loginMode: value.loginMode };
            }
            return { ...publicUser(id, null, 'authorizing'), loginMode: value.loginMode,
                phase: value.loginMode === 'browser' ? 'browser' : answer.state === 'scanned' ? 'scanned' : 'waiting' };
        } catch (error) {
            return { ...publicUser(id, null, 'error'), error: authError(error) };
        }
    }
    const fromId = (id, value) => {
        if (typeof value !== 'string' || value.length > 128 || !value.startsWith(`${id}:`)) fail('invalid-response');
        const raw = value.slice(id.length + 1);
        if (!/^[a-zA-Z0-9_.:-]{1,100}$/.test(raw) || raw.startsWith('opaque-')) fail('unavailable');
        return raw;
    };
    async function songOf(id, value) {
        const state = stateOf(id);
        if (state.songs.has(value)) return state.songs.get(value);
        const rawId = fromId(id, value);
        const get = requireProvider(id).playback?.getSongDetail;
        if (!get) fail('unavailable');
        const song = await get(rawId);
        if (!song) fail('unavailable');
        keepSong(id, song);
        return state.songs.get(value) ?? fail('unavailable');
    }
    async function collectionOf(id, value, type = 'playlist') {
        if (!['playlist', 'album'].includes(type)) fail('invalid-response');
        const state = stateOf(id);
        const user = await readUser(id);
        if (state.collections.has(value)) {
            const collection = state.collections.get(value);
            remember(state.collections, value, collection, 1024);
            state.collectionsDirty = true;
            return collection;
        }
        const rawId = fromId(id, value);
        const provider = requireProvider(id);
        // Older UI caches contain only public IDs. Recover private owner/dir routes first.
        if (user && provider.library?.getUserPlaylists) {
            try {
                let offset = 0;
                for (let page = 0; page < 20; page++) {
                    const answer = await provider.library.getUserPlaylists(user.id, 100, offset);
                    for (const item of list(answer?.items)) keepCollection(id, item);
                    if (state.collections.has(value)) return state.collections.get(value);
                    if (!answer?.hasMore || !Number.isSafeInteger(answer.nextOffset) || answer.nextOffset <= offset) break;
                    offset = answer.nextOffset;
                }
            } catch (error) { if (requestCancelled()) throw error; }
        }
        // KuGou's virtual recommendations are addressed by card rather than playlist ID.
        const card = id === 'kugou' && /^kugou-card-(\d+)$/.exec(rawId);
        const qqList = id === 'qq' && /^(toplist|radio)-(\d+)$/.exec(rawId);
        const seed = { providerId: id, id: rawId, type, name: '',
            ...(card ? { providerData: { virtualRecommendation: true, cardId: Number(card[1]) } } : {}),
            ...(qqList ? { providerData: { list: qqList[1], listId: Number(qqList[2]) } } : {}) };
        const get = type === 'album' ? provider.catalog?.getAlbumDetail : provider.catalog?.getPlaylistDetail;
        const collection = get ? await get(rawId, seed) : seed;
        if (!collection) fail('not-found');
        keepCollection(id, collection);
        return state.collections.get(value) ?? fail('not-found');
    }
    const mapPage = (id, page, kind = 'songs') => {
        const result = newPage();
        const items = list(page?.items);
        if (kind === 'songs') result.tracks = items.map(song => keepSong(id, song));
        else result.playlists = items.map(collection => keepCollection(id, collection));
        result.more = page?.hasMore === true;
        return result;
    };
    async function likes(id, fresh = false) {
        const provider = requireProvider(id), state = stateOf(id);
        if (!provider.library?.getLikedSongIds) return null;
        if (fresh || !state.liked) {
            const user = await readUser(id);
            if (!user) return null;
            const answer = await provider.library.getLikedSongIds(user.id);
            state.liked = new Set(list(answer?.ids).map(String));
        }
        return [...state.liked].map(raw => stableId(id, raw));
    }
    async function executeOnline(id, request = {}) {
        const provider = requireProvider(id), state = stateOf(id);
        const action = request.action, args = request.args ?? {};
        if (!ACTIONS.has(action) || !args || typeof args !== 'object' || Array.isArray(args)) fail('unsupported');
        const allowed = new Set(['offset', 'limit', '_userId', ...(ARGUMENTS[action] ?? [])]);
        if (Object.keys(args).some(key => !allowed.has(key))) fail('invalid-response');
        if (requestCancelled()) fail('cancelled');
        const offset = bounded(args.offset, 0, 100000), limit = Math.max(1, bounded(args.limit, 50, 100));
        let page = newPage();
        if (action === 'search') {
            if (!provider.search?.searchSongs) fail('unsupported');
            const query = text(args.query, 512).trim();
            page = mapPage(id, await provider.search.searchSongs(query, limit, offset));
        } else if (action === 'playlist') {
            const collection = await collectionOf(id, args.id, args.collectionType);
            const get = collection.type === 'album' ? provider.catalog?.getAlbumTracks :
                collection.type === 'cloud' ? provider.catalog?.getCloudTracks : provider.catalog?.getPlaylistTracks;
            if (!get) fail('unsupported');
            page = mapPage(id, collection.type === 'cloud' ? await get(limit, offset, collection) :
                await get(collection.id, limit, offset, collection));
        } else if (action === 'home' || action === 'explore') {
            const user = await readUser(id);
            const errors = [];
            if (action === 'home' && user && provider.library?.getUserPlaylists) {
                try {
                    let next = 0;
                    for (let count = 0; count < 20; count++) {
                        const library = await provider.library.getUserPlaylists(user.id, 100, next);
                        page.playlists.push(...list(library?.items).map(item => keepCollection(id, item)));
                        if (!library?.hasMore || !list(library.items).length || !Number.isSafeInteger(library.nextOffset) || library.nextOffset <= next) break;
                        next = library.nextOffset;
                    }
                }
                catch (error) { errors.push(error); }
            }
            if (action === 'home' && provider.recommendations?.getRecommendedCollections) {
                try {
                    // Folia home requests 35 recommendations; exploration has its own feed and pagination.
                    const collections = list(await provider.recommendations.getRecommendedCollections(35));
                    const seen = new Set();
                    page.recommendations = collections.filter(item => {
                        if (item.personalFm || seen.has(String(item.id))) return false;
                        seen.add(String(item.id)); return true;
                    }).map(item => keepCollection(id, item));
                } catch (error) { errors.push(error); }
            } else if (provider.explore?.getFeed) {
                try {
                    if (!state.feed || offset === 0) state.feed = { items: [], cursor: undefined, begun: false, seen: new Set() };
                    const held = state.feed;
                    for (let count = 0; count < 10 && (!held.begun || (held.items.length < offset + limit && held.cursor)); count++) {
                        const cursor = held.cursor;
                        const feed = await provider.explore.getFeed(cursor);
                        held.begun = true;
                        for (const section of list(feed.sections)) for (const item of list(section.items)) {
                            const key = item.kind === 'song' ? `song:${item.song?.id}` : `collection:${item.collection?.id}`;
                            if (!held.seen.has(key)) { held.seen.add(key); held.items.push(item); }
                        }
                        held.cursor = feed.nextCursor && feed.nextCursor !== cursor ? feed.nextCursor : undefined;
                    }
                    for (const item of held.items.slice(offset, offset + limit)) {
                        if (item.kind === 'song') page.tracks.push(keepSong(id, item.song));
                        else if (item.kind === 'collection') page.recommendations.push(keepCollection(id, item.collection));
                    }
                    page.more = held.items.length > offset + limit || Boolean(held.cursor);
                } catch (error) { errors.push(error); }
            } else if (provider.recommendations?.getRecommendedCollections) {
                try { page.recommendations = list(await provider.recommendations.getRecommendedCollections(limit)).map(item => keepCollection(id, item)); }
                catch (error) { errors.push(error); }
            }
            if (action === 'home' && provider.recommendations?.getDailySongs) {
                try { page.tracks = list(await provider.recommendations.getDailySongs()).slice(offset, offset + limit).map(song => keepSong(id, song)); }
                catch (error) { errors.push(error); }
            }
            if (!page.tracks.length && !page.playlists.length && !page.recommendations.length && errors.length) throw errors[0];
        } else if (action === 'likes') {
            return { ...page, likedIds: await likes(id, true) ?? [] };
        } else if (action === 'favorites') {
            const user = await readUser(id);
            if (!user) fail('expired');
            const collections = provider.library?.getUserPlaylists ? list((await provider.library.getUserPlaylists(user.id, 100, 0))?.items) : [];
            const favorite = collections.find(item => item.isLiked);
            if (favorite && provider.catalog?.getPlaylistTracks) {
                keepCollection(id, favorite);
                page = mapPage(id, await provider.catalog.getPlaylistTracks(favorite.id, limit, offset, favorite));
            } else {
                const ids = await likes(id, offset === 0);
                if (!ids) fail('unsupported');
                const selected = ids.slice(offset, offset + limit);
                const rawIds = selected.map(value => fromId(id, value));
                let songs;
                if (provider.playback?.getSongDetails) songs = await provider.playback.getSongDetails(rawIds);
                else if (provider.playback?.getSongDetail) songs = await Promise.all(rawIds.map(value => provider.playback.getSongDetail(value)));
                else fail('unsupported');
                page.tracks = list(songs).filter(Boolean).map(song => keepSong(id, song));
                page.more = offset + limit < ids.length;
            }
        } else if (action === 'recent') {
            // Folia's recommendation history is not listening history; only an explicit recent collection qualifies.
            const user = await readUser(id);
            if (!user || !provider.library?.getUserPlaylists || !provider.catalog?.getPlaylistTracks) fail('unsupported');
            const collections = list((await provider.library.getUserPlaylists(user.id, 100, 0))?.items);
            const recent = collections.find(item => item.type === 'history');
            if (!recent) fail('unsupported');
            keepCollection(id, recent);
            page = mapPage(id, await provider.catalog.getPlaylistTracks(recent.id, limit, offset, recent));
        } else if (action === 'like') {
            if (typeof args.value !== 'boolean' || !provider.mutations?.likeSong) fail('unsupported');
            const song = await songOf(id, args.id);
            await provider.mutations.likeSong(song, args.value);
            const raw = String(provider.library?.getSongLikeId?.(song) ?? song.id);
            if (state.liked) args.value ? state.liked.add(raw) : state.liked.delete(raw);
            return { liked: args.value, ...(state.liked ? { likedIds: [...state.liked].map(value => stableId(id, value)) } : {}) };
        } else if (action === 'createPlaylist') {
            if (!provider.mutations?.createPlaylist) fail('unsupported');
            const title = text(args.title, 256).trim(); if (!title) fail('invalid-response');
            await readUser(id);
            page.playlists = [keepCollection(id, await provider.mutations.createPlaylist(title))];
            return page;
        } else if (action === 'addToPlaylist') {
            if (!provider.mutations?.updatePlaylistTracks) fail('unsupported');
            const collection = await collectionOf(id, args.id), song = await songOf(id, args.trackId);
            if (provider.mutations.canAddToPlaylist?.(collection) === false || collection.isOwned !== true) fail('unsupported');
            await provider.mutations.updatePlaylistTracks('add', collection, [song]);
            return { ok: true };
        } else if (action === 'resolve') {
            if (args.quality !== undefined && !QUALITIES.has(args.quality)) fail('unsupported');
            const song = await songOf(id, args.id), quality = args.quality ?? 'high';
            if (!provider.playback?.getAudioSource) fail('unsupported');
            const source = await provider.playback.getAudioSource(song, quality);
            if (!source?.url) fail('unavailable');
            if (source.preview) fail('preview-only');
            const parsed = new URL(source.url);
            if (!['http:', 'https:'].includes(parsed.protocol) || parsed.username || parsed.password) fail('invalid-response');
            const url = typeof mediaUrl === 'function' ? await mediaUrl(id, source.url) : source.url;
            return { url, quality: source.quality || quality, bitrate: Number(source.bitrate) || 0 };
        } else if (action === 'lyrics') {
            if (!provider.lyrics?.getLyrics) fail('unsupported');
            const song = await songOf(id, args.id), user = await readUser(id);
            const result = await provider.lyrics.getLyrics(song, { userId: user?.id });
            const alternate = (line, role) => list(line?.alternateTexts).find(item => item?.role === role)?.text;
            const foliaLines = list(result?.lyrics?.lines).map(line => ({
                startTime: Math.max(0, Number(line.startTime) || 0), endTime: Math.max(0, Number(line.endTime) || 0),
                fullText: typeof line.fullText === 'string' ? line.fullText : '',
                words: list(line.words).map(word => ({ text: typeof word.text === 'string' ? word.text : '',
                    startTime: Math.max(0, Number(word.startTime) || 0), endTime: Math.max(0, Number(word.endTime) || 0) })),
                ...(typeof (line.translation ?? alternate(line, 'translation')) === 'string' ?
                    { translation: line.translation ?? alternate(line, 'translation') } : {}),
                ...(typeof (line.romanization ?? alternate(line, 'romanization')) === 'string' ?
                    { romanization: line.romanization ?? alternate(line, 'romanization') } : {}),
            }));
            return { foliaLines, isPureMusic: result?.isPureMusic === true };
        }
        try { const ids = await likes(id); if (ids) page.likedIds = ids; } catch { /* Optional badges never hide the requested page. */ }
        return page;
    }
    async function online(id, request) {
        try { return await executeOnline(id, request); }
        finally { if (PROVIDERS.has(id) && providers?.[id]) saveCollections(id); }
    }
    return { auth, cancel, online };
}

module.exports = { createFacade };
