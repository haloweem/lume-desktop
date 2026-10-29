import assert from "node:assert/strict";
import { test } from "node:test";
import { prepareInitialView } from "../startup.js";

function deferred() {
  let resolve, reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}
function fixture() {
  const font = deferred(), image = deferred(), frames = [], decoded = [];
  const preview = {
    style: {},
    getAnimations: () => [{ finish() { preview.finished = true; } }],
  };
  const root = {
    ownerDocument: {
      fonts: { ready: font.promise },
      defaultView: {
        Image: class {
          set src(value) { decoded.push(value); }
          decode() { return image.promise; }
        },
        getComputedStyle: () => ({
          getPropertyValue: () => 'url("assets/initial wallpaper.webp")',
        }),
        requestAnimationFrame(callback) { frames.push(callback); },
      },
    },
    querySelectorAll(selector) { return selector === "img" ? [] : [preview]; },
  };
  return { root, preview, font, image, frames, decoded };
}
async function microtasks() { for (let i = 0; i < 8; i++) await Promise.resolve(); }

test("an unavailable remote cover does not block the manager's first show",async()=>{
  for(const fail of [false,true]){
    const state=fixture();
    const image={src:'https://static.lumedesktop.com/slow.jpg',decode:()=>fail?Promise.reject(Error('CDN')):new Promise(()=>{})};
    state.root.querySelectorAll=selector=>selector==='img'?[image]:[];
    const preparation=prepareInitialView(state.root,{remoteTimeoutMs:10});state.font.resolve();
    await new Promise(resolve=>setTimeout(resolve,20));await microtasks();
    assert.equal(state.frames.length,1);state.frames.shift()(16);await microtasks();state.frames.shift()(32);
    await preparation;
  }
});

test("first-view readiness waits for both decoded wallpaper and fonts, then two frames", async () => {
  const state = fixture();
  let ready = false;
  const preparation = prepareInitialView(state.root).then(() => { ready = true; });
  await microtasks();
  assert.deepEqual(state.decoded, ["assets/initial wallpaper.webp"]);
  assert.equal(ready, false);
  state.font.resolve();
  await microtasks();
  assert.equal(state.frames.length, 0);
  state.image.resolve();
  await microtasks();
  assert.equal(state.preview.finished, true);
  assert.equal(state.preview.style.animation, "none");
  assert.equal(ready, false);
  assert.equal(state.frames.length, 1);
  state.frames.shift()(16);
  await microtasks();
  assert.equal(ready, false);
  assert.equal(state.frames.length, 1);
  state.frames.shift()(32);
  await preparation;
  assert.equal(ready, true);
});

test("a broken initial wallpaper is not silently called ready", async () => {
  const state = fixture();
  const preparation = prepareInitialView(state.root);
  state.font.resolve();
  state.image.reject(new Error("decode failed"));
  await assert.rejects(preparation, /decode failed/);
  assert.equal(state.frames.length, 0);
});

test("ordinary inline images also finish decoding before first show", async () => {
  const state = fixture(), inline = deferred();
  state.root.querySelectorAll = selector => selector === "img" ? [{ decode: () => inline.promise }] : [];
  const preparation = prepareInitialView(state.root);
  state.font.resolve();
  await microtasks();
  assert.equal(state.frames.length, 0);
  inline.resolve();
  await microtasks();
  state.frames.shift()(16);
  await microtasks();
  state.frames.shift()(32);
  await preparation;
});
