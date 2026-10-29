import test from 'node:test';
import assert from 'node:assert/strict';
import {initialState, reduce} from '../state.js';
import {createWallpaperScrollMemory} from '../wallpaper-navigation.js';

for (const view of ['home','wallpaperOfficial','wallpaperLocal','wallpaperDetail','search']) {
  test(`music returns to ${view} without retaining playback data or duplicating history`,()=>{
    const state={...initialState(),view,history:['home','wallpaperOfficial'],query:'forest',
      wallpaperSearchPage:3,wallpaperSelection:{source:'official',id:'fixture'}};
    state.wallpaperBrowse.official={kind:'image',query:'forest',page:3};
    const memory=createWallpaperScrollMemory(),grid={scrollTop:0};
    const root={querySelector:()=>grid};
    memory.restore(root,state);grid.scrollTop=1250;memory.capture(root);
    let current=state;
    for(let i=0;i<20;i++) {
      current=reduce(current,{type:'navigate',view:'music'});
      assert.equal(current.view,'music');
      assert.equal(reduce(current,{type:'navigate',view:'music'}),current,'reentry is idempotent');
      assert.equal(current.wallpaper,state.wallpaper,'no copy of catalog or playback');
      current=reduce(current,{type:'music-leave'});
      assert.equal(current.view,view);
      assert.deepEqual(current.history,state.history);
      assert.equal(current.wallpaperBrowse,state.wallpaperBrowse);
      assert.equal(current.wallpaperSelection,state.wallpaperSelection);
      assert.equal(current.query,'forest');
      assert.equal(current.wallpaperSearchPage,3);
      memory.restore(root,current);
      if(['wallpaperOfficial','wallpaperLocal','search'].includes(view))assert.equal(grid.scrollTop,1250);
    }
  });
}

test('temporary settings preserve the panel anchor for back, native entry and titlebar exit',()=>{
  const panel={...initialState(),view:'wallpaperLocal',history:['home']};
  const music=reduce(panel,{type:'navigate',view:'music'});
  const settings=reduce(music,{type:'navigate',view:'applicationSettings'});
  assert.equal(reduce(settings,{type:'back'}).view,'music');
  assert.equal(reduce(reduce(settings,{type:'navigate',view:'music'}),{type:'music-leave'}).view,panel.view);
  assert.equal(reduce(settings,{type:'music-leave'}).view,'applicationSettings');
  assert.equal(reduce(music,{type:'music-leave'}).view,panel.view);
  const directSettings={...initialState(),view:'applicationSettings',history:['home','wallpaperLocal']};
  assert.equal(reduce(reduce(directSettings,{type:'navigate',view:'music'}),{type:'music-leave'}).view,'wallpaperLocal');
});

test('direct music has a home fallback and navigation away cannot leave a stale music history',()=>{
  assert.equal(reduce({...initialState(),view:'music'},{type:'music-leave'}).view,'home');
  const music=reduce(initialState(),{type:'navigate',view:'music'});
  const panel=reduce(music,{type:'navigate',view:'wallpaperLocal'});
  assert.equal(panel.musicReturn,null);
  assert.deepEqual(panel.history,['home']);
});
