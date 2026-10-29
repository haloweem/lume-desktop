import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';

const vendor=new URL('../music-visualizers/gallery-vendor/',import.meta.url);
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
test('generated Gallery dependencies and default asset match the source manifest',()=>{
  const manifest=JSON.parse(readFileSync(new URL('manifest.json',vendor),'utf8'));
  for(const generated of manifest.generated) {
    const bytes=readFileSync(new URL(generated.path,vendor));
    assert.equal(hash(bytes),generated.sha256,generated.path);
    assert.equal(bytes.length,generated.bytes);
    assert.doesNotMatch(bytes.toString(),/\b(?:require\(|React\.|react-dom|node:)/);
  }
  const image=readFileSync(new URL('../assets/music/forest.webp',import.meta.url));
  assert.equal(image.toString('ascii',0,4),'RIFF');
  assert.equal(image.toString('ascii',8,12),'WEBP');
  assert.ok(!image.includes(Buffer.from('ANIM')), 'bundled forest is a single frame');
  assert.equal(Object.keys(manifest.sources).some(name=>name.includes('GalleryLyricsRail.tsx')),false);
  for(const [license,source] of [
    ['Folia-AGPL-3.0.txt','LICENSE'],
    ['pretext-LICENSE.txt','node_modules/@chenglou/pretext/LICENSE'],
    ['motion-dom-LICENSE.txt','node_modules/motion-dom/LICENSE.md'],
    ['motion-utils-LICENSE.txt','node_modules/motion-utils/LICENSE.md'],
  ])assert.equal(hash(readFileSync(new URL(license,vendor))),manifest.sources[source]);
});
