import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const reference=fileURLToPath(new URL('../music-visualizers/reference-next/',import.meta.url));
const manifest=JSON.parse(readFileSync(path.join(reference,'manifest.json'),'utf8'));

test('newer Folia renderers are listed with their upstream revision',()=>{
  assert.deepEqual(manifest.modes,['cappella','diorama','pendolo','tempera']);
  assert.match(manifest.upstreamRevision,/^[0-9a-f]{40}$/);
  for(const mode of manifest.modes)assert.ok(manifest.modeBytes[mode]>0,mode);
});

test('generated files, adapters and licences match the manifest',()=>{
  for(const item of manifest.generated){
    const bytes=readFileSync(path.join(reference,item.path));
    assert.equal(bytes.length,item.bytes,item.path);
    assert.equal(createHash('sha256').update(bytes).digest('hex'),item.sha256,item.path);
  }
  for(const [file,hash] of Object.entries(manifest.adapters))
    assert.equal(createHash('sha256').update(readFileSync(path.join(reference,file))).digest('hex'),hash,file);
  assert.ok(manifest.generated.some(item=>item.path==='licenses/Folia-AGPL-3.0.txt'));
  for(const dependency of Object.values(manifest.dependencies))
    for(const license of dependency.licenses)assert.ok(manifest.generated.some(item=>item.path===`licenses/${license}`),license);
});

test('the bundle carries no Folia app surface (accounts, providers, storage, analysis)',()=>{
  const app=/services\/(?:onlineMusic|db|appDatabase|repositories|analysis)\b|components\/app\/|(?:^|\/)providers?\//;
  const sources=Object.keys(manifest.sources).filter(name=>name.startsWith('src/'));
  assert.deepEqual(sources.filter(name=>app.test(name)),[]);
  const code=manifest.generated.filter(item=>item.path.endsWith('.js')).map(item=>readFileSync(path.join(reference,item.path),'utf8')).join('\n');
  assert.doesNotMatch(code,/import\.meta\.glob/);
  assert.doesNotMatch(code,/music\.163\.com|y\.qq\.com\/cgi|kugou\.com\/api/);
});

test('the stage host accepts every listed style, including the newer renderers',async()=>{
  const {MUSIC_VISUALIZERS}=await import('../music-visualizer.js');
  const {DEFAULT_MUSIC_PREFERENCES,musicPreferenceSnapshot}=await import('../music-preference-values.js');
  for(const {id} of MUSIC_VISUALIZERS)
    assert.doesNotThrow(()=>musicPreferenceSnapshot(undefined,{...DEFAULT_MUSIC_PREFERENCES,mode:id}),id);
  const host=readFileSync(fileURLToPath(new URL('../music-visualizers/reference-host.js',import.meta.url)),'utf8');
  assert.match(host,/musicPreferenceSnapshot as preferenceSnapshot\} from '\.\.\/music-preference-values\.js'/);
});
