import test from 'node:test';
import assert from 'node:assert/strict';
import {initialIslandState,reduceIslandState} from '../island-state.js';
import {initialSystemFeatureState,reduceSystemFeatureState} from '../system-feature-state.js';
import {initialState,reduce,taskbarDisplayedPreferences} from '../state.js';

test('feature reducers work without navigation, wallpaper or another feature state',()=>{
  const island=initialIslandState();
  const requested=reduceIslandState(island,{type:'request',scope:'island-notifications',key:'set'});
  assert.equal(requested.islandNotificationsStatus,'saving');
  assert.equal(requested.islandNotifications,island.islandNotifications);
  assert.equal(Object.hasOwn(requested,'wallpaper'),false);
  const system=initialSystemFeatureState();
  const enabled=reduceSystemFeatureState(system,{type:'request',scope:'taskbar',key:'enabled',value:true});
  assert.equal(enabled.taskbar.enabled,false,'a request cannot overwrite host confirmation');
  assert.deepEqual(taskbarDisplayedPreferences(enabled),{...system.taskbar,enabled:true});
  assert.equal(Object.hasOwn(enabled,'dynamicIsland'),false);
});

test('unowned events and consumed stale messages have distinct routing results',()=>{
  const island=initialIslandState(),system=initialSystemFeatureState();
  for(const reducer of [reduceIslandState,reduceSystemFeatureState]) {
    assert.equal(reducer({}, {type:'navigate',view:'home'}),undefined);
    assert.equal(reducer({}, {type:'host',message:{type:'future-feature-state'}}),undefined);
  }
  assert.equal(reduceIslandState(island,{type:'host',message:{type:'island-notifications-state'}}),island);
  assert.equal(reduceSystemFeatureState({...system,taskbarRevision:3},
    {type:'host',message:{type:'taskbar-state',revision:2}}).taskbarRevision,3);
  const state=initialState();
  assert.equal(reduce(state,{type:'host',message:{type:'island-notifications-state'}}),state);
  assert.equal(reduce(state,{type:'host',message:{type:'future-feature-state'}}),state);
});

test('feature defaults do not share writable nested state between instances',()=>{
  const firstIsland=initialIslandState(),secondIsland=initialIslandState();
  firstIsland.dynamicIsland.screens.push(2);
  firstIsland.islandNotifications.preferences.messagesQQ=true;
  assert.deepEqual(secondIsland.dynamicIsland.screens,[1]);
  assert.equal(secondIsland.islandNotifications.preferences.messagesQQ,false);
  const firstSystem=initialSystemFeatureState(),secondSystem=initialSystemFeatureState();
  firstSystem.taskbar.enabled=true;
  assert.equal(secondSystem.taskbar.enabled,false);
});

test('global disconnect settles feature requests while retaining confirmed settings and navigation',()=>{
  let state=reduce(initialState(),{type:'navigate',view:'dynamicIsland'});
  state=reduce(state,{type:'request',scope:'taskbar',key:'enabled',value:true});
  state=reduce(state,{type:'request',scope:'island-notifications',key:'set'});
  const disconnected=reduce(state,{type:'unavailable',scope:'all'});
  for(const key of ['taskbarStatus','iconsStatus','minimizeStatus','dynamicIslandStatus','islandNotificationsStatus'])
    assert.equal(disconnected[key],'unavailable',key);
  assert.equal(disconnected.taskbarPending,null);
  assert.equal(disconnected.taskbarRequestRevision,null);
  assert.equal(disconnected.taskbar,state.taskbar);
  assert.equal(disconnected.islandNotifications,state.islandNotifications);
  assert.equal(disconnected.view,'dynamicIsland');
  assert.equal(disconnected.history,state.history);
  assert.equal(state.taskbarStatus,'saving','the input state remains unchanged');
});
