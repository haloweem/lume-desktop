import test from 'node:test';
import assert from 'node:assert/strict';
import {beatGrid,buildMotifScore,detectKey,keyAlteration,spell} from '../music-visualizers/motif-score.js';

// 120 BPM grid; a sung line C D E F G (one syllable per beat) starting at 1.0 s.
const beats=Array.from({length:40},(_,i)=>0.5*i);
function track(notes,seconds=20){
  const pitch=Array(Math.round(seconds/0.02)).fill(0);
  for(const {start,end,midi} of notes)for(let t=Math.round(start/0.02);t<Math.round(end/0.02);t++)pitch[t]=midi*10;
  return {version:1,type:'music-score',session:1,trackId:'t',hop:0.02,tempo:120,pitch,beats};
}
const sung=[60,62,64,65,67].map((midi,i)=>({start:1+0.5*i,end:1.45+0.5*i,midi,text:'啦'}));
const lyric=[{startTime:1,endTime:3.6,fullText:'啦啦啦啦啦',translation:'',
  words:sung.map(note=>({startTime:note.start,endTime:note.end,text:note.text}))}];

test('beat grid maps time to beats and back, extending past both ends',()=>{
  const grid=beatGrid(beats,120,20);
  assert.equal(grid.beatAt(1.25),2.5);
  assert.equal(grid.timeAt(2.5),1.25);
  assert.equal(grid.beatAt(-0.5),-1);
  assert.ok(Math.abs(grid.timeAt(50)-25)<1e-9);
  assert.ok(Math.abs(beatGrid([],90,10).tempo-90)<1e-9,'no beats: steady grid from the tempo');
});

test('each sung syllable becomes one note with its real pitch, on the beat',()=>{
  const score=buildMotifScore({lines:lyric,score:track(sung)});
  assert.deepEqual(score.notes.map(note=>note.midi),[60,62,64,65,67]);
  assert.deepEqual(score.notes.map(note=>note.beat),[2,3,4,5,6]);
  assert.ok(score.notes.every(note=>note.beats===1),'quarter notes');
  assert.ok(score.notes.every((note,i)=>Math.abs(note.time-(1+0.5*i))<1e-9),'balls land in time with the audio');
  assert.equal(score.key.fifths,0);
  assert.ok(score.notes.every(note=>note.accidental===null));
  // Downbeat phase puts the first onset on a measure start; the staff itself
  // begins with the audio, so the bar before the singing is there too.
  assert.equal(score.firstBeat,-2);
  assert.deepEqual(score.notes.map(note=>note.measure),[1,1,1,1,2]);
});

test('the staff spans the audible song, not just the sung part',()=>{
  const score=buildMotifScore({lines:lyric,score:{...track(sung),start:0.4,end:14.2}});
  assert.equal(score.start,0.4);
  assert.equal(score.end,14.2);
  assert.ok(score.grid.timeAt(score.firstBeat)<=0.4,'starts with the first sound');
  assert.ok(score.grid.timeAt(score.firstBeat+score.measures*4)>14.2,'ends with the last sound');
  // An older analysis without the span still runs to the end of the track.
  const fallback=buildMotifScore({lines:lyric,score:track(sung),duration:18});
  assert.equal(fallback.end,18);
  assert.ok(fallback.grid.timeAt(fallback.firstBeat+fallback.measures*4)>18);
});

test('sung characters sit on the beat grid under their notes and fade after the line',()=>{
  const lines=[{...lyric[0],words:[...lyric[0].words.slice(0,4),{startTime:3,endTime:3.45,text:'，'},
    {startTime:3.0,endTime:3.45,text:'好的'}]}];
  const score=buildMotifScore({lines,score:track(sung)});
  assert.deepEqual(score.lyrics.map(item=>item.text),['啦','啦','啦','啦','好','的'],'punctuation is dropped, words split by character');
  assert.deepEqual(score.lyrics.slice(0,4).map(item=>item.beat),score.notes.slice(0,4).map(note=>note.beat));
  assert.ok(score.lyrics.every((item,i)=>!i||item.beat>score.lyrics[i-1].beat),'no two characters share a slot');
  assert.ok(score.lyrics.every(item=>item.fade>item.time+1 && item.fade>=lines[0].endTime),'they scatter only after being lit and sung');
  assert.equal(buildMotifScore({lines:[],score:track(sung)}).lyrics.length,0);
});

