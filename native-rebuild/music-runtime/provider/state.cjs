'use strict';

// Folia's state transitions run unchanged; the private Node host has no React subscribers.
function createState(initializer) {
    let state;
    const listeners = new Set();
    const get = () => state;
    const set = (patch, replace = false) => {
        const previous = state;
        const value = typeof patch === 'function' ? patch(state) : patch;
        state = replace ? value : { ...state, ...value };
        if (state !== previous) for (const listener of listeners) listener(state, previous);
    };
    const store = selector => typeof selector === 'function' ? selector(state) : state;
    store.getState = get;
    store.setState = set;
    store.subscribe = listener => { listeners.add(listener); return () => listeners.delete(listener); };
    state = typeof initializer === 'function' ? initializer(set, get, store) : initializer;
    const initialState = state;
    store.getInitialState = () => initialState;
    return store;
}
const create = initializer => initializer === undefined ? createState : createState(initializer);
module.exports = { create, createStore: createState };
