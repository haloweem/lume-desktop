import test from "node:test";
import assert from "node:assert/strict";
import {createBridge} from "../bridge.js";
import {initialMusicState, musicPage} from "../music.js";
import {mountMusicDrop} from "../music-drop.js";

test("music page exposes a local import control and isolated drop zone", () => {
  const html = musicPage(initialMusicState());
  assert.match(html, /data-music-action="import-local"/);
  assert.match(html, /data-music-file-input/);
  assert.match(html, /data-music-drop-zone="local"/);
  assert.match(html, /accept="audio\//);
});

test("local tracks remain playable in the signed-out view", () => {
  const state = {
    ...initialMusicState(),
    queue: [{id: "local-1", title: "本地歌曲", source: "C:/Music/song.mp3", duration: 30}],
    current: {id: "local-1", title: "本地歌曲", source: "C:/Music/song.mp3", duration: 30},
  };
  const html = musicPage(state, {view: {nav: "local"}});
  assert.match(html, /本地歌曲/);
  assert.doesNotMatch(html, /登录，开启你的音乐/);
  const play = html.match(/<button\b[^>]*data-music-action="local-play"[^>]*aria-label="播放 本地歌曲"[^>]*>/)?.[0];
  assert.ok(play, "signed-out local navigation exposes the track's play control");
  assert.doesNotMatch(play, /\bdisabled\b/);

  const home = musicPage(state, {view: {nav: "recommendations"}});
  assert.match(home, /登录，开启你的音乐/);
  assert.match(home, /class="MusicProviderButtons"/);
  for (const provider of ["netease", "qq", "kugou"]) {
    assert.match(home, new RegExp(`<button\\b[^>]*data-music-provider-open="${provider}"[^>]*>`));
  }
});

test("local music files use WebView additional objects and the music dropzone command", () => {
  const messages = [];
  const files = [new File(["one"], "one.mp3"), new File(["two"], "two.flac")];
  const bridge = createBridge({
    postMessage: message => messages.push(message),
    postMessageWithAdditionalObjects: (...args) => messages.push(args),
  });
  assert.equal(bridge.musicDropFiles(files), true);
  assert.deepEqual(messages[0], ["v1|music|dropFiles", files]);
  assert.equal(bridge.send("window", "dropzone-music"), true);
  assert.deepEqual(messages[1], "v1|window|dropzone-music");
  assert.equal(bridge.musicDropFiles(Array.from({length: 33}, () => files[0])), false);
});

test("music drop only accepts files inside the enabled local zone", () => {
  const handlers = new Map(), received = [], errors = [];
  let enabled = true, active = false;
  const zone = {classList: {toggle: (_, value) => { active = value; }}};
  const root = {
    addEventListener: (name, handler) => handlers.set(name, handler),
    removeEventListener: name => handlers.delete(name),
  };
  const unmount = mountMusicDrop(root, {
    isEnabled: () => enabled,
    onFiles: files => received.push(files),
    onError: error => errors.push(error),
  });
  const files = [new File(["one"], "one.mp3"), new File(["two"], "two.flac")];
  const event = (local = true, data = files) => ({
    target: {closest: () => local ? zone : null},
    relatedTarget: null,
    dataTransfer: {types: ["Files"], files: data},
    prevented: false,
    preventDefault() { this.prevented = true; },
  });
  const drag = event();
  handlers.get("dragover")(drag);
  assert.equal(drag.prevented, true);
  assert.equal(drag.dataTransfer.dropEffect, "copy");
  assert.equal(active, true);
  const drop = event();
  handlers.get("drop")(drop);
  assert.equal(drop.prevented, true);
  assert.deepEqual(received, [files]);
  enabled = false;
  handlers.get("drop")(event());
  assert.equal(received.length, 1);
  enabled = true;
  handlers.get("drop")(event(true, Array.from({length: 33}, () => files[0])));
  assert.match(errors.at(-1), /32/);
  unmount();
  assert.equal(handlers.size, 0);
});
