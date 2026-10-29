function listContext(state) {
  if (state.view==="search")
    return {view:state.view,key:JSON.stringify([state.query,state.wallpaperSearchPage])};
  const source={wallpaperLocal:"local",wallpaperOfficial:"official"}[state.view];
  if (!source) return null;
  const {kind,query,page}=state.wallpaperBrowse[source];
  return {view:state.view,key:JSON.stringify([kind,query,page])};
}

export function createWallpaperScrollMemory() {
  // Retain only the latest position of each list, not detached media nodes.
  const positions=new Map();
  let current=null;
  return {
    capture(root) {
      const grid=root.querySelector(".wallpaper-grid");
      if (current && grid)
        positions.set(current.view,{key:current.key,top:grid.scrollTop});
    },
    restore(root,state) {
      current=listContext(state);
      const grid=root.querySelector(".wallpaper-grid");
      if (!current || !grid) return;
      const saved=positions.get(current.view);
      grid.scrollTop=saved?.key===current.key ? saved.top : 0;
    },
    reset() {
      if (current) positions.delete(current.view);
      current=null;
    },
  };
}
