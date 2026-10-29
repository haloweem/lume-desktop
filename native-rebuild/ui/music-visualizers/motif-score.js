// 音符律动: turns the native melody analysis (pitch frames + beat grid) and
// the lyric timing into an engraved single-staff melody in 4/4. Pure data;
// no DOM, no renderer.

const STEP = 0.25;                          // sixteenth-note grid, in beats
const VALUES = [0.25, 0.5, 0.75, 1, 1.5, 2, 3, 4];
const LETTERS = [0, 2, 4, 5, 7, 9, 11];     // C D E F G A B
// Krumhansl–Kessler key profiles.
const MAJOR = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88];
const MINOR = [6.33, 2.68, 3.52, 5.38, 2.6, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17];
// Fifths from C for each major tonic pitch class (flats negative).
const FIFTHS = [0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5];
// Lyric marks that are not sung (CJK and ASCII punctuation).
const PUNCTUATION = /^[\p{P}\p{S}]$/u;
const SHARP_ORDER = [3, 0, 4, 1, 5, 2, 6];  // F C G D A E B as letter indexes
const FLAT_ORDER = [6, 2, 5, 1, 4, 0, 3];   // B E A D G C F

const median = values => {
  if (!values.length) return NaN;
  const sorted = [...values].sort((a, b) => a - b);
  return sorted[sorted.length >> 1];
};

// Beat grid with linear extension past both ends, so any time maps to a
// fractional beat and back.
export function beatGrid(beats, tempo, duration) {
  let grid = Array.isArray(beats) ? beats.filter(Number.isFinite) : [];
  if (grid.length < 8) {
    const period = 60 / (tempo > 30 && tempo < 300 ? tempo : 100);
    grid = Array.from({length: Math.ceil((duration || 600) / period) + 2}, (_, i) => i * period);
  }
  const gaps = grid.slice(1).map((value, i) => value - grid[i]);
  const period = median(gaps);
  const beatAt = time => {
    if (time <= grid[0]) return (time - grid[0]) / period;
    const last = grid.length - 1;
    if (time >= grid[last]) return last + (time - grid[last]) / period;
    let low = 0, high = last;
    while (high - low > 1) { const mid = (low + high) >> 1; if (grid[mid] <= time) low = mid; else high = mid; }
    return low + (time - grid[low]) / (grid[low + 1] - grid[low]);
  };
  const timeAt = beat => {
    if (beat <= 0) return grid[0] + beat * period;
    const last = grid.length - 1;
    if (beat >= last) return grid[last] + (beat - last) * period;
    const index = Math.floor(beat);
    return grid[index] + (beat - index) * (grid[index + 1] - grid[index]);
  };
  return {beatAt, timeAt, period, tempo: 60 / period};
}

// Note time spans: one per sung syllable when the lyric has word timing,
// otherwise its text split evenly, otherwise stable stretches of the pitch track.
function spans(lines, pitch, hop) {
  const result = [];
  if (Array.isArray(lines) && lines.length) {
    for (const line of lines) {
      const words = (line.words || []).filter(word => word.text?.trim());
      if (words.length) {
        words.forEach((word, i) => {
          const next = words[i + 1]?.startTime ?? line.endTime;
          result.push({start: word.startTime, end: Math.max(word.endTime, Math.min(next, word.startTime + 0.25))});
        });
        continue;
      }
      const glyphs = [...(line.fullText || '')].filter(glyph => glyph.trim());
      const span = (line.endTime - line.startTime) * 0.92;
      glyphs.forEach((_, i) => result.push({
        start: line.startTime + span * i / glyphs.length, end: line.startTime + span * (i + 1) / glyphs.length}));
    }
    if (result.length) return result;
  }
  // No lyrics: a note starts where voicing starts or the pitch moves ≥ 0.8
  // semitone and stays there for 80 ms.
  let start = -1, base = 0;
  const close = end => { if (start >= 0 && (end - start) * hop >= 0.12) result.push({start: start * hop, end: end * hop}); };
  for (let t = 0; t <= pitch.length; t++) {
    const value = pitch[t] || 0;
    if (!value) { close(t); start = -1; continue; }
    if (start < 0) { start = t; base = value; continue; }
    if (Math.abs(value - base) >= 8) {
      const held = pitch.slice(t, t + 4).every(v => v && Math.abs(v - value) < 5);
      if (held) { close(t); start = t; base = value; }
    }
  }
  return result;
}

