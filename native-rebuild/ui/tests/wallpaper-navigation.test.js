import test from "node:test";
import assert from "node:assert/strict";
import {createWallpaperScrollMemory} from "../wallpaper-navigation.js";
import {initialState} from "../state.js";

function fixture() {
  const grid={scrollTop:0};
  return {grid,querySelector:selector=>selector===".wallpaper-grid"?grid:null};
}
test("returning from detail restores each wallpaper list independently",()=>{
  const memory=createWallpaperScrollMemory(),root=fixture(),state=initialState();
  state.view="wallpaperOfficial";
  state.wallpaperBrowse.official.page=3;
  memory.restore(root,state);
  root.grid.scrollTop=1250;
  memory.capture(root);
  memory.restore(root,{...state,view:"wallpaperDetail"});
  memory.restore(root,{...state,view:"wallpaperLocal"});
  assert.equal(root.grid.scrollTop,0);
  root.grid.scrollTop=400;
  memory.capture(root);
  memory.restore(root,state);
  assert.equal(root.grid.scrollTop,1250);
  memory.restore(root,{...state,view:"wallpaperLocal"});
  assert.equal(root.grid.scrollTop,400);
});
test("pagination and filtering reset scroll without reusing a stale list position",()=>{
  const memory=createWallpaperScrollMemory(),root=fixture(),state=initialState();
  state.view="wallpaperOfficial";
  memory.restore(root,state);
  root.grid.scrollTop=900;
  memory.capture(root);
  state.wallpaperBrowse.official={kind:"image",query:"",page:1};
  memory.restore(root,state);
  assert.equal(root.grid.scrollTop,0);
  root.grid.scrollTop=300;
  memory.capture(root);
  memory.reset();
  memory.capture(root);
  memory.restore(root,state);
  assert.equal(root.grid.scrollTop,0);
});
test("search results retain their own page position and a new query starts at the top",()=>{
  const memory=createWallpaperScrollMemory(),root=fixture();
  const state={...initialState(),view:"search",query:"forest",wallpaperSearchPage:2};
  memory.restore(root,state);
  root.grid.scrollTop=640;
  memory.capture(root);
  memory.restore(root,{...state,view:"wallpaperDetail"});
  memory.restore(root,state);
  assert.equal(root.grid.scrollTop,640);
  memory.restore(root,{...state,query:"space",wallpaperSearchPage:1});
  assert.equal(root.grid.scrollTop,0);
});
