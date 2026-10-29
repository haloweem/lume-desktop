import test from 'node:test';
import assert from 'node:assert/strict';
import {modules} from './music-3d-harness.js';
const close = (actual, expected, tolerance = 1e-10) => assert.ok(Math.abs(actual - expected) < tolerance, `${actual} ~= ${expected}`);

test('orbit, wheel reversal, pan, drag and inertia preserve the authored math', async () => {
  const {stage: s} = await modules(), orbit = s.createMineradio3DOrbitState();
  assert.deepEqual(orbit, {theta: 0, phi: 0.06, radius: 16});
  close(s.resolveMineradio3DOrbitPosition(orbit).length(), 16);
  close(s.applyMineradio3DOrbitWheel(orbit, -120).radius, 14.56);
  close(s.applyMineradio3DOrbitWheel({...orbit, radius: 10}, 120, 14).radius, 15.44);
  assert.equal(s.applyMineradio3DOrbitWheel(orbit, -99999).radius, 8);
  assert.equal(s.applyMineradio3DOrbitWheel(orbit, 99999).radius, 28);
  close(s.easeMineradio3DOrbitCamera(orbit, {...orbit, radius: 10}).radius, 15.58);
  assert.deepEqual([0, 1, 2].map(s.resolveMineradio3DPointerMode), ['rotate', 'pan', null]);
  const pan = s.applyMineradio3DStagePan({x: 0, y: 0}, 100, 50, 700, 16, 44);
  close(pan.x, 100 * 32 * Math.tan(22 * Math.PI / 180) / 700); close(pan.y, -pan.x / 2);
  const spin = s.applyMineradio3DStageDrag({x: 0, y: 0, velocityX: 0, velocityY: 0}, 100, -50, 0.1);
  close(spin.x, -0.16); close(spin.y, 0.34); close(spin.velocityY, 1.564);
  const next = s.advanceMineradio3DStageSpin(spin, 0.1);
  close(next.y, 0.4964); close(next.velocityY, 1.564 * 0.9 ** 6);
  const rebased = s.rebaseMineradio3DStageSpin({...spin, x: Math.PI * 10.25, y: -Math.PI * 12.4});
  close(Math.cos(rebased.spin.x), Math.cos(Math.PI * 10.25));
  assert.equal(rebased.spin.velocityY, spin.velocityY);
  close(s.resolveMineradio3DRotationEase(1 / 30), 1 - 0.945 ** 2);
});

test('window, translated spacing, hierarchy and viewport scale match the original scene', async () => {
  const {stage: s} = await modules();
  assert.deepEqual(s.resolveMineradio3DLineWindow(5, 12), [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10]);
  assert.deepEqual(s.resolveMineradio3DLineWindow(-1, 12), [0, 1, 2, 3, 4, 5]);
  assert.deepEqual(s.resolveMineradio3DLineWindow(999, 1000), [994, 995, 996, 997, 998, 999]);
  assert.deepEqual(s.resolveMineradio3DLineWindow(0, 0), []);
  const compact = s.resolveMineradio3DPrimaryVirtualIndices([{fullText: 'same', translation: ' same '}, {}, {}]);
  const translated = s.resolveMineradio3DPrimaryVirtualIndices([{translation: 'translated'}, {}, {}]);
  close(compact[1], 1.136); close(translated[1], 2.1214);
  close(s.resolveMineradio3DLineFrame(compact[1], 0.5).y, -1.8355, 0.0001);
  close(s.resolveMineradio3DLineFrame(translated[1], 0.5).y, -3.4277, 0.0001);
  close(s.resolveMineradio3DTranslationFrame(true, 1).y, -1.4679, 0.0001);
  close(s.resolveMineradio3DLineFrame(1, 0.5).scale, 0.9 * 0.974);
  assert.equal(s.resolveMineradio3DLineFrame(-1, 1).opacity, 0.78);
  assert.equal(s.resolveMineradio3DTranslationFrame(true, 1).opacity, 0.94);
  for (const offset of [-2, -1, 0, 1, 2]) assert.ok(s.resolveMineradio3DLineFrame(offset, 0.5).opacity > 0);
  for (const offset of [-3, 3]) assert.equal(s.resolveMineradio3DLineFrame(offset, 0.5).opacity, 0);
  close(s.resolveMineradio3DResponsiveScale(0.31), 0.5);
  assert.equal(s.resolveMineradio3DResponsiveScale(16 / 9), 1);
  assert.deepEqual(s.resolveMineradio3DBackfaceTextureTransform(), {repeatX: -1, offsetX: 1});
  assert.equal(s.resolveMineradio3DStableActiveLineIndex(-1, 5, 10, false), 5);
  assert.equal(s.resolveMineradio3DStableActiveLineIndex(-1, 5, 10, true), 0);
  close(s.easeMineradio3DTrackOffset(0, 100, 1 / 60), 0.68);
});

test('karaoke uses seconds, timed graphemes, word gaps and emoji clusters', async () => {
  const {stage: s} = await modules(), fill = s.resolveMineradio3DKaraokeFillWidth;
  const line = {fullText: 'A B', startTime: 10, endTime: 14,
    words: [{text: 'A', startTime: 10, endTime: 11}, {text: 'B', startTime: 13, endTime: 14}]};
  close(fill(line, 10.5, [0, 10, 15, 30]), 5);
  close(fill(line, 12, [0, 10, 15, 30]), 10);
  close(fill(line, 13.5, [0, 10, 15, 30]), 22.5);
  close(fill(line, 14, [0, 10, 15, 30]), 30);
  const emoji = {fullText: 'A\u{1F469}\u200d\u{1F680}', startTime: 0, endTime: 4, words: []};
  close(fill(emoji, 3, [0, 10, 40]), 25);
});