export function detectKey(notes) {
  const histogram = Array(12).fill(0);
  for (const note of notes) histogram[((note.midi % 12) + 12) % 12] += note.beats;
  let best = {score: -Infinity, tonic: 0, minor: false};
  for (let tonic = 0; tonic < 12; tonic++) for (const minor of [false, true]) {
    const profile = minor ? MINOR : MAJOR;
    let score = 0;
    for (let pc = 0; pc < 12; pc++) score += histogram[pc] * profile[(pc - tonic + 12) % 12];
    if (score > best.score) best = {score, tonic, minor};
  }
  const fifths = FIFTHS[best.minor ? (best.tonic + 3) % 12 : best.tonic];
  return {tonic: best.tonic, minor: best.minor, fifths};
}

// Letter/alteration for a MIDI note in a key: staff step counts diatonic
// steps from C4 (0) upward; alter is -1, 0 or +1.
export function spell(midi, fifths) {
  const pc = ((midi % 12) + 12) % 12, octave = Math.floor(midi / 12) - 5;
  let letter = LETTERS.indexOf(pc), alter = 0;
  if (letter < 0) {
    if (fifths < 0) { letter = LETTERS.indexOf(pc + 1); alter = -1; }
    else { letter = LETTERS.indexOf(pc - 1); alter = 1; }
  }
  return {step: octave * 7 + letter, letter, alter};
}
export function keyAlteration(letter, fifths) {
  if (fifths > 0) return SHARP_ORDER.slice(0, fifths).includes(letter) ? 1 : 0;
  if (fifths < 0) return FLAT_ORDER.slice(0, -fifths).includes(letter) ? -1 : 0;
  return 0;
}
export const keySignature = fifths => (fifths >= 0 ? SHARP_ORDER : FLAT_ORDER)
  .slice(0, Math.abs(fifths)).map(letter => ({letter, alter: fifths > 0 ? 1 : -1}));

const snap = beats => VALUES.reduce((best, value) => value <= beats + 1e-6 ? value : best, 0.25);

/**
 * Build the engraved melody.
 * @param {{lines?: object[], score?: object, duration?: number}} input
 */
