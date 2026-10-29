import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import {dirname,resolve} from 'node:path';

const uiRoot=resolve(dirname(fileURLToPath(import.meta.url)),'..');

test('主应用按需加载音乐运行时，不静态加载音乐视图和控制器',async()=>{
  const source=await readFile(resolve(uiRoot,'app.js'),'utf8');
  for(const module of [
    './music-view.js','./music-online.js','./music-library.js','./music-provider-account.js',
    './music-preferences.js','./music-background.js','./music-font-asset-query.js','./music-font-assets.js',
  ]) {
    assert.doesNotMatch(source,new RegExp(`import\\s+[^;]*from\\s+['"]${module.replace('.','\\.')}`),module);
  }
  assert.match(source,/import\(\s*['"]\.\/music-entry\.js['"]\s*\)/);
  assert.doesNotMatch(source,/musicView\b|musicOnline\b|musicLibrary\b|musicProviderAccount\b|musicPreferences\b|musicBackground\b|musicFontAssetQuery\b|musicFontAssets\b/);
  assert.doesNotMatch(source,/musicDesktop\.(state|setEnabled|dispose)\b/);
});
