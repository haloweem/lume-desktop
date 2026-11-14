import test from 'node:test';
import assert from 'node:assert/strict';
import {compactTimeReadings,compactWidgetThumbnail} from '../compact-widget-thumbnails.js';
import {compactWidgetPicker} from '../compact-widgets.js';
import {initialState,reduce} from '../state.js';
import {setLanguage} from '../localization.js';

const values=date=>new Map(compactTimeReadings(date.getTime()).map(row=>[row.id,row]));
const close=(actual,expected)=>assert.ok(Math.abs(actual-expected)<1e-12,`${actual} != ${expected}`);

test('calendar fallback captures the local date and all five real progress values',()=>{
  setLanguage('zh-CN');
  const rows=values(new Date(2026,10,5,12));
  assert.equal(rows.get(2).text,'周四 05');
  assert.equal(rows.get(3).text,'11/5');assert.equal(rows.get(4).text,'周四');assert.equal(rows.get(5).text,'12:00');
  close(rows.get(6).progress,.5);close(rows.get(7).progress,.5);close(rows.get(8).progress,.15);
  close(rows.get(9).progress,35.5/92);close(rows.get(10).progress,308.5/365);
  for(const id of [6,7,8,9,10])assert.equal(rows.get(id).text,`${Math.round(rows.get(id).progress*100)}%`);
});

test('calendar progress handles leap years and Monday, month, quarter and year boundaries',()=>{
  const leap=values(new Date(2024,1,29));
  close(leap.get(8).progress,28/29);close(leap.get(9).progress,59/91);close(leap.get(10).progress,59/366);
  const monday=values(new Date(2026,10,2));close(monday.get(7).progress,0);
  const nextYear=values(new Date(2027,0,1));
  for(const id of [6,8,9,10])close(nextYear.get(id).progress,0);
  const end=values(new Date(2026,11,31,23,59,59));
  assert.ok(end.get(10).progress>.9999&&end.get(10).progress<1);
  // Local wall-clock progress remains noon on a DST transition day.
  close(values(new Date(2024,2,10,12)).get(6).progress,.5);
  assert.deepEqual(compactTimeReadings(NaN),[]);
});

test('first native snapshot wins over calendar fallback and later preview animation frames',()=>{
  let state={...initialState(),islandCompactCapturedAt:new Date(2026,10,5,12).getTime()};
  const frame=readings=>({type:'host',message:{type:'dynamicIsland-preview',readings}});
  state=reduce(state,frame([]));assert.equal(state.islandCompactReadings,null);
  const first=[{id:3,text:'11/6',available:true,progress:null},{id:6,text:'7%',available:true,progress:.07}];
  state=reduce(state,frame(first));
  const captured=compactWidgetPicker(state);
  assert.match(captured,/11\//);assert.match(captured,/>7%<\/text>/);
  state=reduce(state,frame([{id:6,text:'99%',available:true,progress:.99}]));
  assert.equal(state.islandCompactReadings,first);
  assert.equal(compactWidgetPicker(state),captured,'preview changes do not update the open picker');
  state=reduce({...state,islandCompactReadings:null,islandCompactCapturedAt:new Date(2026,10,6,12).getTime()},
    frame([{id:6,text:'99%',available:true,progress:.99}]));
  assert.match(compactWidgetPicker(state),/>99%<\/text>/,'a new visit accepts a new snapshot');
});

test('system chooser icons stay visible without readings and captured date text stays escaped',()=>{
  for(let id=11;id<=17;id++){
    const icon=compactWidgetThumbnail(id);
    assert.match(icon,/<svg[^]*?<path/);
    assert.doesNotMatch(icon,/<text|—|%/);
    assert.equal(compactWidgetThumbnail(id,{available:false,text:'48%',progress:.48}),icon);
    assert.equal(compactWidgetThumbnail(id,{available:true,text:'82%',progress:.82}),icon);
  }
  assert.doesNotMatch(compactWidgetThumbnail(3,{available:true,text:'<script>/5'}),/<script>/);
  setLanguage('en');assert.equal(values(new Date(2026,10,5)).get(4).text,'Thu');setLanguage('zh-CN');
});