export function buildMotifScore({lines, score, duration} = {}) {
  const pitch = Array.isArray(score?.pitch) ? score.pitch : [];
  const hop = score?.hop > 0 ? score.hop : 0.02;
  const grid = beatGrid(score?.beats, score?.tempo, duration);
  const raw = [];
  for (const {start, end} of spans(lines, pitch, hop)) {
    const length = end - start;
    if (!(length > 0)) continue;
    const first = Math.floor((start + length * 0.2) / hop), last = Math.ceil((end - length * 0.1) / hop);
    const voiced = [];
    for (let t = Math.max(0, first); t <= Math.min(pitch.length - 1, last); t++) if (pitch[t]) voiced.push(pitch[t]);
    if (voiced.length < 2) continue;
    raw.push({start, end, midi: Math.round(median(voiced) / 10)});
  }
  // Octave errors: pull notes far from the recent line back by an octave.
  for (let i = 0; i < raw.length; i++) {
    const context = median(raw.slice(Math.max(0, i - 8), i).map(note => note.midi));
    if (Number.isFinite(context)) while (Math.abs(raw[i].midi - context) > 9) raw[i].midi += raw[i].midi > context ? -12 : 12;
  }
  // Quantize onto the sixteenth grid; no two notes share an onset.
  const notes = [];
  for (const note of raw) {
    let beat = Math.round(grid.beatAt(note.start) / STEP) * STEP;
    const endBeat = Math.round(grid.beatAt(note.end) / STEP) * STEP;
    const previous = notes[notes.length - 1];
    if (previous && beat < previous.beat + STEP) beat = previous.beat + STEP;
    if (previous) previous.beats = Math.min(previous.beats, beat - previous.beat);
    notes.push({beat, beats: Math.max(STEP, endBeat - beat), midi: note.midi, time: note.start});
  }
  // 4/4 phase: put as many onsets as possible on downbeats.
  let phase = 0, bestHits = -1;
  for (let p = 0; p < 4; p++) {
    const hits = notes.reduce((sum, note) => sum + (((note.beat - p) % 4 + 4) % 4 === 0 ? 2 : ((note.beat - p) % 2 + 2) % 2 === 0 ? 1 : 0), 0);
    if (hits > bestHits) { bestHits = hits; phase = p; }
  }
  // Sung characters, quantized like the notes so each sits under its note.
  const lyrics = [];
  (Array.isArray(lines) ? lines : []).forEach((line, lineIndex) => {
    const chars = [];
    const words = (line.words || []).filter(word => word.text?.trim());
    const push = (text, time) => { if (!PUNCTUATION.test(text)) chars.push({text, time}); };
    if (words.length) for (const word of words) {
      const glyphs = [...word.text.trim()].filter(glyph => glyph.trim());
      const span = Math.max(0, (word.endTime ?? word.startTime) - word.startTime);
      glyphs.forEach((glyph, k) => push(glyph, word.startTime + span * k / glyphs.length));
    } else {
      const glyphs = [...(line.fullText || '')].filter(glyph => glyph.trim());
      const span = (line.endTime - line.startTime) * 0.92;
      glyphs.forEach((glyph, k) => push(glyph, line.startTime + span * k / glyphs.length));
    }
    chars.forEach((char, k) => {
      let beat = Math.round(grid.beatAt(char.time) / STEP) * STEP;
      const previous = lyrics[lyrics.length - 1];
      if (previous && beat < previous.beat + STEP) beat = previous.beat + STEP;
      // Lit when the ball lands; scatters into light once the line has been sung.
      const time = grid.timeAt(beat);
      lyrics.push({text: char.text, beat, time, line: lineIndex,
        fade: Math.max(time + 1.6, (line.endTime ?? time) + 0.5) + k * 0.05});
    });
  });
  // The staff spans the whole audible song, intro and outro included, so the
  // beat keeps moving before the first and after the last sung note. Older
  // analyses without the span fall back to the beat grid and the duration.
  const beatTimes = Array.isArray(score?.beats) ? score.beats.filter(Number.isFinite) : [];
  const start = score?.end > score?.start ? score.start : beatTimes[0] ?? 0;
  const end = score?.end > score?.start ? score.end
    : duration > 0 ? duration : beatTimes[beatTimes.length - 1] ?? notes[notes.length - 1]?.time ?? 0;
  const starts = [notes[0]?.beat, lyrics[0]?.beat, Math.ceil(grid.beatAt(start) - 1e-6)].filter(Number.isFinite);
  const firstBeat = starts.length ? Math.floor((Math.min(...starts) - phase) / 4) * 4 + phase : phase;
  for (const lyric of lyrics) lyric.measure = Math.floor((lyric.beat - firstBeat) / 4);
  for (const note of notes) {
    note.measure = Math.floor((note.beat - firstBeat) / 4);
    const measureEnd = firstBeat + (note.measure + 1) * 4;
    note.beats = snap(Math.min(note.beats, 4, measureEnd - note.beat));
    note.time = grid.timeAt(note.beat);   // land on the beat grid, in step with the audio
  }
  const key = detectKey(notes);
  const center = median(notes.map(note => note.midi));
  const octaveDown = Number.isFinite(center) && center < 57;   // written an octave up, "8" under the clef
  for (const note of notes) {
    const spelled = spell(note.midi + (octaveDown ? 12 : 0), key.fifths);
    Object.assign(note, spelled);
  }
  // Courtesy-free accidentals: shown when the note differs from what the
  // key signature or an earlier accidental in the same measure implies.
  let measure = -1, carried = new Map();
  for (const note of notes) {
    if (note.measure !== measure) { measure = note.measure; carried = new Map(); }
    const implied = carried.get(note.step) ?? keyAlteration(note.letter, key.fifths);
    note.accidental = note.alter === implied ? null : note.alter;
    carried.set(note.step, note.alter);
  }
  const ends = [notes.length ? notes[notes.length - 1].beat + notes[notes.length - 1].beats : NaN,
    lyrics.length ? lyrics[lyrics.length - 1].beat + STEP : NaN, Math.floor(grid.beatAt(end) + 1e-6) + 1].filter(Number.isFinite);
  const lastBeat = ends.length ? Math.max(...ends) : firstBeat + 4;
  const measures = Math.max(1, Math.ceil((lastBeat - firstBeat) / 4));
  return {notes, lyrics, key, octaveDown, firstBeat, measures, grid, start, end};
}