test('octave slips are folded back and low voices are written an octave up',()=>{
  const slipped=sung.map((note,i)=>i===3?{...note,midi:note.midi+12}:note);
  assert.equal(buildMotifScore({lines:lyric,score:track(slipped)}).notes[3].midi,65);
  const low=sung.map(note=>({...note,midi:note.midi-12}));
  const score=buildMotifScore({lines:lyric,score:track(low)});
  assert.equal(score.octaveDown,true);
  assert.equal(score.notes[0].step,0,'C3 sung, written as C4 with the octave clef');
});

test('keys, spelling and accidentals follow the detected key',()=>{
  assert.equal(detectKey([67,69,71,72,74,76,78,79].map(midi=>({midi,beats:1}))).fifths,1,'G major');
  assert.equal(detectKey([65,67,69,70,72,74,76,77].map(midi=>({midi,beats:1}))).fifths,-1,'F major');
  assert.deepEqual(spell(61,2),{step:0,letter:0,alter:1},'C sharp in D major');
  assert.deepEqual(spell(70,-1),{step:6,letter:6,alter:-1},'B flat in F major');
  assert.equal(keyAlteration(3,1),1,'F is sharp in G major');
  // Measure memory: a repeated altered note in the same bar shows its sign once.
  const chromatic=[60,61,61,62].map((midi,i)=>({start:1+0.5*i,end:1.45+0.5*i,midi,text:'啦'}));
  const lines=[{startTime:1,endTime:3,fullText:'啦啦啦啦',translation:'',
    words:chromatic.map(note=>({startTime:note.start,endTime:note.end,text:'啦'}))}];
  const score=buildMotifScore({lines,score:track(chromatic)});
  assert.equal(score.notes[1].accidental,1,'C sharp after C natural shows its sharp');
  assert.equal(score.notes[2].accidental,null,'and carries through the measure');
});

test('without lyrics the pitch track itself is segmented into notes',()=>{
  const score=buildMotifScore({lines:[],score:track(sung)});
  assert.deepEqual(score.notes.map(note=>note.midi),[60,62,64,65,67]);
});

test('unvoiced syllables leave rests instead of inventing notes',()=>{
  const silent=track([]);
  assert.equal(buildMotifScore({lines:lyric,score:silent}).notes.length,0);
  assert.equal(buildMotifScore({lines:lyric,score:null}).notes.length,0);
});

test('a melody analysis handed to the view survives state normalization',async()=>{
  // Regression: the view re-normalizes each update; an incoming score must
  // replace the previous (empty) one rather than be overwritten by it.
  for(const path of ['../music.js','../music-runtime-state.js']){
    const {initialMusicState,normalizeMusicState}=await import(path);
    const current={id:'t',title:'x',source:'online:t',duration:10};
    const base=normalizeMusicState({current,session:1},initialMusicState());
    const score={version:1,type:'music-score',session:1,trackId:'t',hop:0.02,tempo:120,pitch:[600],beats:[0.5]};
    assert.equal(normalizeMusicState({...base,score},base).score,score,path);
    assert.equal(normalizeMusicState({current,session:1},{...base,score}).score,score,'state ticks keep it');
    assert.equal(normalizeMusicState({current:{...current,id:'u'},session:1},{...base,score}).score,null,'a new track clears it');
  }
});

test('the bridge accepts a score with or without its audible span',async()=>{
  const {validMusicScore}=await import('../bridge.js');
  const score={version:1,type:'music-score',session:1,trackId:'t',hop:0.02,tempo:120,pitch:[600],beats:[0.5]};
  assert.equal(validMusicScore(score),true,'older hosts send no span');
  assert.equal(validMusicScore({...score,start:2.58,end:264.96}),true);
  assert.equal(validMusicScore({...score,start:5,end:2}),false,'span runs forwards');
  assert.equal(validMusicScore({...score,start:1}),false,'both ends or neither');
});
