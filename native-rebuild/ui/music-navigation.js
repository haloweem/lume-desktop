const PANEL_VIEWS = new Set(["home", "wallpaperLocal", "wallpaperOfficial", "desktopBeauty", "widgets",
  "clock", "calendar", "performance", "almanac", "weather", "organizer", "icons", "taskbar",
  "minimize", "search", "wallpaperDetail"]);

export function enterMusic(state) {
  if (state.view === "music") return state;
  const previous=state.history.at(-1);
  const anchorView=PANEL_VIEWS.has(state.view)?state.view:PANEL_VIEWS.has(previous)?previous:"home";
  const anchorHistory=state.view==="applicationSettings" && PANEL_VIEWS.has(previous)
    ? state.history.slice(0,-1) : state.history;
  const musicReturn = state.musicReturn || {
    view: anchorView,
    history: [...anchorHistory],
  };
  const history = state.view === "applicationSettings" && state.history.at(-1) === "music"
    ? state.history.slice(0, -1) : state.history;
  return {...state, view:"music", history, musicReturn};
}

export function leaveMusic(state) {
  if (state.view !== "music") return state;
  const target = state.musicReturn;
  const {musicReturn: _musicReturn, ...panel} = state;
  return {...panel, view:target?.view || "home", history:target ? [...target.history] : []};
}
