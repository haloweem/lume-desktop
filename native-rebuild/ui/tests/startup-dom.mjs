import assert from "node:assert/strict";

const flushMicrotasks = async () => {
  for (let index = 0; index < 8; index++) await Promise.resolve();
};

// jsdom supplies the DOM but not these browser rendering APIs. Keep their
// completion explicitly controlled so a failed or skipped startup cannot be
// mistaken for a passing interaction test.
export function installStartupBrowserApis(view) {
  const frames = [], decoded = [];
  let resolveFonts;
  const fontsReady = new Promise(resolve => { resolveFonts = resolve; });
  Object.defineProperty(view.document, "fonts", {
    configurable: true,
    value: { ready: fontsReady },
  });
  view.HTMLImageElement.prototype.decode = function () {
    decoded.push(this.src);
    return Promise.resolve();
  };
  view.Element.prototype.getAnimations = () => [];
  view.requestAnimationFrame = callback => frames.push(callback);
  // App navigation mounts the music compositor; pixels are verified in Edge.
  view.HTMLCanvasElement.prototype.getContext = function (type) {
    return type === "2d" ? { globalAlpha: 1, fillStyle: "",
      clearRect() {}, fillRect() {}, drawImage() {} } : null;
  };

  return async (commands, connected = true) => {
    const readyCount = () => commands.filter(message => message === "v1|ready").length;
    await flushMicrotasks();
    assert.equal(readyCount(), 0, "ready waits for the initial fonts");
    assert.deepEqual(decoded, [], "an empty official catalog does not decode bundled or remote covers");
    assert.equal(view.document.querySelectorAll(".HomeHero [data-wallpaper-id], .HomeHero img").length, 0,
      "startup does not substitute a bundled wallpaper before the catalog arrives");
    for (const page of ["home","wallpaperLocal","wallpaperOfficial","desktopBeauty"])
      assert.ok(view.document.querySelector(`.module-tab[data-view="${page}"]:not([disabled])`),
        `${page} navigation is ready before the official catalog arrives`);
    assert.equal(frames.length, 0, "render frames cannot precede resources");
    resolveFonts();
    await flushMicrotasks();
    assert.equal(frames.length, 1, "startup requests its first render frame");
    frames.shift()(16);
    await flushMicrotasks();
    assert.equal(readyCount(), 0, "one render frame cannot signal ready");
    assert.equal(frames.length, 1, "startup requests its second render frame");
    frames.shift()(32);
    await flushMicrotasks();
    assert.equal(readyCount(), connected ? 1 : 0,
      "connected DOM interactions require exactly one completed homepage ready command");
    assert.doesNotMatch(view.document.querySelector("#notice").textContent,
      /首页资源加载失败/, "startup errors must fail the integration runner");
  };
}
