'use strict';
const { AsyncLocalStorage } = require('node:async_hooks');
const requests = new AsyncLocalStorage();
const withRequest = (task, work) => requests.run(task, work);
const withoutRequest = work => requests.run(undefined, work);
const requestCancelled = () => requests.getStore()?.cancelled === true;
// A cancelled QR check may finish inside the unchanged provider after its UI has
// closed. Do not let that late continuation replace a newer saved credential.
const guardStorage = storage => new Proxy(storage, {
    get(target, key) {
        const value = Reflect.get(target, key, target);
        if (typeof value !== 'function') return value;
        if (['setItem', 'removeItem', 'clear', 'set', 'delete'].includes(key))
            return (...args) => { if (!requestCancelled()) return value.apply(target, args); };
        return value.bind(target);
    },
});
module.exports = { withRequest, withoutRequest, requestCancelled, guardStorage };
