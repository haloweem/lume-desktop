'use strict';

// This process-local bounded cache replaces IndexedDB; credentials use the separate host session store.
const entries = new Map();
const maximumEntries = 256;
const copy = value => value === undefined ? value : structuredClone(value);
async function saveToCache(key, data) {
    entries.delete(key);
    entries.set(key, { data: copy(data), timestamp: Date.now() });
    while (entries.size > maximumEntries) entries.delete(entries.keys().next().value);
}
async function getFromCache(key) { return entries.has(key) ? copy(entries.get(key).data) : null; }
async function removeFromCache(key) { entries.delete(key); }
async function getFromCacheWithMigration(key, migrate) {
    const value = await getFromCache(key);
    if (value === null || typeof migrate !== 'function') return value;
    const migration = migrate(value);
    if (migration.changed) await saveToCache(key, migration.value);
    return migration.value;
}
async function getCacheKeysByPrefix(prefix) { return [...entries.keys()].filter(key => key.startsWith(prefix)); }
async function getCacheEntriesByPrefix(prefix) {
    return [...entries].filter(([key]) => key.startsWith(prefix)).map(([key, value]) => ({ key, ...copy(value) }));
}
async function getCacheEntriesByKey(predicate) {
    return [...entries.keys()].filter(predicate).map(key => ({ key, ...copy(entries.get(key)) }));
}
async function removeCacheEntries(keys) { for (const key of keys) entries.delete(key); }
async function removeCacheEntriesByPrefix(prefix) { await removeCacheEntries(await getCacheKeysByPrefix(prefix)); }
async function clearCache(preserve = []) { for (const key of entries.keys()) if (!preserve.includes(key)) entries.delete(key); }
module.exports = { saveToCache, getFromCache, removeFromCache, getFromCacheWithMigration,
    getCacheKeysByPrefix, getCacheEntriesByPrefix, getCacheEntriesByKey, removeCacheEntries,
    removeCacheEntriesByPrefix, clearCache };
