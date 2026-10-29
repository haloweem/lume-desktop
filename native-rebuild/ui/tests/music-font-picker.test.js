import test from 'node:test';
import assert from 'node:assert/strict';
import {musicFontFamilies} from '../music-font-picker.js';
import {musicSettings} from '../music-settings.js';
import {DEFAULT_MUSIC_PREFERENCES} from '../music-preference-values.js';

test('system families match historical trimming, case-insensitive deduplication and sorting',()=>{
  const names=['Zeta','Arial','宋体'];
  assert.deepEqual(musicFontFamilies([
    {family:' Zeta '},{family:'Arial',fullName:'Arial Bold'},{family:'arial'},
    {family:'宋体'},{family:'ARIAL',fullName:'Arial Italic'},{family:''},{family:' '},{},null,
  ]),names.sort((a,b)=>a.localeCompare(b)));
});

test('system families stay within the existing fontFamily DTO without truncation',()=>{
  assert.deepEqual(musicFontFamilies([
    {family:42},{family:'bad\nfont'},{family:'bad\ud800font'},
    {family:'a'.repeat(129)},{family:'字'.repeat(43)},{family:'a'.repeat(128)},
  ]),['a'.repeat(128)]);
});

test('eight quick fonts and default remain, and the system picker follows loaded state',()=>{
  const markup=musicSettings({loaded:true});
  const select=markup.match(/<select id="music-font"[\s\S]*?<\/select>/)[0];
  assert.equal((select.match(/<option /g)||[]).length,8);
  assert.match(select,/<option value="" selected>默认字体<\/option>/);
  assert.match(select,/class="MusicModeProxy"/);
  assert.match(markup,/<button[^>]*data-music-font-trigger="quick"[^>]*aria-haspopup="listbox"[^>]*>默认字体<\/button>/);
  assert.match(markup,/<button[^>]*data-music-font-trigger(?![^>]*disabled)[^>]*>更换<\/button>/);
  assert.match(musicSettings(),/<button[^>]*data-music-font-trigger[^>]*disabled/);
  assert.doesNotMatch(markup,/上传字体|data-music-unavailable="local-font"/);
});

test('initial imported font markup hides its internal alias in both visible and proxy labels',()=>{
  const family=`LumeImportedFont_${'a'.repeat(32)}`;
  const markup=musicSettings({loaded:true,preferences:{...DEFAULT_MUSIC_PREFERENCES,fontFamily:family}});
  assert.match(markup,new RegExp(`<option value="${family}" selected>已导入字体</option>`));
  assert.match(markup,/<button[^>]*data-music-font-trigger="quick"[^>]*>已导入字体<\/button>/);
  assert.doesNotMatch(markup,/>LumeImportedFont_/);
});
