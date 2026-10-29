import test from "node:test";
import assert from "node:assert/strict";
import {createBridge, validHostMessage} from "../bridge.js";

const line = (startTime, endTime, fullText = "line") => ({
  startTime, endTime, fullText, words: [], translation: "",
});

test("music lyrics wire message validates as a bounded independent host message", () => {
  const message = {
    version: 1,
    type: "music-lyrics",
    session: 7,
    trackId: "local-1",
    lines: [line(1.25, 3.5, "第一句"), line(3.5, 8, "第二句")],
  };
  assert.equal(validHostMessage(message), true);
  assert.equal(validHostMessage({
    ...message,
    lines: [{...line(1, 2), words: [{text: "word", startTime: 1, endTime: 2}] }],
  }), true, "native word timing is preserved at the bridge boundary");
  assert.equal(validHostMessage({...message,lines:[
    {...line(1,3),translation:"翻译"},line(2,4),
  ]}),true,"provider overlap and translations remain valid");
  assert.equal(validHostMessage({...message, lines: [line(2, 1)]}), false,
    "descending line time is rejected");
  assert.equal(validHostMessage({...message, lines: Array.from({length: 2000}, (_, i) => line(i, i + 1))}), true);
  assert.equal(validHostMessage({...message, lines: Array.from({length: 2001}, (_, i) => line(i, i + 1))}), false,
    "oversized wire arrays are rejected");
});

test("music lyrics arrive through subscribe without a per-tick command", () => {
  const listeners = new Set();
  const commands = [];
  const webview = {
    addEventListener: (_, listener) => listeners.add(listener),
    removeEventListener: (_, listener) => listeners.delete(listener),
    postMessage: message => commands.push(message),
  };
  const received = [];
  const bridge = createBridge(webview);
  const dispose = bridge.subscribe(message => received.push(message));
  const message = {version: 1, type: "music-lyrics", session: 2, trackId: "local-2", lines: []};
  listeners.forEach(listener => listener({data: message}));
  assert.deepEqual(received, [message]);
  assert.deepEqual(commands, []);
  dispose();
  assert.equal(listeners.size, 0);
});

test("lyrics bridge rejects extra fields, malformed timing and excessive decoded text", () => {
  const message = {version:1, type:"music-lyrics", session:1, trackId:"local-1", lines:[line(1, 2)]};
  for (const changes of [{version:2}, {session:-1}, {session:1.5}, {session:Number.MAX_SAFE_INTEGER + 1},
    {trackId:""}, {trackId:"x".repeat(129)}, {lines:null}, {path:"C:/secret.lrc"},
    {lines:[line(-1, 2)]}, {lines:[line(1, Infinity)]}, {lines:[line(1, NaN)]}, {lines:[line(1, 86401)]},
    {lines:[line(1, 1)]}, {lines:[line(2, 3), line(1, 2)]},
    {lines:[{...line(1, 2), translation:"x".repeat(4097)}]}, {lines:[{...line(1, 2), extra:true}]},
    {lines:[{...line(1,2),words:[{startTime:0,endTime:2,text:"bad"}]}]},
    {lines:[{...line(1,2),words:[{startTime:1,endTime:3,text:"bad"}]}]},
    {lines:[{...line(1,2),words:[{startTime:1,endTime:2,text:"bad",source:"private"}]}]},
    {lines:[line(1, 2, "x".repeat(4097))]}, {lines:[line(1, 2, "\u4e00".repeat(1366))]},
    {lines:Array.from({length:65}, (_, index) => line(index, index + 1, "x".repeat(4096)))}])
    assert.equal(validHostMessage({...message, ...changes}), false);
  assert.equal(validHostMessage({...message, trackId:"", lines:[]}), true);
  const posted = [];
  const bridge = createBridge({postMessage: message => posted.push(message)});
  assert.equal(bridge.music("loadLyrics", {session:1, id:"local-1", path:"C:/secret.lrc"}), false);
  assert.equal(bridge.music("loadLyrics", {id:"local-1"}), false);
  assert.equal(bridge.music("loadLyrics", {id:"local-1", session:0}), false);
  assert.equal(bridge.music("loadLyrics", {id:"", session:1}), false);
  assert.deepEqual(posted, []);
});
